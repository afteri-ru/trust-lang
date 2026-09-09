// Тесты дампа состояния скоупа анализатора (include/semantic/debug_scope.hpp):
//   - разбор именованных опций (parseScopeDumpArgs) и FAULT на невалидных опциях (scopeDumpOptsFrom);
//   - форматирование (formatScopeStack): depth/names, level=current|all, маска имён, max,
//     types=on, count=only.

#include "semantic/debug_scope.hpp"

#include "ast/ast_nodes.hpp"
#include "analysis/symbol_table.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

using trust::debug::formatScopeStack;
using trust::debug::parseScopeDumpArgs;
using trust::debug::ScopeDumpOpts;

// -- parseScopeDumpArgs (именованные опции, каждая - отдельный аргумент) --

TEST(ScopeDumpOpts, DefaultsForNoOptions) {
    ScopeDumpOpts opts;
    std::string err;
    ASSERT_TRUE(parseScopeDumpArgs({}, opts, err));
    EXPECT_TRUE(err.empty());
    EXPECT_TRUE(opts.current_only);
    EXPECT_TRUE(opts.show_names);
    EXPECT_FALSE(opts.show_types);
    EXPECT_EQ(opts.max_names, 20u);
    EXPECT_TRUE(opts.name_mask.empty());
    EXPECT_FALSE(opts.count_only);
}

TEST(ScopeDumpOpts, ParsesAllNamedOptions) {
    ScopeDumpOpts opts;
    std::string err;
    ASSERT_TRUE(parseScopeDumpArgs({"level=all", "types=on", "max=10", "count=only"}, opts, err)) << err;
    EXPECT_FALSE(opts.current_only);
    EXPECT_TRUE(opts.show_types);
    EXPECT_EQ(opts.max_names, 10u);
    EXPECT_TRUE(opts.count_only);
}

TEST(ScopeDumpOpts, NamesIsNotAnOptionAnymore) {
    // Фильтр имён задаётся ТОЛЬКО позиционными масками, отдельной опции names больше нет.
    ScopeDumpOpts opts;
    std::string err;
    EXPECT_FALSE(parseScopeDumpArgs({"names=x*"}, opts, err));
    EXPECT_NE(err.find("unknown option"), std::string::npos);
}

TEST(ScopeDumpOpts, RejectsUnknownOptionName) {
    ScopeDumpOpts opts;
    std::string err;
    EXPECT_FALSE(parseScopeDumpArgs({"bogus=1"}, opts, err));
    EXPECT_NE(err.find("unknown option"), std::string::npos);
}

TEST(ScopeDumpOpts, RejectsPositionalArgument) {
    ScopeDumpOpts opts;
    std::string err;
    EXPECT_FALSE(parseScopeDumpArgs({"scope"}, opts, err));
    EXPECT_NE(err.find("key=value"), std::string::npos);
}

TEST(ScopeDumpOpts, RejectsBadValues) {
    ScopeDumpOpts opts;
    std::string err;
    EXPECT_FALSE(parseScopeDumpArgs({"level=deep"}, opts, err));
    EXPECT_FALSE(parseScopeDumpArgs({"types=maybe"}, opts, err));
    EXPECT_FALSE(parseScopeDumpArgs({"max=abc"}, opts, err));
    EXPECT_FALSE(parseScopeDumpArgs({"count=many"}, opts, err));
    EXPECT_FALSE(parseScopeDumpArgs({"level="}, opts, err));
}

TEST(ScopeDumpOpts, UsageListsAllOptions) {
    const std::string usage = trust::trace::scopeDumpOptionsUsage();
    EXPECT_NE(usage.find("level=current|all"), std::string::npos);
    EXPECT_NE(usage.find("types=on|off"), std::string::npos);
    EXPECT_NE(usage.find("max=<N>"), std::string::npos);
    EXPECT_NE(usage.find("count=only"), std::string::npos);
}

