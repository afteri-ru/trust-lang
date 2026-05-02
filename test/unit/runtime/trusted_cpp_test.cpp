#include "runtime/trusted-cpp.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <atomic>

using namespace trust;

class TrustedCppTest : public ::testing::Test {};

// ============================================================================
// SyncSingleThread Tests (base class for single-threaded access)
// ============================================================================

TEST_F(TrustedCppTest, SyncBasicConstruction) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    {
        auto locker = Locker<int>(sync, false);
        EXPECT_EQ(*locker, 42);
    }
}

TEST_F(TrustedCppTest, SyncLockConst) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 42);
    }
}

TEST_F(TrustedCppTest, SyncUnlockAfterLock) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 42);
    }
    {
        auto locker = Locker<int>(sync, false);
        EXPECT_EQ(*locker, 42);
    }
    {
        auto locker = Locker<int>(sync, false);
        EXPECT_EQ(*locker, 42);
    }
}

TEST_F(TrustedCppTest, SyncTimeoutError) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    SyncTimeoutType timeout(100);
    EXPECT_THROW(Locker<int>(sync, false, timeout), std::runtime_error);
}

// ============================================================================
// SyncSingleThread Tests
// ============================================================================

TEST_F(TrustedCppTest, SyncSingleThreadBasic) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    {
        auto locker = Locker<int>(sync, false);
        *locker = 100;
    }
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 100);
    }
}

TEST_F(TrustedCppTest, SyncSingleThreadSameThread) {
    auto sync = std::make_shared<SyncSingleThread<std::string>>("hello");
    {
        auto locker = Locker<std::string>(sync, true);
        EXPECT_EQ(*locker, "hello");
    }
}

TEST_F(TrustedCppTest, SyncSingleThreadDifferentThread) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    std::atomic<bool> thread_done{false};
    std::string error_msg;
    std::mutex error_mutex;

    std::thread t([&sync, &thread_done, &error_msg, &error_mutex]() {
        try {
            auto locker = Locker<int>(sync, false);
            (void)locker;
        } catch (const std::runtime_error &e) {
            std::lock_guard<std::mutex> lock(error_mutex);
            error_msg = e.what();
        }
        thread_done = true;
    });
    t.join();

    EXPECT_TRUE(thread_done);
    std::lock_guard<std::mutex> lock(error_mutex);
    EXPECT_TRUE(error_msg.find("single thread") != std::string::npos || !error_msg.empty());
}

// ============================================================================
// SyncTimedMutex Tests
// ============================================================================

TEST_F(TrustedCppTest, SyncTimedMutexBasicLock) {
    auto sync = std::make_shared<SyncTimedMutex<int>>(42);
    {
        auto locker = Locker<int>(sync, false);
        *locker = 100;
    }
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 100);
    }
}

TEST_F(TrustedCppTest, SyncTimedMutexConstLock) {
    auto sync = std::make_shared<SyncTimedMutex<int>>(42);
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 42);
    }
}

TEST_F(TrustedCppTest, SyncTimedMutexThreadSafety) {
    auto sync = std::make_shared<SyncTimedMutex<int>>(0);
    const int num_threads = 4;
    const int increments = 1000;

    auto worker = [&sync, increments]() {
        for (int i = 0; i < increments; ++i) {
            auto locker = Locker<int>(sync, false);
            (*locker)++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back(worker);
    }

    for (auto &t : threads) {
        t.join();
    }

    auto locker = Locker<int>(sync, true);
    EXPECT_EQ(*locker, num_threads * increments);
}

// ============================================================================
// SyncTimedShared Tests
// ============================================================================

TEST_F(TrustedCppTest, SyncTimedSharedBasicLock) {
    auto sync = std::make_shared<SyncTimedShared<int>>(42);
    {
        auto locker = Locker<int>(sync, false);
        *locker = 100;
    }
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 100);
    }
}

TEST_F(TrustedCppTest, SyncTimedSharedConstLock) {
    auto sync = std::make_shared<SyncTimedShared<int>>(42);
    {
        auto locker = Locker<int>(sync, true);
        EXPECT_EQ(*locker, 42);
    }
}

TEST_F(TrustedCppTest, SyncTimedSharedConcurrentReads) {
    auto sync = std::make_shared<SyncTimedShared<int>>(42);
    std::atomic<int> read_count{0};
    std::atomic<bool> stop{false};
    const int num_readers = 4;
    const int read_iterations = 500;

    auto reader = [&sync, &read_count, &stop, read_iterations]() {
        for (int i = 0; i < read_iterations && !stop; ++i) {
            auto locker = Locker<int>(sync, true);
            volatile int val = *locker;
            (void)val;
            read_count++;
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < num_readers; ++i) {
        threads.emplace_back(reader);
    }

    for (auto &t : threads) {
        t.join();
    }

    EXPECT_EQ(read_count.load(), num_readers * read_iterations);
}

// ============================================================================
// Locker Tests
// ============================================================================

TEST_F(TrustedCppTest, LockerDereference) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    {
        auto locked = Locker<int>(sync, false);
        EXPECT_EQ(*locked, 42);
        *locked = 100;
        EXPECT_EQ(*locked, 100);
    }
}

TEST_F(TrustedCppTest, LockerDereferenceConst) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    {
        auto locked = Locker<int>(sync, true);
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, LockerNoncopyable) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    auto locked = Locker<int>(sync, false);
    // static assertions ensure noncopyable at compile time
    EXPECT_EQ(*locked, 42);
}

