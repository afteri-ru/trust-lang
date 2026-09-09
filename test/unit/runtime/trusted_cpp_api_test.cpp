#include "runtime/trusted_cpp_test_fixture.hpp"

#include <functional>
#include <string>
#include <type_traits>
#include <unordered_set>

using namespace trust;

// ============================================================================
// C7: identity comparison / owner_before / std::hash
// ============================================================================

TEST_F(TrustedCppTest, SharedIdentityEquality) {
    Shared<int> a(1);
    Shared<int> b = a; // same object
    Shared<int> c(1);  // different object, same value
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_TRUE(a != c);
    EXPECT_EQ(a.use_count(), 2);
}

TEST_F(TrustedCppTest, UniqueIdentityEquality) {
    Unique<int> a(1);
    Unique<int> b(1);
    EXPECT_TRUE(a == a);
    EXPECT_FALSE(a == b);
}

TEST_F(TrustedCppTest, SharedOwnerBeforeIsStrictOrder) {
    Shared<int> a(1);
    Shared<int> b(2);
    EXPECT_NE(a.owner_before(b), b.owner_before(a));
    EXPECT_FALSE(a.owner_before(a));
}

TEST_F(TrustedCppTest, SharedHashUsableInUnorderedSet) {
    Shared<int> a(1);
    Shared<int> b = a;
    std::unordered_set<Shared<int>> set;
    set.insert(a);
    set.insert(b); // same identity -> no new element
    EXPECT_EQ(set.size(), 1u);
    EXPECT_EQ(set.count(a), 1u);
}

// ============================================================================
// C6: clone / copy-on-write detach
// ============================================================================

TEST_F(TrustedCppTest, CloneSharedIsDeepCopy) {
    Shared<std::string> src("hello");
    Shared<std::string> copy = clone(src);
    EXPECT_EQ(*copy, "hello");
    EXPECT_NE(copy.get_shared().get(), src.get_shared().get()); // distinct objects
    *copy += "!";
    EXPECT_EQ(*src, "hello"); // independent
    EXPECT_EQ(*copy, "hello!");
}

TEST_F(TrustedCppTest, CloneUniqueIsDeepCopy) {
    Unique<int> src(5);
    Unique<int> copy = clone(src);
    EXPECT_EQ(*copy, 5);
    EXPECT_NE(copy.get(), src.get());
    *copy = 6;
    EXPECT_EQ(*src, 5);
}

TEST_F(TrustedCppTest, DetachSoleOwnerIsNoOp) {
    Shared<int> s(5);
    int* before = s.get_shared().get();
    detach(s);
    EXPECT_EQ(s.get_shared().get(), before);
}

TEST_F(TrustedCppTest, DetachSharedOwnerMakesPrivateCopy) {
    Shared<int> a(1);
    Shared<int> b = a; // use_count == 2
    int* before = a.get_shared().get();
    detach(a);
    EXPECT_NE(a.get_shared().get(), before);
    EXPECT_EQ(*a, 1);
    EXPECT_EQ(*b, 1);
    *a = 2;           // mutate the detached copy
    EXPECT_EQ(*b, 1); // the other owner is unaffected
}

// ============================================================================
// C3: const-ownership (Shared<const T> / Unique<const T>) is read-only at the type level
// ============================================================================

TEST_F(TrustedCppTest, ConstSharedProvidesReadOnlyAccess) {
    Shared<const int> s(5);
    {
        auto l = s.lock();
        static_assert(std::is_same_v<decltype(*l), const int&>);
        EXPECT_EQ(*l, 5);
    }
    EXPECT_EQ(*s.lock_const(), 5);
}

TEST_F(TrustedCppTest, ConstUniqueProvidesReadOnlyAccess) {
    Unique<const int> b(7);
    static_assert(std::is_same_v<decltype(*b), const int&>);
    EXPECT_EQ(*b, 7);
}

// ============================================================================
// B1 (shared part): custom deleter / adopt of a non-memory resource
// ============================================================================

TEST_F(TrustedCppTest, AdoptSharedWithCustomDeleter) {
    int released = 0;
    {
        int* raw = new int(5);
        Shared<int> r = Shared<int>::adopt(raw, [&released](int* p) {
            ++released;
            delete p;
        });
        EXPECT_TRUE(r.has_value());
        EXPECT_EQ(*r, 5);
        Shared<int> r2 = r; // shared ownership of the adopted resource
        EXPECT_EQ(r2.use_count(), 2);
    }
    EXPECT_EQ(released, 1); // released exactly once, when the last owner died
}

TEST_F(TrustedCppTest, AdoptSharedNullIsEmpty) {
    int released = 0;
    Shared<int> r = Shared<int>::adopt(nullptr, [&released](int*) { ++released; });
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(released, 0);
}

// ============================================================================
// B1 (unique part): Unique<V, D> — the deleter is PART OF THE TYPE
// ============================================================================

namespace {
struct CountDeleter {
    int* count = nullptr;
    void operator()(int* p) const noexcept {
        if (count != nullptr) {
            ++(*count);
        }
        delete p;
    }
};
} // namespace

TEST_F(TrustedCppTest, UniqueCustomDeleterIsPartOfType) {
    int released = 0;
    {
        Unique<int, CountDeleter> b(std::unique_ptr<int, CountDeleter>(new int(42), CountDeleter{&released}));
        EXPECT_EQ(*b, 42);
        *b = 43; // direct access (monopolistic owner, no borrow guard)
        EXPECT_EQ(*b, 43);
    }
    EXPECT_EQ(released, 1); // custom deleter invoked exactly once
}

TEST_F(TrustedCppTest, UniqueAdoptWithCustomDeleter) {
    int released = 0;
    {
        auto b = Unique<int, CountDeleter>::adopt(new int(7), CountDeleter{&released});
        EXPECT_EQ(*b, 7);
    }
    EXPECT_EQ(released, 1);
}

TEST_F(TrustedCppTest, UniqueCustomDeleterPromotesToShared) {
    int released = 0;
    {
        Unique<int, CountDeleter> b(std::unique_ptr<int, CountDeleter>(new int(9), CountDeleter{&released}));
        Shared<int> s = std::move(b).to_shared();
        EXPECT_EQ(*s, 9);
    }
    EXPECT_EQ(released, 1); // shared_ptr preserves the custom deleter
}

// ============================================================================
// C5: aliasing - a Shared to a subobject that keeps the owner alive
// ============================================================================

namespace {
struct Pair {
    int first = 0;
    int second = 0;
};
} // namespace

TEST_F(TrustedCppTest, AliasToMemberKeepsOwnerAlive) {
    Shared<int> member;
    {
        Shared<Pair> owner(Pair{1, 2});
        member = alias(owner, &owner->first);
        EXPECT_EQ(*member, 1);
        *member = 10;
    }
    // The owner is gone, but the alias kept the whole object alive.
    EXPECT_TRUE(member.has_value());
    EXPECT_EQ(*member, 10);
}

TEST_F(TrustedCppTest, AliasEmptyOwnerThrows) {
    Shared<Pair> owner;
    EXPECT_THROW(alias(owner, static_cast<int*>(nullptr)), std::runtime_error);
}
