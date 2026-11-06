// test/unit/utils/text_interner_test.cpp
// Пул строк (`utils/string_interner.hpp`): дедупликация и стабильность view, вложенность/восстановление
// активного пула (`StringInternerScope`), явный процессный пул вне компиляции (`ManualInternerScope`),
// а также имена символов анализатора (view в тот же пул).
#include "symbols/symbol_table.hpp"
#include "utils/string_interner.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <thread>
#include <vector>

namespace trust {
namespace {

TEST(StringInternerTest, InternDeduplicatesAndKeepsStableView) {
    StringInterner interner;
    const std::string_view a = interner.intern("abc");
    const std::string_view b = interner.intern("abc");
    EXPECT_EQ(a.data(), b.data());
    EXPECT_EQ(interner.size(), 1u);
    EXPECT_TRUE(interner.owns("abc"));
    EXPECT_FALSE(interner.owns("abcd"));

    const std::string_view joined = interner.concat({"ab", "c"});
    EXPECT_EQ(joined, "abc");
    EXPECT_EQ(joined.data(), a.data());
}

// Активный пул восстанавливается при вложенном scope и после выхода из внешнего.
TEST(StringInternerTest, ScopeNestingRestoresPreviousPool) {
    ASSERT_EQ(detail::activeInternerSlot(), nullptr) << "test must start without an active pool";

    StringInterner outer;
    StringInterner inner;
    std::string_view outerText;
    {
        StringInternerScope outerScope(outer);
        outerText = internText("scope_outer_text");
        EXPECT_TRUE(outer.owns("scope_outer_text"));

        {
            StringInternerScope innerScope(inner);
            EXPECT_EQ(detail::activeInternerSlot(), &inner);
            EXPECT_TRUE(inner.owns(internText("scope_inner_text")));
            EXPECT_FALSE(outer.owns("scope_inner_text"));
        }

        EXPECT_EQ(detail::activeInternerSlot(), &outer) << "inner scope must restore the outer pool";
        EXPECT_EQ(internText("scope_outer_text").data(), outerText.data());
    }
    EXPECT_EQ(detail::activeInternerSlot(), nullptr);
}

// Вне компиляции процессный пул доступен только по явному ManualInternerScope.
TEST(StringInternerTest, ManualScopeOutsideCompilation) {
    ASSERT_EQ(detail::activeInternerSlot(), nullptr);
    ManualInternerScope manualScope;
    const std::string_view text = internText("manual_fallback_text");
    EXPECT_TRUE(manualStringInterner().owns(text));
}

// Явный процессный scope вложен в компиляционный: активный пул компиляции побеждает.
TEST(StringInternerTest, CompilationPoolWinsOverManualScope) {
    ManualInternerScope manualScope;
    StringInterner pool;
    StringInternerScope scope(pool);
    const std::string_view text = internText("compilation_pool_text");
    EXPECT_TRUE(pool.owns(text));
    EXPECT_FALSE(manualStringInterner().owns(text));
}

// Имена символов анализатора - view в тот же пул: одинаковое имя хранится один раз.
TEST(SymbolNameInterningTest, EqualNamesShareStorage) {
    const std::string_view a = stableSymbolName("same_name");
    const std::string_view b = stableSymbolName("same_name");
    EXPECT_EQ(a.data(), b.data());
    EXPECT_EQ(a, "same_name");
}

// Процессный пул доступен из разных потоков: одинаковый текст интернируется один раз.
TEST(StringInternerTest, SharedPoolIsThreadSafe) {
    SharedStringInterner pool;
    constexpr int kThreads = 4;
    std::vector<std::string_view> views(kThreads);
    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int i = 0; i < kThreads; ++i) {
        threads.emplace_back([&pool, &views, i] { views[i] = pool.intern("threaded_spelling"); });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (int i = 1; i < kThreads; ++i) {
        EXPECT_EQ(views[i].data(), views[0].data()) << "equal spellings must share one interned string across threads";
    }
    EXPECT_EQ(pool.size(), 1u);
}

// (S3) reserve() - только предварительный размер индекса: семантика intern/owns/size не меняется.
TEST(StringInternerTest, ReserveKeepsLookupSemantics) {
    StringInterner pool;
    pool.reserve(256);

    const std::string_view stored = pool.intern("reserved_text");
    EXPECT_TRUE(pool.owns("reserved_text"));
    EXPECT_FALSE(pool.owns("reserved_text_other"));
    EXPECT_EQ(pool.size(), 1u);
    EXPECT_EQ(pool.intern("reserved_text").data(), stored.data());
}

// (S3) Канонический слот текста AST: пустое написание не интернируется, непустое - интернируется.
TEST(StringInternerTest, InternedTextSlotSkipsEmpty) {
    StringInterner pool;
    StringInternerScope scope(pool);

    EXPECT_TRUE(internedTextSlot("").empty());
    EXPECT_EQ(pool.size(), 0u) << "empty spelling must not create a pool entry";

    const std::string_view text = internedTextSlot("interned_slot_text");
    EXPECT_EQ(text, "interned_slot_text");
    EXPECT_TRUE(pool.owns(text));
    EXPECT_EQ(pool.size(), 1u);
}

} // namespace
} // namespace trust