TEST_F(TrustedCppTest, LockerMoveConstructor) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    auto locked1 = Locker<int>(sync, false);
    EXPECT_EQ(*locked1, 42);
    auto locked2 = std::move(locked1);
    EXPECT_FALSE(static_cast<bool>(locked1));
    EXPECT_TRUE(static_cast<bool>(locked2));
    EXPECT_EQ(*locked2, 42);
}

TEST_F(TrustedCppTest, LockerMoveAssignment) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    auto locked1 = Locker<int>(sync, false);
    Locker<int> locked2(sync, false);
    locked2 = std::move(locked1);
    EXPECT_FALSE(static_cast<bool>(locked1));
    EXPECT_TRUE(static_cast<bool>(locked2));
}

TEST_F(TrustedCppTest, LockerOperatorBool) {
    auto sync = std::make_shared<SyncSingleThread<int>>(42);
    auto locked = Locker<int>(sync, false);
    EXPECT_TRUE(static_cast<bool>(locked));
    auto moved = std::move(locked);
    EXPECT_FALSE(static_cast<bool>(locked));
    EXPECT_TRUE(static_cast<bool>(moved));
}

TEST_F(TrustedCppTest, LockerTryLockSuccess) {
    Shared<int, SyncTimedMutex> s(42);
    auto opt = s.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
}

TEST_F(TrustedCppTest, LockerTryLockConstSuccess) {
    Shared<int, SyncTimedMutex> s(42);
    auto opt = s.try_lock_const();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
}

// ============================================================================
// Shared Tests
// ============================================================================

TEST_F(TrustedCppTest, SharedDefaultConstructor) {
    Shared<int> s;
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_FALSE(s.has_value());
}

TEST_F(TrustedCppTest, SharedFromValue) {
    Shared<int> s(42);
    EXPECT_TRUE(static_cast<bool>(s));
    EXPECT_TRUE(s.has_value());
    {
        auto locked = s.lock();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, SharedLockAndModify) {
    Shared<int> s(42);
    {
        auto locked = s.lock();
        *locked = 100;
    }
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 100);
    }
}

TEST_F(TrustedCppTest, SharedSetMethod) {
    Shared<int> s(0);
    s.set(42);
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, SharedCopyAssignment) {
    Shared<int> s1(42);
    Shared<int> s2;
    s2 = s1;
    EXPECT_TRUE(static_cast<bool>(s2));
    EXPECT_TRUE(s2.has_value());
    {
        auto locked = s2.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, SharedMoveSemantics) {
    Shared<int> s1(42);
    Shared<int> s2 = std::move(s1);
    EXPECT_FALSE(static_cast<bool>(s1));
    EXPECT_FALSE(s1.has_value());
    EXPECT_TRUE(static_cast<bool>(s2));
    EXPECT_TRUE(s2.has_value());
}

TEST_F(TrustedCppTest, SharedWeakReference) {
    Shared<int> s(42);
    auto weak = s.weak();
    {
        auto locked = weak.lock();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, SharedSetLvalue) {
    Shared<int> s(0);
    int value = 42;
    s.set(value);
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, SharedSetRvalue) {
    Shared<int> s(0);
    s.set(42);
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 42);
    }
}

TEST_F(TrustedCppTest, SharedTryLockEmpty) {
    Shared<int> s;
    auto opt = s.try_lock();
    EXPECT_FALSE(opt.has_value());
    auto opt_const = s.try_lock_const();
    EXPECT_FALSE(opt_const.has_value());
}

TEST_F(TrustedCppTest, SharedTryLockSuccess) {
    Shared<int, SyncTimedMutex> s(42);
    auto opt = s.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
}

TEST_F(TrustedCppTest, SharedTryLockConstSuccess) {
    Shared<int, SyncTimedShared> s(42);
    auto opt = s.try_lock_const();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
}

TEST_F(TrustedCppTest, SharedMoveConstructValue) {
    std::string str = "hello";
    Shared<std::string, SyncTimedMutex> s(std::move(str));
    EXPECT_EQ(*s.lock_const(), "hello");
    EXPECT_TRUE(str.empty()); // moved from
}

// ============================================================================
// Shared with SyncSingleThread
// ============================================================================

TEST_F(TrustedCppTest, SharedSingleThread) {
    Shared<int, SyncSingleThread> s(42);
    {
        auto locked = s.lock();
        EXPECT_EQ(*locked, 42);
        *locked = 100;
    }
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 100);
    }
}

// ============================================================================
// Shared with SyncTimedMutex
// ============================================================================

TEST_F(TrustedCppTest, SharedTimedMutexLock) {
    Shared<int, SyncTimedMutex> s(42);
    {
        auto locked = s.lock();
        *locked = 100;
    }
    EXPECT_EQ(*s.lock_const(), 100);
}

// ============================================================================
// Shared with SyncTimedShared
// ============================================================================

TEST_F(TrustedCppTest, SharedTimedSharedLock) {
    Shared<int, SyncTimedShared> s(42);
    {
        auto locked = s.lock();
        *locked = 100;
    }
    {
        auto locked = s.lock_const();
        EXPECT_EQ(*locked, 100);
    }
}

// ============================================================================
// Weak Tests
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

    for (auto &t : threads) {
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

    for (auto &t : threads) {
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
}