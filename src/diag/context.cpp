#include "diag/context.hpp"
#include "utils/error.hpp"

#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

namespace trust {
namespace fs = std::filesystem;

namespace {
// Документирующий комментарий (`///`, `##`, `/**`) на строках непосредственно ПЕРЕД defBegin
// в том же файле. Возвращает склеенный текст строк (в естественном порядке) или "".
std::string precedingDocComment(const SourceMapWriter& src, MapperLocation defBegin) {
    if (defBegin.isInvalid()) {
        return "";
    }
    std::string_view text = src.source(defBegin.fileIdx());
    const auto lc = src.line_column(defBegin);
    const size_t defLine = lc.line; // 1-based номер строки определения

    // Разбиваем текст файла на строки (0-based индексы).
    std::vector<std::string_view> lines;
    {
        size_t pos = 0;
        while (pos <= text.size()) {
            const size_t nl = text.find('\n', pos);
            if (nl == std::string_view::npos) {
                lines.push_back(text.substr(pos));
                break;
            }
            lines.push_back(text.substr(pos, nl - pos));
            pos = nl + 1;
        }
    }
    auto isDocLine = [](std::string_view l) {
        size_t i = 0;
        while (i < l.size() && (l[i] == ' ' || l[i] == '\t')) {
            ++i;
        }
        return l.substr(i).starts_with("///") || l.substr(i).starts_with("##") || l.substr(i).starts_with("/**");
    };

    // Строка, идущая сразу перед defLine (1-based) = индекс defLine-2 (0-based).
    std::vector<std::string> collected;
    for (size_t li = defLine; li > 1; --li) {
        const size_t idx = li - 2;
        if (idx >= lines.size() || !isDocLine(lines[idx])) {
            break;
        }
        collected.push_back(std::string(lines[idx]));
    }
    std::string out;
    for (auto it = collected.rbegin(); it != collected.rend(); ++it) {
        if (!out.empty()) {
            out += "\n";
        }
        out += *it;
    }
    return out;
}

// Хвостовой inline-док (`///<`/`##<`) после конца определения (на той же строке).
// Возвращает комментарий целиком с маркером или "".
std::string trailingInlineDoc(const SourceMapWriter& src, MapperLocation defEnd) {
    if (defEnd.isInvalid()) {
        return "";
    }
    std::string_view text = src.source(defEnd.fileIdx());
    const size_t off = static_cast<size_t>(defEnd.offset() - 1); // 0-based
    if (off > text.size()) {
        return "";
    }
    const size_t nl = text.find('\n', off);
    const std::string_view rest = (nl == std::string_view::npos) ? text.substr(off) : text.substr(off, nl - off);
    size_t i = 0;
    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) {
        ++i;
    }
    if (rest.substr(i).starts_with("///<") || rest.substr(i).starts_with("##<")) {
        return std::string(rest.substr(i));
    }
    return "";
}

// Документирующий комментарий макроопределения: доки ПЕРЕД begin + хвостовой inline-док
// (`///<`/`##<`) после end. Источник — текст файла (макроопределения сворачиваются в
// макропроцессоре и AST-узла не имеют), привязан к определению макроса в точке записи.
std::string macroDocComment(const SourceMapWriter& src, MapperLocation begin, MapperLocation end) {
    std::string out = precedingDocComment(src, begin);
    const std::string trail = trailingInlineDoc(src, end);
    if (!trail.empty()) {
        if (!out.empty()) {
            out += "\n";
        }
        out += trail;
    }
    return out;
}
} // namespace

// ══════════════════════════════════════════════════════════════
//                         Context
// ══════════════════════════════════════════════════════════════

// ── Конструкторы ──

