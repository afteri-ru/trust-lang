// src/lsp/html_emit.cpp
// trust-lsp: генерация godbolt-стиля двухоконного playground (Trust | C++).
// In-process транспиляция Trust→C++ + построчный source-map → JSON/HTML.

#include "lsp/html_emit.h"
#include "lsp/lsp_options.hpp"

#include "diag/context.hpp"
#include "diag/mapper.hpp"
#include "pipeline/cli.hpp"
#include "pipeline/pipeline.hpp"
#include "pipeline/analysis_options.hpp"
#include "transpiler/transpiler.hpp"
#include "utils/error.hpp"
#include "utils/file_io.hpp"
#include "utils/io.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace trust::lsp {

using json = nlohmann::json;

// -- Экранирование --

std::string jsonEscape(const std::string& s) {
    std::string out = json(s).dump();
    // HTML/JS-safety: встроенный в <script> JSON не должен содержать сырую
    // последовательность "</script" (она завершит блок скрипта). Экранируем
    // '<' как "\u003c" (валидный JSON-escape, корректно декодируется в JS).
    constexpr const char* kFrom = "<";
    constexpr const char* kTo = "\\u003c";
    size_t pos = 0;
    while ((pos = out.find(kFrom, pos)) != std::string::npos) {
        out.replace(pos, 1, kTo);
        pos += 6;
    }
    return out;
}

// -- Список примеров (комбобокс playground) --

std::vector<LspExample> loadExamplesFromDir(const std::string& dir) {
    namespace fs = std::filesystem;
    std::vector<LspExample> out;
    if (dir.empty()) {
        return out;
    }
    std::error_code ec;
    if (!fs::is_directory(dir, ec) || ec) {
        return out;
    }
    std::vector<std::string> paths;
    for (auto it = fs::directory_iterator(dir, ec); it != fs::directory_iterator() && !ec; it.increment(ec)) {
        const auto& entry = *it;
        if (!entry.is_regular_file(ec) || ec) {
            continue;
        }
        if (entry.path().extension() != ".src") {
            continue;
        }
        paths.push_back(entry.path().string());
    }
    std::sort(paths.begin(), paths.end());
    for (const std::string& p : paths) {
        auto src = trust::utils::FileIO::read<std::string>(p);
        if (!src) {
            continue;
        }
        LspExample ex;
        ex.name = fs::path(p).stem().string();
        ex.source = std::move(*src);
        out.push_back(std::move(ex));
    }
    return out;
}

// -- Вспомогательные --

static size_t countLines(const std::string& s) {
    if (s.empty()) {
        return 0;
    }
    return static_cast<size_t>(std::count(s.begin(), s.end(), '\n')) + 1;
}

static void addUnique(std::vector<int>& v, int x) {
    if (std::find(v.begin(), v.end(), x) == v.end()) {
        v.push_back(x);
    }
}

// Проецирует forward (trust→cpp) и backward (cpp→trust) маппинги source-map
// на построчные массивы trust_to_cpp / cpp_to_trust (индексы 1-based).
static void buildLineMapping(const trust::SourceMapReader& reader, trust::ReaderFile trust_idx, trust::ReaderFile cpp_idx, HtmlResult& r) {
    const size_t t_lines = countLines(r.source);
    const size_t c_lines = countLines(r.cpp);
    r.trust_to_cpp.assign(t_lines + 1, {});
    r.cpp_to_trust.assign(c_lines + 1, {});

    auto line_of = [&](const trust::SourceMapReader::Location& loc) -> int { return reader.line_column(loc).line; };

    // forward: trust → cpp (m_forward: from=trust, to=cpp)
    for (const auto& [key, rm] : reader.getForwardMappings()) {
        (void)key;
        const auto& from = rm.from;
        const auto& to = rm.to;
        if (from.begin.isInvalid() || to.begin.isInvalid() || from.begin.fileIdx() != trust_idx) {
            continue;
        }
        const int tb = line_of(from.begin), te = line_of(from.end);
        const int cb = line_of(to.begin), ce = line_of(to.end);
        if (te < tb || ce < cb) {
            continue;
        }
        for (int L = tb; L <= te; ++L) {
            if (L < 1 || L >= static_cast<int>(r.trust_to_cpp.size())) {
                continue;
            }
            for (int C = cb; C <= ce; ++C) {
                if (C >= 1 && C < static_cast<int>(r.cpp_to_trust.size())) {
                    addUnique(r.trust_to_cpp[static_cast<size_t>(L)], C);
                }
            }
        }
    }

    // backward: cpp → trust (m_backward: from=cpp, to=trust)
    for (const auto& [key, rm] : reader.getBackwardMappings()) {
        (void)key;
        const auto& from = rm.from;
        const auto& to = rm.to;
        if (from.begin.isInvalid() || to.begin.isInvalid() || from.begin.fileIdx() != cpp_idx) {
            continue;
        }
        const int cb = line_of(from.begin), ce = line_of(from.end);
        const int tb = line_of(to.begin), te = line_of(to.end);
        if (ce < cb || te < tb) {
            continue;
        }
        for (int C = cb; C <= ce; ++C) {
            if (C < 1 || C >= static_cast<int>(r.cpp_to_trust.size())) {
                continue;
            }
            for (int L = tb; L <= te; ++L) {
                if (L >= 1 && L < static_cast<int>(r.trust_to_cpp.size())) {
                    addUnique(r.cpp_to_trust[static_cast<size_t>(C)], L);
                }
            }
        }
    }

    for (auto& v : r.trust_to_cpp) {
        std::sort(v.begin(), v.end());
    }
    for (auto& v : r.cpp_to_trust) {
        std::sort(v.begin(), v.end());
    }
}

