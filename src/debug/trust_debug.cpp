#include "debug/trust_debug.h"
#include "debug/trust_source.h"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <unistd.h>

TrustDebug::TrustDebug() : TrustDebug(Config{}) {
}

TrustDebug::TrustDebug(const Config &cfg) : cfg_(cfg), source_(nullptr) {
    lldb::SBDebugger::Initialize();
    dbg_ = lldb::SBDebugger::Create();
    assert(dbg_.IsValid());
    // Асинхронный режим — единственный режим, в котором корректно работают
    // WaitForEvent() через listener и используется lldb-server.
    // Для DAP (через TrustDAP) асинхронный режим ОБЯЗАТЕЛЕН.
    if (!cfg_.lldbServerPath.empty()) {
        setenv("LLDB_DEBUGSERVER_PATH", cfg_.lldbServerPath.c_str(), 1);
    }
    listener_ = dbg_.GetListener();
}

TrustDebug::~TrustDebug() {
    if (proc_.IsValid()) {
        proc_.Destroy();
    }
    if (dbg_.IsValid()) {
        dbg_.Terminate();
    }
    lldb::SBDebugger::Terminate();
}

bool TrustDebug::CreateTarget(const std::string &exe) {
    tgt_ = dbg_.CreateTarget(exe.c_str());
    return tgt_.IsValid();
}

lldb::SBBreakpoint TrustDebug::BreakpointCreateByName(const std::string &name) {
    return tgt_.BreakpointCreateByName(name.c_str());
}

lldb::SBBreakpoint TrustDebug::BreakpointBySource(const std::string &file, int line) {
    if (source_) {
        auto mapping = source_->nearestTrustToCpp(file, line);
        if (!mapping.has_value()) {
            return {}; // invalid mapping
        }
        auto &[cppFile, cppLine] = mapping.value();
        // Try with full path first
        lldb::SBFileSpec spec(cppFile.c_str());
        lldb::SBBreakpoint sb = tgt_.BreakpointCreateByLocation(spec, cppLine);
        // Fallback: DWARF часто хранит только basename — пробуем его
        if (!sb.IsValid() || sb.GetNumLocations() == 0) {
            std::string fn = std::filesystem::path(cppFile).filename().string();
            if (fn != cppFile) {
                lldb::SBFileSpec spec2(fn.c_str());
                sb = tgt_.BreakpointCreateByLocation(spec2, cppLine);
            }
        }
        return sb;
    }
    // 1:1 режим — file/line как есть
    lldb::SBFileSpec spec(file.c_str());
    return tgt_.BreakpointCreateByLocation(spec, line);
}

lldb::SBProcess TrustDebug::Launch(const std::string &exe) {
    const char *argv[2] = {exe.c_str(), nullptr};
    lldb::SBLaunchInfo launchInfo(argv);
    launchInfo.SetLaunchFlags(cfg_.launchFlags);

    lldb::SBError err;
    proc_ = tgt_.Launch(launchInfo, err);
    if (!proc_.IsValid()) {
        std::cerr << "  FAIL: " << err.GetCString() << "\n";
        return proc_;
    }

    // В асинхронном режиме — подписываем listener на события процесса
    proc_.GetBroadcaster().AddListener(listener_, lldb::SBProcess::eBroadcastBitStateChanged | lldb::SBProcess::eBroadcastBitSTDOUT |
                                                      lldb::SBProcess::eBroadcastBitSTDERR);

    lldb::pid_t pid = proc_.GetProcessID();
    std::cout << "  PID=" << pid << "\n";
    if (pid == 0) {
        std::cerr << "  FAIL: PID=0, error=" << err.GetCString() << "\n";
    }

    return proc_;
}

TrustDebug::Event TrustDebug::WaitForEvent(int timeoutMs) {
    if (timeoutMs < 1)
        timeoutMs = 1;

    lldb::SBEvent evt;
    // Блокирующее ожидание с таймаутом
    if (listener_.WaitForEvent(timeoutMs * 1000, evt) && evt.IsValid()) {
        lldb::StateType state = lldb::SBProcess::GetStateFromEvent(evt);
        if (state == lldb::eStateStopped)
            return Event::Stop;
        if (state == lldb::eStateExited)
            return Event::Exit;
    }

    // Проверка на случай, если событие уже было, но мы его пропустили
    if (proc_.IsValid()) {
        lldb::StateType state = proc_.GetState();
        if (state == lldb::eStateExited)
            return Event::Exit;
    }

    return Event::Timeout;
}

