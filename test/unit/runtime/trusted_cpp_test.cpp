#include "runtime/trusted_cpp_test_fixture.hpp"

#include <shared_mutex>
#include <type_traits>

using namespace trust;

// ============================================================================
// Plain Locker (no sync) - the guard returned by Shared::lock()/lock_const()
// ============================================================================

TEST_F(TrustedCppTest, LockerDereference) {
    Shared<int> s(42);
    auto locked = s.lock();
    EXPECT_EQ(*locked, 42);
    *locked = 100;
    EXPECT_EQ(*locked, 100);
}

TEST_F(TrustedCppTest, LockerDereferenceConst) {
    Shared<int> s(42);
    auto locked = s.lock_const();
    // Read-only: type is const V& (cannot mutate).
    static_assert(std::is_same_v<decltype(*locked), const int&>);
    EXPECT_EQ(*locked, 42);
}

TEST_F(TrustedCppTest, LockerGet) {
    Shared<int> s(42);
    auto locked = s.lock();
    locked.get() = 7;
    EXPECT_EQ(s.lock_const().get(), 7);
    EXPECT_EQ(s.lock_const().get(), 7);
}

TEST_F(TrustedCppTest, LockerMoveConstructor) {
    Shared<int> s(42);
    auto locked1 = s.lock();
    EXPECT_EQ(*locked1, 42);
    auto locked2 = std::move(locked1);
    EXPECT_FALSE(static_cast<bool>(locked1));
    EXPECT_TRUE(static_cast<bool>(locked2));
    EXPECT_EQ(*locked2, 42);
}

TEST_F(TrustedCppTest, LockerMoveAssignment) {
    Shared<int> s(42);
    auto locked1 = s.lock();
    auto locked2 = s.lock();
    locked2 = std::move(locked1);
    EXPECT_FALSE(static_cast<bool>(locked1));
    EXPECT_TRUE(static_cast<bool>(locked2));
    EXPECT_EQ(*locked2, 42);
}

TEST_F(TrustedCppTest, LockerOperatorBool) {
    Shared<int> s(42);
    auto locked = s.lock();
    EXPECT_TRUE(static_cast<bool>(locked));
    auto moved = std::move(locked);
    EXPECT_FALSE(static_cast<bool>(locked));
    EXPECT_TRUE(static_cast<bool>(moved));
}

// ============================================================================
// Plain Shared - lock()/take is a SHARED capture (does NOT empty the source), throws on null
// ============================================================================

TEST_F(TrustedCppTest, LockDoesNotEmptyShared) {
    Shared<int> s(42);
    {
        auto l = s.lock();
        EXPECT_EQ(*l, 42);
    }
    // Source reference remains valid and non-empty: shared capture, not ownership transfer.
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(*s.lock(), 42);
}

TEST_F(TrustedCppTest, LockNullThrows) {
    Shared<int> s;
    EXPECT_THROW(s.lock(), std::runtime_error);
    EXPECT_THROW(s.lock_const(), std::runtime_error);
    EXPECT_FALSE(s.try_lock().has_value());
    EXPECT_FALSE(s.try_lock_const().has_value());
}

TEST_F(TrustedCppTest, LockConstDoesNotEmptyShared) {
    Shared<int> s(42);
    {
        auto l = s.lock_const();
        EXPECT_EQ(*l, 42);
    }
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(*s.lock_const(), 42);
}

// ============================================================================
// Plain Shared - direct access (no sync; throws on null, never UB/silent fallback)
// ============================================================================

TEST_F(TrustedCppTest, PlainDirectAccess) {
    Shared<int> s(42);
    EXPECT_EQ(*s, 42);
    *s = 100;
    EXPECT_EQ(s.get(), 100);
    EXPECT_EQ(*s.operator->(), 100);
}

TEST_F(TrustedCppTest, PlainDirectAccessConst) {
    const Shared<int> s(42);
    static_assert(std::is_same_v<decltype(*s), const int&>);
    EXPECT_EQ(*s, 42);
}

TEST_F(TrustedCppTest, PlainDirectAccessNullThrows) {
    Shared<int> s;
    EXPECT_THROW(*s, std::runtime_error);
    EXPECT_THROW(s.get(), std::runtime_error);
}

