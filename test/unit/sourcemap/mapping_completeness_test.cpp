// test/unit/sourcemap/mapping_completeness_test.cpp
// R40 (полнота): у КАЖДОГО сохранённого выхода есть записи mapping (проекция
// «входной диапазон → целевой диапазон»). Проверяется на нескольких программах:
// сохранённый .cppt/.src_map всегда несёт непустую карту.
// Запретительная часть (сохранение при подавленном маппинге) — EXPECT в `save_output`.
#include "transpiler/transpile_lsp_test_util.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace trust;

namespace {

void expectSavedOutputHasMappings(const std::string& program, const std::string& name) {
    Context ctx(".");
    TypeSystem types(ctx.diag(), ctx.opts());
    ctx.setTypes(&types);
    TargetContract target;
    ctx.setTarget(&target);
    MapperFile cppIdx;
    transpileForTest(types, ctx, program, &cppIdx);
    ASSERT_EQ(ctx.diag().errorCount(), 0);
    // saveAndCheckDisk сохраняет .cppt + .src_map и утверждает непустую карту (R40).
    saveAndCheckDisk(ctx, cppIdx, name);
}

} // namespace

TEST(SourceMapCompleteness, SavedOutputAlwaysHasMappings) {
    expectSavedOutputHasMappings("x := 41;\n", "CompletenessVarDecl");
    expectSavedOutputHasMappings("{% int y = 7; %}\n", "CompletenessEmbed");
}
