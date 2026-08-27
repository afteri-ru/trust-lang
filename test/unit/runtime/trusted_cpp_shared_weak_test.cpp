#include "runtime/trusted_cpp_test_fixture.hpp"

using namespace trust;
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
        EXPECT_TRUE(w.has_value());
    }
    EXPECT_FALSE(static_cast<bool>(w));
    EXPECT_FALSE(w.has_value());
}

TEST_F(TrustedCppTest, WeakConstLock) {
    Shared<int> s(42);
    Weak<Shared<int>> w = s.weak();
    {
        auto locked = w.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakSetMethod) {
    Shared<int> s(0);
    Weak<Shared<int>> w = s.weak();
    w.set(42);
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakDereferenceOperator) {
    Shared<int> s(42);
    Weak<Shared<int>> w = s.weak();
    {
        auto locked = w.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakWithStdStringLock) {
    Shared<std::string> s("test");
    Weak<Shared<std::string>> w = s.weak();
    {
        auto locked = w.lock();
        EXPECT_EQ(*locked, "test");
    }
}

TEST_F(TrustedCppTest, WeakSetLvalue) {
    Shared<int> s(0);
    Weak<Shared<int>> w = s.weak();
    int value = 42;
    w.set(value);
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakSetRvalue) {
    Shared<int> s(0);
    Weak<Shared<int>> w = s.weak();
    w.set(42);
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, WeakTryLockSuccess) {
    Shared<int, SyncTimedMutex> s(42);
    Weak<Shared<int, SyncTimedMutex>> w = s.weak();
    auto opt = w.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
}

TEST_F(TrustedCppTest, WeakTryLockConstSuccess) {
    Shared<int, SyncTimedShared> s(42);
    Weak<Shared<int, SyncTimedShared>> w = s.weak();
    auto opt = w.try_lock_const();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
}

TEST_F(TrustedCppTest, WeakTryLockOnExpired) {
    Weak<Shared<int>> w;
    {
        Shared<int> s(42);
        w = s.weak();
    }
    auto opt = w.try_lock();
    EXPECT_FALSE(opt.has_value());
    auto opt_const = w.try_lock_const();
    EXPECT_FALSE(opt_const.has_value());
}

TEST_F(TrustedCppTest, WeakDefaultTryLock) {
    Weak<Shared<int>> w;
    auto opt = w.try_lock();
    EXPECT_FALSE(opt.has_value());
}

// ============================================================================
// Shared Thread Safety Tests
// ============================================================================

TEST_F(TrustedCppTest, SharedThreadSafetyTimedMutex) {
    Shared<int, SyncTimedMutex> s(0);
    const int num_threads = 4;
    const int increments = 1000;

    auto worker = [&s, increments]() {
        for (int i = 0; i < increments; ++i) {
            auto locked = s.lock();
            (*locked)++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(*s.lock_const(), num_threads * increments);
}

TEST_F(TrustedCppTest, SharedThreadSafetyTimedShared) {
    Shared<int, SyncTimedShared> s(0);
    const int num_writers = 2;
    const int num_readers = 4;
    const int operations = 500;
    std::atomic<int> read_count{0};
    std::atomic<int> write_count{0};

    auto writer = [&s, operations, &write_count]() {
        for (int i = 0; i < operations; ++i) {
            auto locked = s.lock();
            (*locked)++;
            write_count++;
        }
    };

    auto reader = [&s, operations, &read_count]() {
        for (int i = 0; i < operations; ++i) {
            auto locked = s.lock_const();
            volatile int val = *locked;
            (void)val;
            read_count++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_writers; ++i) {
        threads.emplace_back(writer);
    }
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back(reader);
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(write_count.load(), num_writers * operations);
    EXPECT_EQ(read_count.load(), num_readers * operations);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(TrustedCppTest, SharedNullPointerAccess) {
    Shared<int> s;
    EXPECT_THROW(s.lock(), std::runtime_error);
}

TEST_F(TrustedCppTest, WeakLockOnExpired) {
    Weak<Shared<int>> w;
    {
        Shared<int> s(42);
        w = s.weak();
    }
    EXPECT_THROW(w.lock(), std::runtime_error);
}

TEST_F(TrustedCppTest, SharedWithStdString) {
    Shared<std::string, SyncTimedMutex> s("hello");
    {
        auto locked = s.lock();
        *locked += " world";
    }
    EXPECT_EQ(*s.lock_const(), "hello world");
}

TEST_F(TrustedCppTest, SharedWithVector) {
    Shared<std::vector<int>, SyncTimedShared> s({1, 2, 3});
    {
        auto locked = s.lock();
        (*locked).push_back(4);
    }
    auto locked = s.lock_const();
    EXPECT_EQ((*locked).size(), 4u);
    EXPECT_EQ((*locked).back(), 4);
}

// ============================================================================
// Template Instantiation Tests
// ============================================================================

TEST_F(TrustedCppTest, SharedWithDouble) {
    Shared<double, SyncTimedMutex> s(3.14);
    {
        auto locked = s.lock();
        *locked *= 2.0;
    }
    EXPECT_NEAR(*s.lock_const(), 6.28, 1e-10);
}

TEST_F(TrustedCppTest, SharedWithBool) {
    Shared<bool, SyncTimedMutex> s(true);
    {
        auto locked = s.lock();
        *locked = false;
    }
    EXPECT_FALSE(*s.lock_const());
}

TEST_F(TrustedCppTest, WeakWithStdString) {
    Shared<std::string> s("test");
    Weak<Shared<std::string>> w = s.weak();
    {
        auto locked = w.lock();
        EXPECT_EQ(*locked, "test");
    }
}

// ============================================================================
// Weak Move Semantics Tests
// ============================================================================

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

// ============================================================================
// Locker Noncopyable/Nonmovable Tests
// ============================================================================

TEST_F(TrustedCppTest, LockerNoncopyableStandalone) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    auto locked = Locker<int>(sync, false);
    EXPECT_EQ(*locked, 42);
}

// ============================================================================
// Expired Weak operator bool
// ============================================================================

TEST_F(TrustedCppTest, WeakOperatorBoolOnExpired) {
    Weak<Shared<int>> w;
    {
        Shared<int> s(42);
        w = s.weak();
        EXPECT_TRUE(static_cast<bool>(w));
    }
    EXPECT_FALSE(static_cast<bool>(w));
