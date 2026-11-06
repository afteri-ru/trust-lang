// test/unit/diag/one_entry_all_formats_test.cpp
// R31: единая запись DiagnosticEntry — единственный источник для text/json/sarif.
// Каждый форматтер проецирует ОДНУ и ту же запись: сообщение, severity и код совпадают
// во всех форматах (второй модели данных нет).
#include "diag/diag.hpp"
#include "diag/diag_formatter.hpp"
#include "diag/serialize/json_formatter.hpp"
#include "diag/serialize/sarif_formatter.hpp"
#include "diag/serialize/text_formatter.hpp"
#include "session/context.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace trust {
namespace {

TEST(DiagOneEntryAllFormats, AllFormatsProjectSameRecord) {
    Context ctx;
    const MapperFile f = ctx.source().add_source("t.trust", "aaaa\n");

    DiagnosticEntry entry;
    entry.severity = Severity::Warning;
    entry.optName = "some-warn";
    entry.message = "shared message";
    entry.range = {ctx.source().loc_from_line(f, 1), ctx.source().loc_from_line(f, 1)};

    std::ostringstream text;
    std::ostringstream json;
    std::ostringstream sarif;
    {
        TextFormatter fmt(text);
        fmt.write(entry, &ctx);
    }
    {
        JsonFormatter fmt(json);
        fmt.write(entry, &ctx);
    }
    {
        SarifFormatter fmt(ToolInfo{"TrustLang", "test"}, sarif);
        fmt.write(entry, &ctx);
    }

    const std::string textOut = text.str();
    const std::string jsonOut = json.str();
    const std::string sarifOut = sarif.str();

    // text: единая запись с локацией, severity и сообщением (первая строка).
    EXPECT_EQ(textOut.rfind("t.trust:1:1: warning: shared message", 0), 0u) << textOut;
    // json: те же severity/код/сообщение.
    EXPECT_NE(jsonOut.find("\"severity\": \"warning\""), std::string::npos) << jsonOut;
    EXPECT_NE(jsonOut.find("\"code\": \"some-warn\""), std::string::npos) << jsonOut;
    EXPECT_NE(jsonOut.find("\"message\": \"shared message\""), std::string::npos) << jsonOut;
    // sarif: те же severity/код/сообщение.
    EXPECT_NE(sarifOut.find("\"ruleId\": \"some-warn\""), std::string::npos) << sarifOut;
    EXPECT_NE(sarifOut.find("\"level\": \"warning\""), std::string::npos) << sarifOut;
    EXPECT_NE(sarifOut.find("\"text\": \"shared message\""), std::string::npos) << sarifOut;

    // Ни один формат не добавляет собственного сообщения: текст записи один и тот же.
    EXPECT_EQ(jsonOut.find("shared message") == std::string::npos, false);
}

} // namespace
} // namespace trust
