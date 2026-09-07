#include "runtime/trusted_cpp_test_fixture.hpp"

#include <shared_mutex>
#include <string>
#include <vector>

using namespace trust;

// ============================================================================
// Shared (plain) - construction / state
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
    EXPECT_EQ(*s.lock_const(), 100);
}

TEST_F(TrustedCppTest, SharedSetMethod) {
    Shared<int> s(0);
    s.set(42);
    EXPECT_EQ(*s.lock_const(), 42);
}

TEST_F(TrustedCppTest, SharedCopyAssignment) {
    Shared<int> s1(42);
    Shared<int> s2;
    s2 = s1;
    EXPECT_TRUE(s2.has_value());
    EXPECT_EQ(*s2.lock_const(), 42);
}

TEST_F(TrustedCppTest, SharedMoveSemantics) {
    Shared<int> s1(42);
    Shared<int> s2 = std::move(s1);
    EXPECT_FALSE(s1.has_value());
    EXPECT_TRUE(s2.has_value());
    EXPECT_EQ(*s2, 42);
}

TEST_F(TrustedCppTest, SharedSetLvalueRvalue) {
    Shared<int> s(0);
    int value = 42;
    s.set(value);
    EXPECT_EQ(*s, 42);
    s.set(7);
    EXPECT_EQ(*s, 7);
}

TEST_F(TrustedCppTest, SharedTryLockEmpty) {
    Shared<int> s;
    EXPECT_FALSE(s.try_lock().has_value());
    EXPECT_FALSE(s.try_lock_const().has_value());
}

TEST_F(TrustedCppTest, SharedTryLockSuccess) {
    Shared<int> s(42);
    auto opt = s.try_lock();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(**opt, 42);
    auto opt_c = s.try_lock_const();
    ASSERT_TRUE(opt_c.has_value());
    EXPECT_EQ(**opt_c, 42);
}

TEST_F(TrustedCppTest, SharedMoveConstructValue) {
    std::string str = "hello";
    Shared<std::string> s(std::move(str));
    EXPECT_EQ(*s.lock_const(), "hello");
    EXPECT_TRUE(str.empty()); // moved from
}

TEST_F(TrustedCppTest, SharedReset) {
    Shared<int> s(42);
    EXPECT_TRUE(s.has_value());
    s.reset();
    EXPECT_FALSE(s.has_value());
    EXPECT_FALSE(static_cast<bool>(s));
    EXPECT_FALSE(s.try_lock().has_value());
}

// ============================================================================
// Shared (plain) - direct access
// ============================================================================

TEST_F(TrustedCppTest, SharedWithString) {
    Shared<std::string> s("hello");
    {
        auto locked = s.lock();
        *locked += " world";
    }
    EXPECT_EQ(*s.lock_const(), "hello world");
}

TEST_F(TrustedCppTest, SharedWithVector) {
    Shared<std::vector<int>> s({1, 2, 3});
    {
        auto locked = s.lock();
        (*locked).push_back(4);
    }
    auto locked = s.lock_const();
    EXPECT_EQ((*locked).size(), 4u);
    EXPECT_EQ((*locked).back(), 4);
}

TEST_F(TrustedCppTest, SharedWithDouble) {
    Shared<double> s(3.14);
    {
        auto locked = s.lock();
        *locked *= 2.0;
    }
    EXPECT_NEAR(*s.lock_const(), 6.28, 1e-10);
}

TEST_F(TrustedCppTest, SharedWithBool) {
    Shared<bool> s(true);
    {
        auto locked = s.lock();
        *locked = false;
    }
    EXPECT_FALSE(*s.lock_const());
}

// ============================================================================
// SyncShared (SyncMutexPolicy, default) - Shared-like API, synchronized
// ============================================================================

TEST_F(TrustedCppTest, SyncSharedFromValue) {
    SyncShared<int> s(42);
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(*s.lock_const(), 42);
}

TEST_F(TrustedCppTest, SyncSharedCopyMove) {
    SyncShared<int> s1(42);
    SyncShared<int> s2 = s1;
    EXPECT_TRUE(s2.has_value());
    EXPECT_EQ(*s2.lock_const(), 42);
    SyncShared<int> s3 = std::move(s1);
    EXPECT_FALSE(s1.has_value());
    EXPECT_EQ(*s3.lock_const(), 42);
}

TEST_F(TrustedCppTest, SyncSharedSet) {
    SyncShared<int> s(0);
    s.set(42);
    EXPECT_EQ(*s.lock_const(), 42);
}

TEST_F(TrustedCppTest, SyncSharedReset) {
    SyncShared<int> s(42);
    s.reset();
    EXPECT_FALSE(s.has_value());
    EXPECT_FALSE(s.try_lock().has_value());
}

TEST_F(TrustedCppTest, SyncSharedWithString) {
    SyncShared<std::string> s("hello");
    {
        auto locked = s.lock();
        *locked += " world";
    }
    EXPECT_EQ(*s.lock_const(), "hello world");
}
