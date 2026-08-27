#ifndef TRUST_FORMAT_TEST_UTIL_HPP
#define TRUST_FORMAT_TEST_UTIL_HPP
// Shared helper for formatter unit tests (see format_test.cpp / format_*_test.cpp).
// Runs the formatter through the real pipeline and expects success.
#include "formatter/format.hpp"

#include "diag/context.hpp"
#include "pipeline/pipeline.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace trust::formatter {

// Утилита: отформатировать и ожидать успех. Как pipeline/LSP — создаёт Context, загружает DSL
// (keywords + макросы), создаёт Parser и прогоняет через форматтер (подписывается на коллбеки).
inline std::string fmt(std::string_view src, const FormatOptions& opts = {}) {
    FormatOptions fopts = opts;
    auto ctx = std::make_shared<trust::Context>(".");
    trust::PipelineOpts popts;
    popts.input_file = "@test";
    trust::Pipeline pipeline(*ctx, popts);
    pipeline.effectiveKeywords(); // загружает DSL (keywords + макросы) в ctx
    if (fopts.keywords.empty()) {
        if (auto v = ctx->opts().flagValueByName("keywords"); v && !v->empty()) {
            fopts.keywords = trust::formatter::splitKeywordList(*v);
        }
    }
    ctx->diag().setMinSeverity(trust::Severity::Fatal);
    trust::Parser parser(*ctx);
    FormatResult r = trust::formatter::format(src, "@test", fopts, parser);
    EXPECT_TRUE(r.ok) << "formatter error: " << r.error;
    return r.text;
}

} // namespace trust::formatter
#endif // TRUST_FORMAT_TEST_UTIL_HPP