Context::Context()
: m_sourceMap(std::make_unique<SourceMapWriter>())
, m_diag(std::make_unique<DiagnosticEngine>()) {
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
    m_diag->setOptions(&*m_opts);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
#define FLAG_REG(name, cli) m_opts->register_flag(FlagKind::name);
    OPTIONS_FLAGS(FLAG_REG)
#undef FLAG_REG
    // Проверки `assert`/`verify` включены по умолчанию (безопасность по умолчанию);
    // отключаются через `-Wno-assert`.
    m_opts->set_enabled(FlagKind::Assert, true);
    // Стек вызовов при провале assert/verify печатается ПО УМОЛЧАНИЮ; отключение — через -Wno-backtrace.
    m_opts->set_enabled(FlagKind::Backtrace, true);
    // Комментарии в C++-выводе выводятся по умолчанию; подавление — через -Wno-comments
    // (флаг «comments» выключен = подавлять). См. OPTIONS_FLAGS(Comments).
    m_opts->set_enabled(FlagKind::Comments, true);
}

Context::Context(std::string_view basePath, std::string_view tempPath)
: m_sourceMap(std::make_unique<SourceMapWriter>(basePath, tempPath))
, m_diag(std::make_unique<DiagnosticEngine>()) {
    m_diag->setSourceManager(this);
    m_opts = Options(*m_diag);
    m_diag->setOptions(&*m_opts);
#define OPT_REG(name, str, sev) m_opts->add_option(OptKind::name);
    OPTIONS_LIST(OPT_REG)
#undef OPT_REG
#define FLAG_REG(name, cli) m_opts->register_flag(FlagKind::name);
    OPTIONS_FLAGS(FLAG_REG)
#undef FLAG_REG
    // Проверки `assert`/`verify` включены по умолчанию (безопасность по умолчанию);
    // отключаются через `-Wno-assert`.
    m_opts->set_enabled(FlagKind::Assert, true);
    // Стек вызовов при провале assert/verify печатается ПО УМОЛЧАНИЮ; отключение — через -Wno-backtrace.
    m_opts->set_enabled(FlagKind::Backtrace, true);
    // Комментарии в C++-выводе выводятся по умолчанию; подавление — через -Wno-comments
    // (флаг «comments» выключен = подавлять). См. OPTIONS_FLAGS(Comments).
    m_opts->set_enabled(FlagKind::Comments, true);
}

// ── Доступ к компонентам ──

SourceMapWriter& Context::source() {
    return *m_sourceMap;
}

const SourceMapWriter& Context::source() const {
    return *m_sourceMap;
}

DiagnosticEngine& Context::diag() {
    return *m_diag;
}

Options& Context::opts() {
    return *m_opts;
}

AttrPool& Context::attrs() {
    if (!m_attr_pool) {
        m_attr_pool = std::make_unique<AttrPool>();
    }
    return *m_attr_pool;
}

const DiagnosticEngine& Context::diag() const {
    return *m_diag;
}

const Options& Context::opts() const {
    return *m_opts;
}

TypeRegistry& Context::types() {
    EXPECT(m_type_registry);
    return *m_type_registry;
}

const TypeRegistry& Context::types() const {
    EXPECT(m_type_registry);
    return *m_type_registry;
}

const AttrPool& Context::attrs() const {
    EXPECT(m_attr_pool);
    return *m_attr_pool;
}

ModuleLoader& Context::loader() {
    EXPECT(m_moduleLoader);
    return *m_moduleLoader;
}

const ModuleLoader& Context::loader() const {
    EXPECT(m_moduleLoader);
    return *m_moduleLoader;
}

std::shared_ptr<Macro> Context::macro() const {
    return m_macro;
}

void Context::setMacro(std::shared_ptr<Macro> macro) {
    m_macro = std::move(macro);
}

void Context::recordMacro(std::string name, MapperRange range) {
    MacroDef md;
    md.name = std::move(name);
    md.range = range;
    // Документирующий комментарий макроопределения (ведущий + хвостовой inline) — для LSP-док.
    md.documentation = macroDocComment(*m_sourceMap, range.begin, range.end);
    m_macroDefs.push_back(std::move(md));
}

} // namespace trust
