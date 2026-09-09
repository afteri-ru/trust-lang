// test/unit/semantic/borrow_check_test.cpp
// Unit-тесты статического borrow-checker'а (BorrowCheckHook): инференс региона из Storage
// и порядок регионов (types/REFType.md §14.3). Правила заимствования (R1/R4/R5/R6)
// проверяются сквозными lit-тестами (test/lit/pipeline/diag/borrow/*).

#include "semantic/borrow_check.hpp"
#include "analysis/symbol_table.hpp"

#include "gtest/gtest.h"

namespace trust {

TEST(BorrowCheckRegionTest, StorageToRegion) {
    // Маппинг storage -> region: Local -> scope, TLS -> thread, Static/Global -> program.
    EXPECT_EQ(BorrowCheckHook::regionOfStorage(Storage::Local), BorrowRegion::Scope);
    EXPECT_EQ(BorrowCheckHook::regionOfStorage(Storage::ThreadLocal), BorrowRegion::Thread);
    EXPECT_EQ(BorrowCheckHook::regionOfStorage(Storage::Static), BorrowRegion::Program);
    EXPECT_EQ(BorrowCheckHook::regionOfStorage(Storage::Global), BorrowRegion::Program);
}

TEST(BorrowCheckRegionTest, RegionOrdering) {
    // program >= thread >= scope (упорядоченная решётка регионов).
    EXPECT_LT(static_cast<int>(BorrowRegion::Scope), static_cast<int>(BorrowRegion::Thread));
    EXPECT_LT(static_cast<int>(BorrowRegion::Thread), static_cast<int>(BorrowRegion::Program));
}

TEST(BorrowCheckRegionTest, RegionNames) {
    EXPECT_STREQ(borrowRegionName(BorrowRegion::Scope), "scope");
    EXPECT_STREQ(borrowRegionName(BorrowRegion::Thread), "thread");
    EXPECT_STREQ(borrowRegionName(BorrowRegion::Program), "program");
}

} // namespace trust
