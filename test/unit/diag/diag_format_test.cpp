// test/unit/diag/diag_format_test.cpp
// Сменный форматтер диагностик (--diagnostics-format): text по умолчанию, json/sarif —
// проекции ЕДИНОЙ записи DiagnosticEntry (см. include/diag/diag_formatter.hpp).
// Тесты — внутренние потребители: могут включать internal-заголовки diag/serialize.
#include "diag/diag.hpp"
#include "diag/diag_formatter.hpp"
#include "diag/serialize/json_formatter.hpp"
#include "diag/serialize/sarif_formatter.hpp"
#include "diag/serialize/text_formatter.hpp"
#include "session/context.hpp"

#include "utils/io/io.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace trust {
namespace {

// RAII-переключение глобального потока диагностик (utils/io/io.hpp: setErrs).
class ErrStreamGuard {
  public:
    explicit ErrStreamGuard(std::ostream& os)
    : m_prev(setErrs(&os)) {}
    ~ErrStreamGuard() { setErrs(m_prev); }

  private:
    std::ostream* m_prev;
};

TEST(DiagFormatTest, JsonFormatterWritesArrayWithoutLocation) {
    std::ostringstream oss;
    {
        JsonFormatter fmt(oss);
        DiagnosticEntry entry;
        entry.severity = Severity::Error;
        entry.optName = "sample-flag";
        entry.message = "sample message";
        fmt.write(entry, nullptr); // без провайдера локация недоступна -> null-поля
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("[\n"), std::string::npos) << out;
    EXPECT_NE(out.find("\"severity\": \"error\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"code\": \"sample-flag\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"message\": \"sample message\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"file\": null"), std::string::npos) << out;
    EXPECT_NE(out.find("\"line\": null"), std::string::npos) << out;
    EXPECT_NE(out.find("]\n"), std::string::npos) << out;
}

TEST(DiagFormatTest, JsonFormatterEscapesQuotesAndNewlines) {
    std::ostringstream oss;
    {
        JsonFormatter fmt(oss);
        DiagnosticEntry entry;
        entry.severity = Severity::Warning;
        entry.message = "say \"hi\"\nnext";
        fmt.write(entry, nullptr);
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("\"message\": \"say \\\"hi\\\"\\nnext\""), std::string::npos) << out;
}

TEST(DiagFormatTest, SarifFormatterWritesDocument) {
    std::ostringstream oss;
    {
        SarifFormatter fmt(ToolInfo{"TrustLang", "0.0.0-test"}, oss);
        DiagnosticEntry entry;
        entry.severity = Severity::Warning;
        entry.optName = "some-warn";
        entry.message = "warn message";
        fmt.write(entry, nullptr);
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("\"$schema\": \"https://json.schemastore.org/sarif-2.1.0.json\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"version\": \"2.1.0\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"name\": \"TrustLang\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"ruleId\": \"some-warn\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"level\": \"warning\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"message\": {\"text\": \"warn message\"}"), std::string::npos) << out;
}

TEST(DiagFormatTest, EngineDelegatesToJsonFormatter) {
    std::ostringstream oss;
    {
        ErrStreamGuard guard(oss);
        DiagnosticEngine engine;
        engine.setFormatter(makeDiagnosticFormatter(DiagnosticsFormat::Json));
        engine.report(Severity::Error, MapperRange{}, "engine message");
        // scope закрывается -> движок/форматтер разрушены -> массив JSON закрыт "]"
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("[\n"), std::string::npos) << out;
    EXPECT_NE(out.find("\"message\": \"engine message\""), std::string::npos) << out;
    EXPECT_NE(out.find("]\n"), std::string::npos) << out;
}

TEST(DiagFormatTest, EngineDefaultsToTextFormatter) {
    std::ostringstream oss;
    ErrStreamGuard guard(oss);
    DiagnosticEngine engine;
    engine.report(Severity::Error, MapperRange{}, "plain text message");
    EXPECT_EQ(oss.str(), "error: plain text message\n");
}

TEST(DiagFormatTest, DiagnosticsFormatFromName) {
    EXPECT_TRUE(diagnosticsFormatFromName("text").has_value());
    EXPECT_TRUE(diagnosticsFormatFromName("json").has_value());
    EXPECT_TRUE(diagnosticsFormatFromName("sarif").has_value());
    EXPECT_FALSE(diagnosticsFormatFromName("yaml").has_value());
}

TEST(DiagFormatTest, TextFormatterUsesInjectedStream) {
    std::ostringstream oss;
    TextFormatter fmt(oss);
    DiagnosticEntry entry;
    entry.severity = Severity::Error;
    entry.message = "plain text message";
    fmt.write(entry, nullptr); // без провайдера локация недоступна -> "error: message"
    EXPECT_EQ(oss.str(), "error: plain text message\n");
}

TEST(DiagFormatTest, TextFixitNoteOmitsCoordinatesWithoutLocation) {
    Context ctx;
    std::ostringstream oss;
    TextFormatter fmt(oss);
    const FixitSuggestion fixit{MapperRange{}, "x"}; // неразрешимый диапазон -> без координат
    fmt.writeFixitNote(fixit, &ctx);
    EXPECT_EQ(oss.str().find(":0:0"), std::string::npos) << oss.str();
    EXPECT_EQ(oss.str(), "note: fix-it: replace with 'x'\n");
}

TEST(DiagFormatTest, TextFixitNoteWithLocation) {
    Context ctx;
    MapperFile f = ctx.source().add_source("t.trust", "aaaa\n");
    const MapperLocation b = ctx.source().loc_from_line(f, 1);
    std::ostringstream oss;
    TextFormatter fmt(oss);
    fmt.writeFixitNote(FixitSuggestion{MapperRange::point(b), "x"}, &ctx);
    EXPECT_EQ(oss.str(), "t.trust:1:1: note: fix-it: replace with 'x'\n");
}

TEST(DiagFormatTest, SarifFormatterWritesAllFixits) {
    Context ctx;
    MapperFile f = ctx.source().add_source("t.trust", "aaaa\nbbbb\n");
    DiagnosticEntry entry;
    entry.severity = Severity::Error;
    entry.message = "m";
    entry.fixits.push_back(FixitSuggestion{{ctx.source().loc_from_line(f, 1), ctx.source().loc_from_line(f, 1)}, "first"});
    entry.fixits.push_back(FixitSuggestion{{ctx.source().loc_from_line(f, 2), ctx.source().loc_from_line(f, 2)}, "second"});
    std::ostringstream oss;
    {
        SarifFormatter fmt(ToolInfo{"TrustLang", "0.0.0-test"}, oss);
        fmt.write(entry, &ctx);
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("\"text\": \"first\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"text\": \"second\""), std::string::npos) << out;
    // Оба fixit — отдельные объекты fix с artifactChanges (по одному на подсказку).
    EXPECT_NE(out.find("\"artifactChanges\""), out.rfind("\"artifactChanges\"")) << out;
    EXPECT_NE(out.find("\"startLine\": 1"), std::string::npos) << out;
    EXPECT_NE(out.find("\"startLine\": 2"), std::string::npos) << out;
}

TEST(DiagFormatTest, SarifFixUsesDecodedRegion) {
    Context ctx;
    MapperFile f = ctx.source().add_source("t.trust", "aaaa\nbbbb\n");
    const MapperLocation b = ctx.source().loc_from_line(f, 2);
    DiagnosticEntry entry;
    entry.severity = Severity::Error;
    entry.message = "m";
    entry.fixits.push_back(FixitSuggestion{{b, b.inc(2)}, "zz"});
    std::ostringstream oss;
    {
        SarifFormatter fmt(ToolInfo{"TrustLang", "1"}, oss);
        fmt.write(entry, &ctx);
    }
    const std::string out = oss.str();
    EXPECT_NE(out.find("\"artifactChanges\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"deletedRegion\""), std::string::npos) << out;
    EXPECT_NE(out.find("\"startLine\": 2"), std::string::npos) << out;
    EXPECT_NE(out.find("\"insertedContent\": {\"text\": \"zz\"}"), std::string::npos) << out;
}

} // namespace
} // namespace trust
