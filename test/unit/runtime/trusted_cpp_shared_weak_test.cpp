#include "runtime/trusted_cpp_test_fixture.hpp"

#include <shared_mutex>
#include <string>

using namespace trust;

// ============================================================================
// Weak (non-owning reference) - over plain Shared
// ============================================================================

TEST_F(TrustedCppTest, WeakDefaultConstructor) {
    Weak<Shared<int>> w;
    EXPECT_FALSE(static_cast<bool>(w));
    EXPECT_FALSE(w.has_value());
}

TEST_F(TrustedCppTest, WeakFromShared) {
    Shared<int> s(42);
    Weak<Shared<int>> w = s.weak();
    EXPECT_TRUE(static_cast<bool>(w));
    EXPECT_TRUE(w.has_value());
    {
        auto locked = w.lock();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakAfterSharedDestroyed) {
    Weak<Shared<int>> w;
    {
        Shared<int> s(42);
        w = s.weak();
        EXPECT_TRUE(static_cast<bool>(w));
    }
    EXPECT_FALSE(static_cast<bool>(w));
    EXPECT_FALSE(w.has_value());
    EXPECT_THROW(w.lock(), std::runtime_error);
}

TEST_F(TrustedCppTest, WeakConstLock) {
    Shared<int> s(42);
    const Weak<Shared<int>> w = s.weak();
    EXPECT_EQ(*w.lock(), 42);
    EXPECT_EQ(*w.lock_const(), 42);
}

TEST_F(TrustedCppTest, WeakSetMethod) {
    Shared<int> s(0);
    Weak<Shared<int>> w = s.weak();
    w.set(42);
    EXPECT_EQ(*s.lock_const(), 42);
}

TEST_F(TrustedCppTest, WeakWithStdString) {
    Shared<std::string> s("test");
    Weak<Shared<std::string>> w = s.weak();
    {
        auto locked = w.lock();
        EXPECT_EQ(*locked, "test");
    }
}

TEST_F(TrustedCppTest, WeakTryLockSuccess) {
    Shared<int> s(42);
    Weak<Shared<int>> w = s.weak();
    auto opt = w.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
    auto opt_c = w.try_lock_const();
    ASSERT_TRUE(opt_c.has_value());
    EXPECT_EQ(**opt_c, 42);
}

TEST_F(TrustedCppTest, WeakTryLockOnExpired) {
    Weak<Shared<int>> w;
    {
        Shared<int> s(42);
        w = s.weak();
    }
    EXPECT_FALSE(w.try_lock().has_value());
    EXPECT_FALSE(w.try_lock_const().has_value());
}

TEST_F(TrustedCppTest, WeakMoveConstructor) {
    Shared<int> s(42);
    Weak<Shared<int>> w1 = s.weak();
    Weak<Shared<int>> w2 = std::move(w1);
    EXPECT_TRUE(static_cast<bool>(w2));
    {
        auto locked = w2.lock();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakMoveAssignment) {
    Shared<int> s(42);
    Weak<Shared<int>> w1 = s.weak();
    Weak<Shared<int>> w2;
    w2 = std::move(w1);
    EXPECT_TRUE(static_cast<bool>(w2));
}

TEST_F(TrustedCppTest, WeakCopyConstructorConst) {
    Shared<int> s(42);
    const Weak<Shared<int>> w1 = s.weak();
    Weak<Shared<int>> w2(w1);
    EXPECT_TRUE(static_cast<bool>(w2));
}

TEST_F(TrustedCppTest, WeakOperatorBoolOnExpired) {
    Weak<Shared<int>> w;
    {
        Shared<int> s(42);
        w = s.weak();
        EXPECT_TRUE(static_cast<bool>(w));
    }
    EXPECT_FALSE(static_cast<bool>(w));
}

// ============================================================================
// Weak over SyncShared (synchronized reference)
// ============================================================================

TEST_F(TrustedCppTest, WeakOverSyncShared) {
    SyncShared<int> s(42);
    Weak<SyncShared<int>> w = s.weak();
    EXPECT_TRUE(w.has_value());
    {
        auto locked = w.lock();
        EXPECT_EQ(*locked, 42);
    }
    EXPECT_EQ(*w.lock_const(), 42);
}

TEST_F(TrustedCppTest, WeakOverSyncSharedExpired) {
    Weak<SyncShared<int>> w;
    {
        SyncShared<int> s(42);
        w = s.weak();
    }
    EXPECT_FALSE(w.has_value());
    EXPECT_THROW(w.lock(), std::runtime_error);
}

// ============================================================================
// Thread safety of SyncShared
// ============================================================================

TEST_F(TrustedCppTest, SyncSharedThreadSafetyTimedMutex) {
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

TEST_F(TrustedCppTest, SyncSharedThreadSafetyTimedShared) {
    using S = SyncShared<int, SyncRwMutexPolicy>;
    S s(0);
    const int num_writers = 2;
    const int num_readers = 4;
    const int operations = 500;
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};

    std::vector<std::thread> threads;
    for (int i = 0; i < num_writers; ++i) {
        threads.emplace_back([&s, operations, &write_count]() {
            for (int j = 0; j < operations; ++j) {
                auto locked = s.lock();
                (*locked)++;
                write_count++;
            }
        });
    }
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back([&s, operations, &read_count]() {
            for (int j = 0; j < operations; ++j) {
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
    EXPECT_EQ(write_count.load(), num_writers * operations);
    EXPECT_EQ(read_count.load(), num_readers * operations);
}
