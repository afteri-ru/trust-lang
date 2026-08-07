#include <gtest/gtest.h>
#include "utils/cache.hpp"

namespace trust {
namespace {

// ══════════════════════════════════════════════════════════════
//                     SparseCache tests
// ══════════════════════════════════════════════════════════════

TEST(SparseCacheTest, EmptySource) {
    SparseCache c;
    c.build("");
    EXPECT_TRUE(c.empty());
    EXPECT_EQ(c.find_by_offset(1), nullptr);
    EXPECT_EQ(c.find_by_line(1), nullptr);
}

TEST(SparseCacheTest, SingleLineSource) {
    SparseCache c;
    c.build("hello world");
    // sqrt(1) = 1, max(2,1) = 2, min(2,1) = 1 → храним все строки
    EXPECT_GE(c.size(), 1u);
    auto* e = c.find_by_offset(1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->offset, 0u);
    EXPECT_EQ(e->line, 1u);

    e = c.find_by_line(1);
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->offset, 0u);
}

TEST(SparseCacheTest, MultiLineSource) {
    // Создаём 50 строк
    std::string src;
    for (int i = 0; i < 50; ++i) {
        src += "line " + std::to_string(i + 1) + "\n";
    }

    SparseCache c;
    c.build(src);
    EXPECT_GT(c.size(), 0u);

    // Первая строка
    auto* first = c.find_by_offset(0);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->offset, 0u);
    EXPECT_EQ(first->line, 1u);

    // Последняя строка — номер <= 50, т.к. кеш хранит sqrt(N) записей
    auto* last = c.find_by_offset(static_cast<uint32_t>(src.size() - 1));
    ASSERT_NE(last, nullptr);
    EXPECT_LE(last->line, 50u);
    EXPECT_GT(last->line, 30u);

    // find_by_line
    auto* l1 = c.find_by_line(1);
    ASSERT_NE(l1, nullptr);
    EXPECT_EQ(l1->offset, 0u);

    auto* l50 = c.find_by_line(50);
    ASSERT_NE(l50, nullptr);
}

TEST(SparseCacheTest, SqrtSizeLimit) {
    std::string src;
    for (int i = 0; i < 100; ++i) {
        src += std::to_string(i + 1) + "\n";
    }

    SparseCache c;
    c.build(src);
    // Для 100 строк sqrt = 10, кеш ≤ 10 записей
    EXPECT_LE(c.size(), 10u);
    EXPECT_GT(c.size(), 0u);
}

TEST(SparseCacheTest, CacheRebuildIdempotent) {
    std::string src = "a\nb\nc\nd\ne\n";
    SparseCache c;
    c.build(src);
    size_t count = c.size();
    EXPECT_GT(count, 0u);

    // Повторный build не меняет кеш
    c.build(src);
    EXPECT_EQ(c.size(), count);
}

// ══════════════════════════════════════════════════════════════
//                     LruCache tests
// ══════════════════════════════════════════════════════════════

TEST(LruCacheTest, EmptyCache) {
    LruCache<int, int> c(4);
    EXPECT_EQ(c.lookup(1), nullptr);
}

TEST(LruCacheTest, InsertAndLookup) {
    LruCache<int, int> c(4);
    c.insert(1, 100);
    auto* v = c.lookup(1);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 100);
}

TEST(LruCacheTest, Eviction) {
    LruCache<int, int> c(3);
    c.insert(1, 10);
    c.insert(2, 20);
    c.insert(3, 30);
    c.insert(4, 40); // должен вытеснить 1

    EXPECT_EQ(c.lookup(1), nullptr);
    EXPECT_NE(c.lookup(2), nullptr);
    EXPECT_NE(c.lookup(3), nullptr);
    EXPECT_NE(c.lookup(4), nullptr);
}

TEST(LruCacheTest, LookupPromotes) {
    LruCache<int, int> c(3);
    c.insert(1, 10);
    c.insert(2, 20);
    c.insert(3, 30);

    // lookup 1 — делает её "последней использованной"
    EXPECT_NE(c.lookup(1), nullptr);

    // теперь вставляем 4 — вытеснится 2 (самая давняя)
    c.insert(4, 40);
    EXPECT_EQ(c.lookup(2), nullptr);
    EXPECT_NE(c.lookup(1), nullptr);
    EXPECT_NE(c.lookup(3), nullptr);
    EXPECT_NE(c.lookup(4), nullptr);
}

TEST(LruCacheTest, OverwriteExisting) {
    LruCache<int, int> c(4);
    c.insert(1, 100);
    c.insert(1, 200); // перезапись

    auto* v = c.lookup(1);
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, 200);
    // размер (всего слотов минус свободные) — не проверяем, т.к. не храним размер
}

TEST(LruCacheTest, CustomStruct) {
    struct Point {
        int x, y;
    };
    LruCache<int, Point> c(3);
    c.insert(1, Point{10, 20});
    auto* p = c.lookup(1);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 10);
    EXPECT_EQ(p->y, 20);

    // eviction
    c.insert(2, Point{0, 0});
    c.insert(3, Point{0, 0});
    c.insert(4, Point{0, 0});
    EXPECT_EQ(c.lookup(1), nullptr);
}

} // namespace
} // namespace trust