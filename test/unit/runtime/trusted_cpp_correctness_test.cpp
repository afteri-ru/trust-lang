// test/unit/runtime/trusted_cpp_correctness_test.cpp
// Тесты контракта и исправленных свойств trusted-cpp:
//   - const-correctness read-only лока (lock_const НЕ позволяет мутировать на уровне типов);
//   - Locker::get();
//   - Shared::reset() / Weak::reset();
//   - try_lock возвращает nullopt ТОЛЬКО на nullptr/конкуренции (без глотания ошибок);
//   - lock()/lock_const() на const Shared;
//   - lock() на nullptr -> исключение (должно быть исключение, не тихий fallback);
//   - lock() - SHARED capture (НЕ опустошает источник).
#include "trusted_cpp_test_fixture.hpp"

#include <type_traits>

using namespace trust;

// -- Const-correctness: read-only лок не даёт мутировать на уровне типов -----------------
static_assert(std::is_same_v<decltype(*std::declval<Shared<int>>().lock()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<Shared<int>>().lock_const()), const int&>);
static_assert(std::is_same_v<decltype(*std::declval<const Shared<int>>().lock()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<const Shared<int>>().lock_const()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<Locker<int, false>>().get()), int&>);
static_assert(std::is_same_v<decltype(std::declval<Locker<int, true>>().get()), const int&>);
static_assert(std::is_same_v<decltype(std::declval<Locker<int, false>>().operator->()), int*>);
static_assert(std::is_same_v<decltype(std::declval<Locker<int, true>>().operator->()), const int*>);
// Прямой доступ plain Shared: не-const -> V&, const -> const V&.
static_assert(std::is_same_v<decltype(*std::declval<Shared<int>>()), int&>);
static_assert(std::is_same_v<decltype(*std::declval<const Shared<int>>()), const int&>);

TEST_F(TrustedCppTest, ReadOnlyLockReturnsConstRef) {
    Shared<int> s(42);
    const auto& v = *s.lock_const();
    EXPECT_EQ(v, 42);
}

TEST_F(TrustedCppTest, MutableLockReturnsMutableRef) {
    Shared<int> s(0);
    *s.lock() = 42;
    EXPECT_EQ(*s.lock_const(), 42);
}

TEST_F(TrustedCppTest, LockerGetMutable) {
    Shared<int> s(0);
    {
        auto locked = s.lock();
        locked.get() = 7;
    }
    EXPECT_EQ(s.lock_const().get(), 7);
}

TEST_F(TrustedCppTest, LockerGetReadOnly) {
    Shared<int> s(9);
    EXPECT_EQ(s.lock_const().get(), 9);
}

TEST_F(TrustedCppTest, ConstSharedLockable) {
    const Shared<int> s(42);
    EXPECT_EQ(*s.lock_const(), 42);
    // const Shared по-прежнему позволяет исключительную блокировку (как `const shared_ptr<T>`).
    EXPECT_EQ(*s.lock(), 42);
}

// -- reset() -------------------------------------------------------------------------------
TEST_F(TrustedCppTest, WeakReset) {
    Shared<int> s(42);
    auto w = s.weak();
    EXPECT_TRUE(w.has_value());
    w.reset();
    EXPECT_FALSE(w.has_value());
    EXPECT_THROW(w.lock(), std::runtime_error);
}

// -- lock() на nullptr: контракт (take/разыменование nullptr) = исключение, не fallback -----
TEST_F(TrustedCppTest, LockNullThrowsContract) {
    Shared<int> s;
    EXPECT_THROW(s.lock(), std::runtime_error);
    EXPECT_THROW(s.lock_const(), std::runtime_error);
    EXPECT_THROW(*s, std::runtime_error);
    EXPECT_THROW(s.get(), std::runtime_error);
}

// -- try_lock: nullopt ТОЛЬКО на nullptr (нет тихого глотания программных ошибок) ----------
TEST_F(TrustedCppTest, TryLockNullReturnsNullopt) {
    Shared<int> s;
    EXPECT_FALSE(s.try_lock().has_value());
    EXPECT_FALSE(s.try_lock_const().has_value());
}

TEST_F(TrustedCppTest, TryLockNonNullReturnsValue) {
    Shared<int> s(42);
    EXPECT_TRUE(s.try_lock().has_value());
    EXPECT_TRUE(s.try_lock_const().has_value());
}

// -- lock() - SHARED capture (НЕ опустошает источник) -------------------------------------
TEST_F(TrustedCppTest, LockIsSharedCaptureNotTransfer) {
    Shared<int> s(42);
    {
        auto guard = s.lock();
        *guard = 100;
    }
    EXPECT_TRUE(s.has_value()); // источник остался непустым
    EXPECT_EQ(*s.lock_const(), 100);
}

// -- const-qualified Weak lock --------------------------------------------------------------
TEST_F(TrustedCppTest, ConstWeakLock) {
    Shared<int> s(42);
    const auto w = s.weak();
    EXPECT_EQ(*w.lock(), 42);
    EXPECT_EQ(*w.lock_const(), 42);
}