TEST(ScopeDumpOpts, FromArgsFaultsOnInvalidInput) {
    EXPECT_THROW((void)trust::debug::scopeDumpOptsFrom({"level=deep"}), std::runtime_error);
    EXPECT_NO_THROW((void)trust::debug::scopeDumpOptsFrom({"level=all"}));
    EXPECT_NO_THROW((void)trust::debug::scopeDumpOptsFrom({}));
}

// -- formatScopeStack --

namespace {

// Стек из трёх скоупов: global(gvar) -> block(x, y) -> anonymous(deep).
trust::SymbolTable makeStack(std::shared_ptr<trust::ScopeBlock>& blockOwner) {
    trust::SymbolTable st;
    trust::Symbol g;
    g.name = "gvar";
    g.type = 1;
    g.storage = trust::Storage::Global;
    EXPECT_TRUE(st.declare(g));

    blockOwner = std::make_shared<trust::ScopeBlock>("blk");
    st.push(blockOwner.get());
    trust::Symbol x;
    x.name = "x";
    x.type = 2;
    st.declare(x);
    trust::Symbol y;
    y.name = "y";
    y.type = 3;
    st.declare(y);

    st.push(); // скоуп без узла-создателя
    trust::Symbol deep;
    deep.name = "deep";
    deep.type = 4;
    st.declare(deep);
    return st;
}

} // namespace

TEST(ScopeDump, CountOnly) {
    std::shared_ptr<trust::ScopeBlock> block;
    trust::SymbolTable st = makeStack(block);
    ScopeDumpOpts opts;
    opts.count_only = true;
    EXPECT_EQ(formatScopeStack(st, opts), "scope: depth=3 names=4");
}

TEST(ScopeDump, DefaultShowsCurrentLevelOnly) {
    std::shared_ptr<trust::ScopeBlock> block;
    trust::SymbolTable st = makeStack(block);
    const std::string dump = formatScopeStack(st, ScopeDumpOpts{});
    EXPECT_NE(dump.find("scope: depth=3 names=4"), std::string::npos);
    EXPECT_NE(dump.find("[2] -: deep"), std::string::npos);
    EXPECT_EQ(dump.find("gvar"), std::string::npos);
    EXPECT_EQ(dump.find("x, y"), std::string::npos);
}

TEST(ScopeDump, AllLevelsShowCreatorsAndNames) {
    std::shared_ptr<trust::ScopeBlock> block;
    trust::SymbolTable st = makeStack(block);
    ScopeDumpOpts opts;
    opts.current_only = false;
    const std::string dump = formatScopeStack(st, opts);
    EXPECT_NE(dump.find("[2] -: deep"), std::string::npos);
    EXPECT_NE(dump.find("[1] ScopeBlock: x, y"), std::string::npos);
    EXPECT_NE(dump.find("[0] global: gvar"), std::string::npos);
}

TEST(ScopeDump, NameMaskFiltersSymbols) {
    std::shared_ptr<trust::ScopeBlock> block;
    trust::SymbolTable st = makeStack(block);
    ScopeDumpOpts opts;
    opts.current_only = false;
    opts.name_mask = "g*";
    const std::string dump = formatScopeStack(st, opts);
    EXPECT_NE(dump.find("gvar"), std::string::npos);
    EXPECT_EQ(dump.find("deep"), std::string::npos);
    EXPECT_EQ(dump.find("x, y"), std::string::npos);
}

TEST(ScopeDump, MaxNamesTruncates) {
    std::shared_ptr<trust::ScopeBlock> block;
    trust::SymbolTable st = makeStack(block);
    ScopeDumpOpts opts;
    opts.current_only = false;
    opts.max_names = 1;
    const std::string dump = formatScopeStack(st, opts);
    EXPECT_NE(dump.find("[1] ScopeBlock: x ..."), std::string::npos);
}

TEST(ScopeDump, TypesShownWhenRequested) {
    std::shared_ptr<trust::ScopeBlock> block;
    trust::SymbolTable st = makeStack(block);
    ScopeDumpOpts opts;
    opts.show_types = true;
    const std::string dump = formatScopeStack(st, opts);
    EXPECT_NE(dump.find("deep<T4"), std::string::npos);
}

} // namespace
