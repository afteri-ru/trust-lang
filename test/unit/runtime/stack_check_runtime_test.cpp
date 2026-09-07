// test/unit/runtime/stack_check_runtime_test.cpp
// Автономные юнит-тесты низкоуровневой реализации контроля переполнения стека
// (trust/stack_check.hpp) БЕЗ интеграции в компилятор trust: напрямую вызываются
// runtime-методы check_overflow / check_stack_limit / check_reserve / set_reserve /
// set_include_functions / get_stack_limit и проверяется поведение (throw / abort).
//
// Для работы check_stack_limit/get_stack_limit требуется секция .stack_sizes, поэтому
// этот файл компилируется с -fstack-size-section (см. test/unit/CMakeLists.txt).
// TLS `info` определяется здесь (единственная TU).

#include "trust/stack_check.hpp"

#include "gtest/gtest.h"

#include <cstddef>

const thread_local trust::stack_check trust::stack_check::info;

namespace trust {
namespace {

// Функции с реальным кадром стека (>0 байт), чтобы .stack_sizes записал ненулевой размер.
static std::size_t stackFoo() {
    volatile char buf[256] = {0};
    buf[0] = 1;
    return static_cast<std::size_t>(buf[0]);
}

static std::size_t stackBar() {
    volatile char buf[1024] = {0};
    buf[0] = 2;
    return static_cast<std::size_t>(buf[0]);
}

TEST(StackCheckRuntimeTest, CheckOverflowNoThrowWhenEnough) {
    // Свободного места на стеке теста много; малый N не должен бросать.
    EXPECT_NO_THROW(trust::stack_check::check_overflow(1024));
}

TEST(StackCheckRuntimeTest, CheckOverflowThrowsWhenInsufficient) {
    // N + reserve > свободного места (стек теста ~8MB) -> stack_overflow.
    EXPECT_THROW(trust::stack_check::check_overflow(1'000'000'000), trust::stack_overflow);
}

TEST(StackCheckRuntimeTest, SetGetReserve) {
    const std::size_t saved = trust::stack_check::get_reserve();
    trust::stack_check::set_reserve(4096);
    EXPECT_EQ(trust::stack_check::get_reserve(), static_cast<std::size_t>(4096));
    trust::stack_check::set_reserve(saved);
    EXPECT_EQ(trust::stack_check::get_reserve(), saved);
}

TEST(StackCheckRuntimeTest, CheckOverflowAddsReserve) {
    // check_overflow(N) бросает, если free < N + reserve. При огромном reserve бросает даже N=0.
    const std::size_t saved = trust::stack_check::get_reserve();
    trust::stack_check::set_reserve(1'000'000'000);
    EXPECT_THROW(trust::stack_check::check_overflow(0), trust::stack_overflow);
    trust::stack_check::set_reserve(saved);
}

TEST(StackCheckRuntimeTest, CheckReserveNoAbortWhenEnough) {
    // При достаточном резерве (default) abort не происходит.
    EXPECT_NO_FATAL_FAILURE(trust::stack_check::check_reserve());
}

TEST(StackCheckRuntimeTest, CheckReserveAbortsWhenInsufficient) {
    // При free < reserve НЕЛЬЗЯ создать исключение -> abort с диагностикой.
    const std::size_t saved = trust::stack_check::get_reserve();
    trust::stack_check::set_reserve(1'000'000'000);
    EXPECT_DEATH(trust::stack_check::check_reserve(), "insufficient stack reserve");
    trust::stack_check::set_reserve(saved);
}

TEST(StackCheckRuntimeTest, GetStackLimitPositive) {
    // Требует -fstack-size-section; максимум по всем функциям - ненулевой.
    trust::stack_check::set_limit({});
    EXPECT_GT(trust::stack_check::get_stack_limit(), static_cast<std::size_t>(0));
}

TEST(StackCheckRuntimeTest, GetLimitMatchesStackSizes) {
    // get_limit() == m_stack_limit (максимум по заданному перечню или всем функциям .stack_sizes).
    trust::stack_check::set_limit({});
    EXPECT_EQ(trust::stack_check::get_limit(), trust::stack_check::get_stack_limit());
}

TEST(StackCheckRuntimeTest, SetLimitRestrictsMax) {
    // set_limit({&stackFoo}) -> максимум не больше максимума по всем функциям.
    const std::size_t all = trust::stack_check::get_limit();
    trust::stack_check::set_limit({reinterpret_cast<void*>(&stackFoo)});
    const std::size_t foo = trust::stack_check::get_limit();
    trust::stack_check::set_limit({});
    EXPECT_GT(foo, static_cast<std::size_t>(0));
    EXPECT_LE(foo, all);
}

TEST(StackCheckRuntimeTest, CheckStackLimitNoThrowWhenEnough) {
    // free велико, m_stack_limit + reserve малы -> не бросает.
    trust::stack_check::set_limit({});
    EXPECT_NO_THROW(trust::stack_check::check_stack_limit());
}

} // namespace
} // namespace trust
