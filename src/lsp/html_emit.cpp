// src/lsp/html_emit.cpp
// trust-lsp: генерация godbolt-стиля двухоконного playground (Trust | C++).
// In-process транспиляция Trust→C++ + построчный source-map → JSON/HTML.

#include "lsp/html_emit.h"

#include "diag/context.hpp"
#include "diag/mapper.hpp"
#include "pipeline/pipeline.hpp"
#include "transpiler/transpiler.hpp"
#include "utils/file_io.hpp"

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

// ── Экранирование ──

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

// ── Список примеров (комбобокс playground) ──

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

// ── Вспомогательные ──

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

// ── Транспиляция ──

HtmlResult transpileToResult(const std::string& trust_code, const std::string& file_name, const LspOptions& opts) {
    HtmlResult r;
    r.source = trust_code;

    const std::string project_dir = opts.projectDir.empty() ? "." : opts.projectDir;
    auto ctx = std::make_unique<trust::Context>(project_dir);

    // Пробрасываем доп. опции (-W<name>=<status>) в pipeline (диагностику).
    if (!opts.pipelineArgs.empty()) {
        std::vector<char*> argv;
        for (const std::string& s : opts.pipelineArgs) {
            argv.push_back(const_cast<char*>(s.c_str()));
        }
        ctx->opts().parse_argv(argv);
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

// ── JSON ──

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

// ── HTML ──
// Всё, что нужно для работы, встроено в выводимый HTML: стили, конфиг и
// glue-JS (Monarch-токенайзер Trust, инициализация двух редакторов Monaco,
// синхронная навигация, debounced живая пере-транспиляция). Внешней остаётся
// только сама библиотека Monaco (monaco_url) — она слишком велика для встраивания.

static const unsigned char kMonarchTrustBytes[] = {
#embed "trust.monarch.js"
};
static const std::string kMonarchTrust(reinterpret_cast<const char*>(kMonarchTrustBytes), sizeof(kMonarchTrustBytes));

static const unsigned char kGlueJsBytes[] = {
#embed "glue.js"
};
static const std::string kGlueJs(reinterpret_cast<const char*>(kGlueJsBytes), sizeof(kGlueJsBytes));

static std::string buildConfigJson(const HtmlResult& r, const std::string& monaco_url, const std::string& server_url, const std::vector<LspExample>& examples) {
    std::string cfg = "{";
    cfg += "\"monacoUrl\":" + jsonEscape(monaco_url) + ",";
    cfg += "\"serverUrl\":" + jsonEscape(server_url) + ",";
    // Трансляция (cpp/маппинги/ok/error) НЕ хранится в шаблоне страницы —
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
    // Светлые дефолты; сайт может переопределить --tpl-* в своём CSS (общая тема).
    out += ".tpl-pg{--tpl-bg:#ffffff;--tpl-text:#24292f;--tpl-gutter:#6b7280;"
           "--tpl-border:#d1d5db;--tpl-toolbar:#f6f8fa;--tpl-linked:#fff3bf;"
           "--tpl-error:#dc2626;display:flex;flex-direction:column;gap:6px;"
           "font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;"
           "background:var(--tpl-bg);color:var(--tpl-text);padding:8px;"
           "border:1px solid var(--tpl-border);border-radius:6px;}\n";
    out += ".tpl-row{display:flex;gap:6px;min-height:400px;}\n";
    out += ".tpl-pane{flex:1;display:flex;flex-direction:column;min-width:0;position:relative;border:1px solid "
           "var(--tpl-border);border-radius:4px;overflow:hidden;}\n";
    out += ".tpl-toolbar{padding:4px "
           "8px;background:var(--tpl-toolbar);font-size:12px;font-weight:600;user-select:none;display:flex;align-items:center;gap:8px;}\n";
    out += ".tpl-examples{font-size:12px;font-weight:400;max-width:260px;background:var(--tpl-bg);color:var(--tpl-text);border:1px solid "
           "var(--tpl-border);border-radius:3px;}\n";
    out += ".tpl-editor{flex:1;min-height:380px;}\n";
    out += ".tpl-status{min-height:1.2em;font-size:12px;color:var(--tpl-gutter);white-space:pre-wrap;}\n";
    out += ".tpl-status.tpl-error{color:var(--tpl-error);}\n";
    // Индикатор связи песочницы с балансировщиком (публичный пинг /health): онлайн/деградация/нет связи.
    out += ".tpl-health{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:600;color:var(--tpl-text);white-space:nowrap;}\n";
    out += ".tpl-health .dot{width:9px;height:9px;border-radius:50%;background:var(--tpl-gutter);flex:none;}\n";
    out += ".tpl-health.ok .dot{background:#2e7d32;}\n";
    out += ".tpl-health.degraded .dot{background:#ef6c00;}\n";
    out += ".tpl-health.down .dot{background:var(--tpl-error);}\n";
    out += ".tpl-linked{background:var(--tpl-linked);}\n";
    out += ".tpl-gutter{box-shadow:inset 3px 0 0 var(--tpl-text);opacity:.55;}\n";
    out += ".tpl-follow{font-size:12px;font-weight:400;display:flex;align-items:center;gap:4px;margin-left:auto;cursor:pointer;user-select:none;}\n";
    out += ".tpl-follow input{accent-color:var(--tpl-text);cursor:pointer;}\n";
    out += ".tpl-btn{font-size:12px;padding:3px 10px;background:var(--tpl-toolbar);color:var(--tpl-text);border:1px solid "
           "var(--tpl-border);border-radius:4px;cursor:pointer;text-decoration:none;font-weight:600;}\n";
    out += ".tpl-btn-disabled{opacity:.6;pointer-events:none;}\n";
    out += ".tpl-log{min-height:80px;max-height:160px;overflow:auto;font-size:12px;color:var(--tpl-text);background:var(--tpl-bg);border:1px solid "
           "var(--tpl-border);border-radius:4px;padding:6px;white-space:pre-wrap;}\n";
    // Оверлей правой панели: центрированное сообщение об ошибке/нет связи с
    // сервером песочницы. По умолчанию скрыт (display:none), включается из glue-JS.
    out += ".tpl-overlay{position:absolute;top:0;left:0;right:0;bottom:0;display:none;align-items:center;justify-content:center;"
           "padding:16px;text-align:center;background:var(--tpl-bg);color:var(--tpl-error);font-size:14px;line-height:1.5;z-index:10;}\n";
    out += "</style>\n";

    out += "<div class=\"tpl-pg\" id=\"trust-playground\">\n";
    out += "<div class=\"tpl-row\">\n";
    out += "  <div class=\"tpl-pane\"><div class=\"tpl-toolbar\">Trust"
           "<select id=\"tpl-examples\" class=\"tpl-examples\" title=\"Load example\"></select></div>"
           "<div id=\"tpl-trust-editor\" class=\"tpl-editor\"></div></div>\n";
    out += "  <div class=\"tpl-pane\"><div class=\"tpl-toolbar\">Generated C++"
           "<a id=\"tpl-download\" class=\"tpl-btn tpl-btn-disabled\" href=\"#\" title=\"Скачать архив сборки\">&#11015; Скачать архив</a>"
           "<label id=\"tpl-follow\" class=\"tpl-follow\" title=\"Следовать за выбранной строкой (прокручивать вторую панель к ней)\">"
           "<input type=\"checkbox\" id=\"tpl-follow-cb\" checked>follow</label></div>"
           "<div id=\"tpl-cpp-editor\" class=\"tpl-editor\"></div>"
           "<div id=\"tpl-cpp-overlay\" class=\"tpl-overlay\"></div></div>\n";
    out += "</div>\n";
    out += "<div id=\"tpl-log\" class=\"tpl-log\"></div>\n";
    out += "<div class=\"tpl-toolbar\" style=\"border-top:1px solid var(--tpl-border);\">"
           "<span id=\"tpl-health\" class=\"tpl-health\"><span class=\"dot\"></span><span id=\"tpl-health-text\">…</span></span></div>\n";
    out += "<div id=\"tpl-status\" class=\"tpl-status\"></div>\n";
    out += "</div>\n";

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

    std::string page;
    page += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n";
    page += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    page += "<title>Trust Playground</title>\n";
    page += "<style>html,body{margin:0;padding:0;background:#1e1e1e;}body{display:flex;min-height:100vh;}</style>\n";
    page += "</head>\n<body style=\"width:100%;\">\n";
    page += fragment;
    page += "\n</body>\n</html>\n";
    return page;
}

} // namespace trust::lsp
