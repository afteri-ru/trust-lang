#include "runtime/trusted_cpp_test_fixture.hpp"

using namespace trust;
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
        } catch (const std::runtime_error& e) {
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

    for (auto& t : threads) {
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

    for (auto& t : threads) {
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