TEST_F(TrustedCppTest, PlainSet) {
    Shared<int> s(0);
    s.set(42);
    EXPECT_EQ(*s, 42);
}

TEST_F(TrustedCppTest, PlainSetNullThrows) {
    Shared<int> s;
    EXPECT_THROW(s.set(1), std::runtime_error);
}

// ============================================================================
// SyncShared with SyncMutexPolicy (default; exclusive lock)
// ============================================================================

TEST_F(TrustedCppTest, SyncSharedBasicLock) {
    SyncShared<int> s(42);
    {
        auto locked = s.lock();
        *locked = 100;
    }
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 100);
    }
}

TEST_F(TrustedCppTest, SyncSharedNullThrows) {
    SyncShared<int> s;
    EXPECT_THROW(s.lock(), std::runtime_error);
    EXPECT_FALSE(s.try_lock().has_value());
}

TEST_F(TrustedCppTest, SyncSharedThreadSafety) {
    SyncShared<int> s(0);
    const int num_threads = 4;
    const int increments = 1000;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&s, increments]() {
            for (int j = 0; j < increments; ++j) {
                auto locked = s.lock();
                (*locked)++;
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(*s.lock_const(), num_threads * increments);
}

// ============================================================================
// SyncShared with SyncRwMutexPolicy (exclusive lock / shared read-only lock_const)
// ============================================================================

using SyncSharedRead = SyncShared<int, SyncRwMutexPolicy>;

TEST_F(TrustedCppTest, SyncSharedSharedMutexConstLock) {
    SyncSharedRead s(42);
    {
        auto locked = s.lock();
        *locked = 100;
    }
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 100);
    }
}