// -- Транспиляция --

HtmlResult transpileToResult(const std::string& trust_code, const std::string& file_name, const LspOptions& opts) {
    HtmlResult r;
    r.source = trust_code;

    const std::string project_dir = opts.projectDir.empty() ? "." : opts.projectDir;
    auto ctx = std::make_unique<trust::Context>(project_dir);

    // Применяем опции анализа ПО ИСТОЧНИКУ (окружение + шебанг файла) по opts.shebangMode:
    // -W... и поведенческие флаги (--solver-mode, --keywords, -fsolver-loop-unroll), как в trust.
    // Ошибки опций печатаем в errs() (--json/--html - одноразовый CLI, без publishDiagnostics).
    {
        const std::vector<std::string> shebang = extractShebangOptions(trust_code);
        applyAnalysisArgsBySource(ctx->opts(), opts.pipelineArgs, shebang, opts.shebangMode, [](const std::string& msg, bool fromShebang) {
            trust::errs() << "error: invalid " << (fromShebang ? "shebang" : "environment") << " analysis options: " << msg << "\n";
        });
    }

    trust::PipelineOpts pipeline_opts{};
    pipeline_opts.input_file = file_name;

    try {
        trust::MapperFile trust_idx = ctx->source().add_source(file_name, std::string(trust_code));
        trust::MapperFile cpp_idx = ctx->source().add_output("playground.cppt");

        trust::Pipeline pipeline(*ctx, pipeline_opts);
        pipeline.runPipeline(trust::PipelineSteps::ParseAST | trust::PipelineSteps::Semantic | trust::PipelineSteps::Transpile, trust_idx, cpp_idx);

        const trust::SourceMapReader* reader = ctx->source().toReader();
        const trust::ReaderFile trust_reader_idx = trust::ReaderFile::from(trust_idx);
        const trust::ReaderFile cpp_reader_idx = trust::ReaderFile::from(cpp_idx);

        if (reader) {
            std::string_view cpp_view = reader->source(cpp_reader_idx);
            r.cpp.assign(cpp_view.data(), cpp_view.size());
            buildLineMapping(*reader, trust_reader_idx, cpp_reader_idx, r);
        }

        if (ctx->diag().errorCount() > 0) {
            r.ok = false;
            r.error = "transpilation completed with " + std::to_string(ctx->diag().errorCount()) + " error(s)";
        }
    } catch (const std::exception& e) {
        r.ok = false;
        r.error = std::string("transpilation failed: ") + e.what();
    }
    return r;
}

// -- JSON --

std::string resultToJson(const HtmlResult& r) {
    json j;
    j["source"] = r.source;
    j["cpp"] = r.cpp;
    j["ok"] = r.ok;
    j["error"] = r.error;
    j["trustToCpp"] = r.trust_to_cpp;
    j["cppToTrust"] = r.cpp_to_trust;
    return j.dump();
}

// -- HTML --
// Всё, что нужно для работы, встроено в выводимый HTML: стили, конфиг и
// glue-JS (Monarch-токенайзер Trust, инициализация двух редакторов Monaco,
// синхронная навигация, debounced живая пере-транспиляция). Внешней остаётся
// только сама библиотека Monaco (monaco_url) - она слишком велика для встраивания.

