#include "symbols/table/symbol_arena.hpp"
#include "symbols/symbol_table.hpp"

#include "gtest/gtest.h"

#include <type_traits>

namespace trust {
namespace {

// -- SymbolArena: владелец синтетических Symbol (слитые наборы), адреса стабильны -------------

TEST(SymbolArena, AddressesAreStableWhileGrowing) {
    SymbolArena arena;
    Symbol first;
    first.name = "first";
    Symbol& storedFirst = arena.adopt(first);

    for (int i = 0; i < 256; ++i) {
        Symbol extra;
        extra.name = "extra";
        arena.adopt(std::move(extra));
    }

    EXPECT_EQ(storedFirst.name, "first");
    EXPECT_EQ(arena.size(), 257u);
    Symbol& another = arena.adopt(first);
    EXPECT_NE(&another, &storedFirst);
    EXPECT_EQ(storedFirst.name, "first");
}

TEST(SymbolTable, CopyIsDeletedMoveIsAllowed) {
    // Копия оставила бы висячие Symbol* в экспорт-поверхности (арена таблицы).
    static_assert(!std::is_copy_constructible_v<SymbolTable>);
    static_assert(!std::is_copy_assignable_v<SymbolTable>);
    static_assert(std::is_move_constructible_v<SymbolTable>);
    static_assert(std::is_move_assignable_v<SymbolTable>);
}

} // namespace
} // namespace trust