TEST_F(TrustedCppTest, SyncSharedSharedMutexConcurrentReads) {
    SyncSharedRead s(42);
    std::atomic<int> read_count{0};
    std::atomic<bool> stop{false};
    const int num_readers = 4;
    const int read_iterations = 500;

    std::vector<std::thread> threads;
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back([&s, &read_count, &stop, read_iterations]() {
            for (int j = 0; j < read_iterations && !stop; ++j) {
                auto locked = s.lock_const();
                volatile int val = *locked;
                (void)val;
                read_count++;
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(read_count.load(), num_readers * read_iterations);
}

// ============================================================================
// try_lock semantics: nullopt ONLY on empty reference / lock failure (never swallowed)
// ============================================================================

TEST_F(TrustedCppTest, TryLockPlainEmpty) {
    Shared<int> s;
    EXPECT_FALSE(s.try_lock().has_value());
    EXPECT_FALSE(s.try_lock_const().has_value());
}

TEST_F(TrustedCppTest, TryLockPlainSuccess) {
    Shared<int> s(42);
    auto opt = s.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
    auto opt_c = s.try_lock_const();
    ASSERT_TRUE(opt_c.has_value());
    EXPECT_EQ(**opt_c, 42);
}

TEST_F(TrustedCppTest, TryLockSyncSuccess) {
    SyncShared<int> s(42);
    auto opt = s.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
    SyncSharedRead sr(42);
    auto opt_c = sr.try_lock_const();
    ASSERT_TRUE(opt_c.has_value());
    EXPECT_EQ(**opt_c, 42);
}

// ============================================================================
// Locker value-owning constructor (for kLocker codegen variable declarations)
// ============================================================================

TEST_F(TrustedCppTest, LockerValueConstructor) {
    Locker<int, false> l(5);
    EXPECT_TRUE(static_cast<bool>(l));
    EXPECT_EQ(*l, 5);
    *l = 7;
    EXPECT_EQ(*l, 7);
}

// ============================================================================
// SyncSingleThreadPolicy: object usable ONLY in the thread where it was created
// ============================================================================

TEST_F(TrustedCppTest, SingleThreadPolicySameThread) {
    SyncShared<int, SyncSingleThreadPolicy> s(42);
    auto locked = s.lock();
    EXPECT_EQ(*locked, 42);
    auto locked_c = s.lock_const();
    EXPECT_EQ(*locked_c, 42);
}

TEST_F(TrustedCppTest, SingleThreadPolicyCrossThreadThrows) {
    SyncShared<int, SyncSingleThreadPolicy> s(42);
    std::atomic<bool> threw{false};
    std::thread t([&]() {
        try {
            auto locked = s.lock();
            (void)locked;
        } catch (const std::exception&) {
            threw = true;
        }
    });
    t.join();
    EXPECT_TRUE(threw.load());
}

// ============================================================================
// Deadlock detector: blocking lock() throws on timeout (deadlock)
// ============================================================================

TEST_F(TrustedCppTest, DeadlockDetectorThrowsOnTimeout) {
    SyncShared<int> s(42);
    auto held = s.lock(); // удерживаем блокировку в main
    const auto saved_timeout = trust::runtime::syncDeadlockTimeout();
    trust::runtime::setSyncDeadlockTimeout(std::chrono::milliseconds(50));
    std::atomic<bool> deadlocked{false};
    std::thread t([&]() {
        try {
            auto locked = s.lock();
            (void)locked;
        } catch (const std::exception&) {
            deadlocked = true;
        }
    });
    t.join();
    trust::runtime::setSyncDeadlockTimeout(saved_timeout);
    EXPECT_TRUE(deadlocked.load());
}

TEST_F(TrustedCppTest, DeadlockDetectorPerObjectTimeout) {
    // Пер-объектный таймаут (3-й аргумент reftype) перекрывает глобальный: deadlock по нему.
    SyncShared<int> s(42, std::chrono::milliseconds(50));
    auto held = s.lock();
    std::atomic<bool> deadlocked{false};
    std::thread t([&]() {
        try {
            auto locked = s.lock();
            (void)locked;
        } catch (const std::exception&) {
            deadlocked = true;
        }
    });
    t.join();
    EXPECT_TRUE(deadlocked.load());
}

// ============================================================================
// parseSyncDuration / applySystemEnv (системные опции --trust:)
// ============================================================================

TEST_F(TrustedCppTest, ParseSyncDuration) {
    trust::SyncTimeoutType out{};
    EXPECT_TRUE(trust::runtime::parseSyncDuration("5", out));
    EXPECT_EQ(out, std::chrono::seconds(5));
    EXPECT_TRUE(trust::runtime::parseSyncDuration("5s", out));
    EXPECT_EQ(out, std::chrono::seconds(5));
    EXPECT_TRUE(trust::runtime::parseSyncDuration("500ms", out));
    EXPECT_EQ(out, std::chrono::milliseconds(500));
    EXPECT_TRUE(trust::runtime::parseSyncDuration("1000000ns", out));
    EXPECT_EQ(out, std::chrono::milliseconds(1));
    EXPECT_TRUE(trust::runtime::parseSyncDuration("5nano", out));
    EXPECT_EQ(out, std::chrono::milliseconds(0)); // 5ns < 1ms
    EXPECT_FALSE(trust::runtime::parseSyncDuration("", out));
    EXPECT_FALSE(trust::runtime::parseSyncDuration("abc", out));
    EXPECT_FALSE(trust::runtime::parseSyncDuration("10zz", out));
    // syncTimeoutFromString: некорректное значение -> fallback.
    EXPECT_EQ(trust::runtime::syncTimeoutFromString("100ms", std::chrono::milliseconds(7)), std::chrono::milliseconds(100));
    EXPECT_EQ(trust::runtime::syncTimeoutFromString("zzz", std::chrono::milliseconds(7)), std::chrono::milliseconds(7));
}

TEST_F(TrustedCppTest, ApplySystemEnvSyncDeadlock) {
    const auto saved_timeout = trust::runtime::syncDeadlockTimeout();
    trust::runtime::applySystemEnv({"--trust:fsync-deadlock=100ms"});
    EXPECT_EQ(trust::runtime::syncDeadlockTimeout(), std::chrono::milliseconds(100));
    // --trust:fno-sync-deadlock -> блокировать до конца (таймаут < 0, детектор выкл).
    trust::runtime::applySystemEnv({"--trust:fno-sync-deadlock"});
    EXPECT_LT(trust::runtime::syncDeadlockTimeout().count(), 0);
    // Некорректное значение не меняет таймаут.
    trust::runtime::applySystemEnv({"--trust:fsync-deadlock=abc"});
    EXPECT_LT(trust::runtime::syncDeadlockTimeout().count(), 0);
    trust::runtime::setSyncDeadlockTimeout(saved_timeout);
}
