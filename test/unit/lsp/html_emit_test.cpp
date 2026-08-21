// -----------------------------------------------------------------------
// Unit tests for trust-lsp playground HTML/JSON output (html_emit).
// -----------------------------------------------------------------------

#include "lsp/html_emit.h"
#include "lsp/lsp_protocol.h"
#include "utils/file_io.hpp"

#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>
#include <vector>

using json = nlohmann::json;

namespace {

class HtmlEmitTest : public ::testing::Test {
  protected:
    LspOptions opts;

    static std::vector<std::string> splitLines(const std::string& s) {
        std::vector<std::string> out;
        std::string cur;
        for (char c : s) {
            if (c == '\n') {
                out.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        if (!cur.empty()) {
            out.push_back(cur);
        }
        return out;
    }
};

constexpr const char* kHelloSrc = "@main():={\n"
                                  "    msg := \"hello world\";\n"
                                  "    print(msg);\n"
                                  "}\n";

TEST_F(HtmlEmitTest, JsonResult_HasSourceCppAndLineMapping) {
    auto r = trust::lsp::transpileToResult(kHelloSrc, "hello.src", opts);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.source, kHelloSrc);
    ASSERT_FALSE(r.cpp.empty()) << "generated C++ must not be empty";

    // Построчный маппинг непуст: строка 2 (msg := ...) → строки C++, и обратно.
    ASSERT_GT(r.trust_to_cpp.size(), static_cast<size_t>(2));
    EXPECT_FALSE(r.trust_to_cpp[2].empty()) << "msg line must map to a cpp line";
    EXPECT_FALSE(r.cpp_to_trust.empty());

    // Строка печати (3) мапится на строку C++, содержащую вызов print.
    auto cpp_lines = splitLines(r.cpp);
    bool found_print = false;
    for (int c : r.trust_to_cpp[3]) {
        if (c >= 1 && c <= static_cast<int>(cpp_lines.size()) && cpp_lines[static_cast<size_t>(c - 1)].find("print") != std::string::npos) {
            found_print = true;
        }
    }
    EXPECT_TRUE(found_print) << "trust print line must map to a cpp print line";
}

TEST_F(HtmlEmitTest, JsonResult_IsValidJsonAndRoundTrips) {
    const std::string kSrc = "@main():={\n    s := \"a<>&'\\\"q\";\n    print(s);\n}\n";
    auto r = trust::lsp::transpileToResult(kSrc, "esc.src", opts);
    std::string json_str = trust::lsp::resultToJson(r);
    json j = json::parse(json_str);
    EXPECT_EQ(j["source"], kSrc);
    EXPECT_EQ(j["ok"], true);
    EXPECT_TRUE(j.contains("cpp"));
    EXPECT_TRUE(j.contains("trustToCpp"));
    EXPECT_TRUE(j.contains("cppToTrust"));
}

TEST_F(HtmlEmitTest, HtmlFragment_ContainsMonarchInitEditorsAndConfig) {
    auto r = trust::lsp::transpileToResult(kHelloSrc, "hello.src", opts);
    std::string html = trust::lsp::resultToHtml(r, opts);

    // Два окна редактора (godbolt-стиль).
    EXPECT_NE(html.find("tpl-trust-editor"), std::string::npos);
    EXPECT_NE(html.find("tpl-cpp-editor"), std::string::npos);
    // Встроенный Monarch-токенайзер Trust (подсветка).
    EXPECT_NE(html.find("setMonarchTokensProvider"), std::string::npos);
    // Встроенные JS-функции навигации и конфиг (source/cpp/mapping).
    EXPECT_NE(html.find("window.__TPG.config"), std::string::npos);
    EXPECT_NE(html.find("window.__TPG.glue"), std::string::npos);
    EXPECT_NE(html.find("window.__TPG.monarch"), std::string::npos);
    // Исходник примера встроен в конфиг (JSON-экранирован).
    EXPECT_NE(html.find("hello world"), std::string::npos);
    // Трансляция НЕ хранится в шаблоне страницы: в конфиге нет поля "cpp".
    EXPECT_EQ(html.find("\"cpp\":"), std::string::npos) << "cpp must not be embedded in config";
    // Оверлей для центрированного сообщения об ошибке/нет связи присутствует.
    EXPECT_NE(html.find("tpl-cpp-overlay"), std::string::npos);
    // glue-JS содержит обработку ошибок связи (очистка панели + сообщение).
    EXPECT_NE(html.find("resetCppPane"), std::string::npos);
    EXPECT_NE(html.find("Нет связи с балансировщиком"), std::string::npos);
    // Индикатор связи песочницы с балансировщиком (публичный пинг /health).
    EXPECT_NE(html.find("tpl-health"), std::string::npos);
    EXPECT_NE(html.find("updateHealth"), std::string::npos);
    // Кросс-оконная навигация: обработчики по позиции курсора (клик и стрелки).
    EXPECT_NE(html.find("onDidChangeCursorPosition"), std::string::npos);
    EXPECT_NE(html.find("deltaDecorations"), std::string::npos);
    // Изменяемый размер окон: вертикальный сплиттер Trust|C++ и горизонтальный над логом.
    // id/class сплиттеров ДОЛЖНЫ быть обычными кавычками без обратных слешей: иначе
    // getElementById('tpl-split-v'/'tpl-split-h') вернёт null и ресайз не будет работать,
    // а в теле страницы останется видимый артефакт "\n"/слеш (двойное экранирование
    // \\n / \\" в html_emit.cpp).
    EXPECT_NE(html.find("id=\"tpl-split-v\""), std::string::npos);
    EXPECT_NE(html.find("id=\"tpl-split-h\""), std::string::npos);
    EXPECT_NE(html.find("class=\"tpl-splitter-v\""), std::string::npos);
    EXPECT_NE(html.find("class=\"tpl-splitter-h\""), std::string::npos);
    // Никаких обратных слешей перед кавычками в разметке сплиттеров.
    EXPECT_EQ(html.find("id=\\\"tpl-split-v"), std::string::npos);
    EXPECT_EQ(html.find("id=\\\"tpl-split-h"), std::string::npos);
    // В CSS после правила сплиттера должен идти РЕАЛЬНЫЙ перевод строки, а не текст \\n.
    EXPECT_NE(html.find(".tpl-splitter-v{width:6px;cursor:col-resize;flex:none;background:var(--tpl-toolbar);user-select:none;}\n.tpl-splitter-v:hover{"
                        "background:var(--tpl-border);}"),
              std::string::npos);
    EXPECT_NE(html.find(".tpl-splitter-h{height:6px;cursor:row-resize;flex:none;background:var(--tpl-toolbar);user-select:none;}\n.tpl-splitter-h:hover{"
                        "background:var(--tpl-border);}"),
              std::string::npos);
    // glue-JS содержит drag-обработчики сплиттеров (makeSplitter).
    EXPECT_NE(html.find("makeSplitter"), std::string::npos);
}

TEST_F(HtmlEmitTest, HtmlFragment_EmbedsLogNavigation) {
    auto r = trust::lsp::transpileToResult(kHelloSrc, "hello.src", opts);
    std::string html = trust::lsp::resultToHtml(r, opts);
    // glue-JS разбирает заголовки диагностик (формат file:line:col: severity: msg)
    // и делает их кликабельными (переход на строку в исходнике); оверлей строится
    // через textContent (без innerHTML - строки от сервера не исполняются как HTML).
    EXPECT_NE(html.find("gotoTrustLine"), std::string::npos);
    EXPECT_NE(html.find("tpl-log-link"), std::string::npos);
    EXPECT_NE(html.find("tpl-logline"), std::string::npos);
    EXPECT_NE(html.find("tpl-log-error"), std::string::npos);
    EXPECT_NE(html.find("cppOverlay.textContent"), std::string::npos);
    EXPECT_EQ(html.find("cppOverlay.innerHTML"), std::string::npos);
}

TEST_F(HtmlEmitTest, HtmlFullPage_WrapsDocument) {
    auto r = trust::lsp::transpileToResult(kHelloSrc, "hello.src", opts);
    std::string html = trust::lsp::resultToHtml(r, opts, trust::lsp::kDefaultMonacoUrl, "", true);
    EXPECT_NE(html.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(html.find("<html"), std::string::npos);
    EXPECT_NE(html.find("</html>"), std::string::npos);
    // Полная страница содержит фрагмент.
    EXPECT_NE(html.find("tpl-trust-editor"), std::string::npos);
}

TEST_F(HtmlEmitTest, HtmlFragment_HasExamplesCombobox) {
    auto r = trust::lsp::transpileToResult(kHelloSrc, "hello.src", opts);
    ASSERT_TRUE(r.ok) << r.error;

    LspExample ex1{"hello", kHelloSrc};
    LspExample ex2{"fib", "@main():={\n    print(\"hi<&>\");\n}\n"};
    opts.examples = {ex1, ex2};
    std::string html = trust::lsp::resultToHtml(r, opts);

    // Комбобокс примера всегда присутствует в тулбаре Trust.
    EXPECT_NE(html.find("id=\"tpl-examples\""), std::string::npos);
    // Статический блок списка примеров встроен в конфиг.
    EXPECT_NE(html.find("\"examples\":["), std::string::npos);
    EXPECT_NE(html.find("\"name\":\"hello\""), std::string::npos);
    EXPECT_NE(html.find("\"name\":\"fib\""), std::string::npos);
    // source примеров экранирован (HTML-safety): '<' -> \u003c (а '>' не трогается).
    EXPECT_NE(html.find("hi\\u003c&>"), std::string::npos);
    // glue-JS содержит логику подтверждения замены текста.
    EXPECT_NE(html.find("confirm("), std::string::npos);
}

TEST_F(HtmlEmitTest, HtmlFragment_ExamplesArrayEmptyWhenNoExamples) {
    auto r = trust::lsp::transpileToResult(kHelloSrc, "hello.src", opts);
    ASSERT_TRUE(r.ok) << r.error;
    opts.examples = {};
    std::string html = trust::lsp::resultToHtml(r, opts);
    // Комбобокс есть, но список примеров пуст.
    EXPECT_NE(html.find("id=\"tpl-examples\""), std::string::npos);
    EXPECT_NE(html.find("\"examples\":[]"), std::string::npos);
}

TEST_F(HtmlEmitTest, LoadExamplesFromDir_ReadsSortedSrcFiles) {
    namespace fs = std::filesystem;
    const std::string dir = (fs::temp_directory_path() / ("tpl_ex_io_" + std::to_string(::getpid()))).string();
    fs::create_directory(dir);
    ASSERT_TRUE(trust::utils::FileIO::write(dir + "/b.src", std::string("second")));
    ASSERT_TRUE(trust::utils::FileIO::write(dir + "/a.src", std::string("first")));
    ASSERT_TRUE(trust::utils::FileIO::write(dir + "/note.txt", std::string("ignored")));

    auto ex = trust::lsp::loadExamplesFromDir(dir);
    ASSERT_EQ(ex.size(), static_cast<size_t>(2));
    EXPECT_EQ(ex[0].name, "a");
    EXPECT_EQ(ex[0].source, "first");
    EXPECT_EQ(ex[1].name, "b");
    EXPECT_EQ(ex[1].source, "second");

    fs::remove_all(dir);
}

TEST_F(HtmlEmitTest, JsonEscape_ProducesValidJsonString) {
    const std::string kIn = "a\"b\\c\nd\t<&> 'x'";
    std::string esc = trust::lsp::jsonEscape(kIn);
    // jsonEscape возвращает полный строковый литерал вида "…", который сам является валидным JSON.
    json full = json::parse(esc);
    EXPECT_EQ(full.get<std::string>(), kIn);
    // HTML/JS-safety: '<' экранируется как \u003c, чтобы "</script" не завершил блок.
    EXPECT_NE(esc.find("\\u003c"), std::string::npos);
    EXPECT_EQ(esc.find("</"), std::string::npos) << "raw '</' must be escaped";
}

} // namespace