static const unsigned char kMonarchTrustBytes[] = {
#embed "trust.monarch.js"
};
static const std::string kMonarchTrust(reinterpret_cast<const char*>(kMonarchTrustBytes), sizeof(kMonarchTrustBytes));

static const unsigned char kGlueJsBytes[] = {
#embed "glue.js"
};
static const std::string kGlueJs(reinterpret_cast<const char*>(kGlueJsBytes), sizeof(kGlueJsBytes));

// CSS-правила, HTML-каркас и полная страница вынесены во внешние файлы (#embed),
// чтобы редактировать их без C++-экранирования и тестировать независимо.
static const unsigned char kPlaygroundCssBytes[] = {
#embed "playground.css"
};
static const std::string kPlaygroundCss(reinterpret_cast<const char*>(kPlaygroundCssBytes), sizeof(kPlaygroundCssBytes));

static const unsigned char kPlaygroundHtmlBytes[] = {
#embed "playground.html"
};
static const std::string kPlaygroundHtml(reinterpret_cast<const char*>(kPlaygroundHtmlBytes), sizeof(kPlaygroundHtmlBytes));

// Полная HTML-страница; единственный плейсхолдер %%BODY%% заменяется фрагментом.
static const unsigned char kPlaygroundPageBytes[] = {
#embed "playground_page.html"
};
static const std::string kPlaygroundPage(reinterpret_cast<const char*>(kPlaygroundPageBytes), sizeof(kPlaygroundPageBytes));

static std::string buildConfigJson(const HtmlResult& r, const std::string& monaco_url, const std::string& server_url, const std::vector<LspExample>& examples) {
    std::string cfg = "{";
    cfg += "\"monacoUrl\":" + jsonEscape(monaco_url) + ",";
    cfg += "\"serverUrl\":" + jsonEscape(server_url) + ",";
    // Трансляция (cpp/маппинги/ok/error) НЕ хранится в шаблоне страницы -
    // она получается только от балансировщика при каждом изменении кода.
    cfg += "\"source\":" + jsonEscape(r.source);
    cfg += ",\"examples\":[";
    for (size_t i = 0; i < examples.size(); ++i) {
        if (i != 0) {
            cfg += ",";
        }
        cfg += "{\"name\":" + jsonEscape(examples[i].name) + ",";
        cfg += "\"source\":" + jsonEscape(examples[i].source) + "}";
    }
    cfg += "]";
    cfg += "}";
    return cfg;
}

static std::string buildFragment(const HtmlResult& r, const LspOptions& opts, const std::string& monaco_url, const std::string& server_url) {
    std::string out;
    out.reserve(r.source.size() + r.cpp.size() + 8192);

    out += "<style>\n";
    out += kPlaygroundCss;
    out += "</style>\n";

    out += kPlaygroundHtml;

    out += "<script>\n";
    out += "window.__TPG = window.__TPG || {};\n";
    out += "window.__TPG.config = " + buildConfigJson(r, monaco_url, server_url, opts.examples) + ";\n";
    out += "window.__TPG.monarch = function () { return (" + std::string(kMonarchTrust) + "); };\n";
    out += "window.__TPG.glue = function (m) { var __MONARCH__ = m; " + std::string(kGlueJs) + " };\n";
    out += "if (window.__TPG.monarch && window.__TPG.glue) { window.__TPG.glue(window.__TPG.monarch()); }\n";
    out += "</script>\n";

    return out;
}

std::string resultToHtml(const HtmlResult& r, const LspOptions& opts, const std::string& monaco_url, const std::string& server_url, bool full_page) {
    const std::string fragment = buildFragment(r, opts, monaco_url, server_url);
    if (!full_page) {
        return fragment;
    }

    std::string page = kPlaygroundPage;
    // Единственный плейсхолдер шаблона полной страницы - %%BODY%% (вставляется фрагмент).
    // Если плейсхолдер отсутствует (шаблон испорчен) - это ошибка разработчика, а не
    // тихий fallback: бросаем диагностику вместо бесшумного дописывания фрагмента.
    constexpr std::string_view kBodyPlaceholder = "%%BODY%%";
    const size_t pos = page.find(kBodyPlaceholder);
    EXPECT(pos != std::string::npos && "playground page template must contain %%BODY%% placeholder");
    page.replace(pos, kBodyPlaceholder.size(), fragment);
    return page;
}

} // namespace trust::lsp