void TrustDebug::StepOver() {
    proc_.GetSelectedThread().StepOver();
}

void TrustDebug::StepInto() {
    proc_.GetSelectedThread().StepInto();
}

void TrustDebug::StepOut() {
    proc_.GetSelectedThread().StepOut();
}

void TrustDebug::Continue() {
    proc_.Continue();
}

std::string TrustDebug::ReadStdout() {
    char buf[4096];
    size_t n = proc_.GetSTDOUT(buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
        return std::string(buf);
    }
    return {};
}

lldb::SBThread TrustDebug::GetThread() const {
    return proc_.GetSelectedThread();
}

lldb::SBFrame TrustDebug::GetFrame() const {
    return proc_.GetSelectedThread().GetSelectedFrame();
}

lldb::SBProcess TrustDebug::GetProcess() const {
    return proc_;
}

lldb::SBTarget TrustDebug::GetTarget() const {
    return tgt_;
}

std::vector<std::string> TrustDebug::GetVariablesBySource() {
    std::vector<std::string> result;
    lldb::SBFrame frame = GetFrame();
    if (!frame.IsValid())
        return result;

    lldb::SBValueList vars = frame.GetVariables(true, true, false, true);
    if (!vars.IsValid())
        return result;

    // Определяем C++ файл из frame для маппинга
    std::string cppFile;
    lldb::SBLineEntry lineEntry = frame.GetLineEntry();
    int cppLine = lineEntry.GetLine();
    if (lineEntry.GetFileSpec().IsValid()) {
        const char *name = lineEntry.GetFileSpec().GetFilename();
        if (name)
            cppFile = name;
    }

    for (size_t i = 0; i < vars.GetSize(); ++i) {
        lldb::SBValue v = vars.GetValueAtIndex(i);
        if (!v.IsValid())
            continue;

        std::string cppName = v.GetName() ? v.GetName() : "";

        if (source_) {
            // LLDB показывает C++ имена — транслируем в trust-имена
            auto trustName = source_->getTrustVar(cppFile, cppLine, cppName);
            if (trustName.has_value()) {
                result.push_back(trustName->vars.first);
            } else {
                result.push_back(cppName);
            }
        } else {
            result.push_back(cppName);
        }
    }

    return result;
}

lldb::SBValue TrustDebug::GetValueBySource(const std::string &varName) {
    lldb::SBFrame frame = GetFrame();
    if (!frame.IsValid())
        return {};

    std::string cppName = varName;

    if (source_) {
        // Определяем C++ файл из frame
        std::string cppFile;
        lldb::SBLineEntry lineEntry = frame.GetLineEntry();
        int cppLine = lineEntry.GetLine();
        if (lineEntry.GetFileSpec().IsValid()) {
            const char *name = lineEntry.GetFileSpec().GetFilename();
            if (name)
                cppFile = name;
        }
        // Получаем trust-контекст через обратную трансляцию cpp→trust
        auto trustContext = source_->nearestCppToTrust(cppFile, static_cast<trust::LineNumber>(cppLine));
        if (!trustContext.has_value())
            return {};
        // varName — trust-имя (от пользователя), транслируем в C++ имя по trust-контексту
        auto var = source_->getCppVar(trustContext->first, trustContext->second, varName);
        if (var.has_value())
            cppName = var->vars.second;
        else
            return {};  // переменная не найдена — нет значений
    }

    return frame.FindVariable(cppName.c_str());
}

bool TrustDebug::SetValueBySource(const std::string &varName, const std::string &value) {
    lldb::SBValue val = GetValueBySource(varName);
    if (!val.IsValid())
        return false;
    return val.SetValueFromCString(value.c_str());
}

void TrustDebug::SetSource(std::unique_ptr<const trust::TrustSource> source) {
    source_ = std::move(source);
}

const trust::TrustSource *TrustDebug::GetSource() const {
    return source_.get();
}
