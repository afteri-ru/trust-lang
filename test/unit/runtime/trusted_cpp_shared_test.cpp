#include "runtime/trusted_cpp_test_fixture.hpp"

using namespace trust;
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
