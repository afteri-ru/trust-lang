#include "runtime/trusted_cpp_test_fixture.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <utility>

using namespace trust;

// ============================================================================
// Compile-time checks: exclusivity, non-copyability, unified guard interface
// ============================================================================

static_assert(!std::is_copy_constructible_v<Unique<int>>, "Unique must be non-copyable");
static_assert(!std::is_copy_assignable_v<Unique<int>>, "Unique must be non-copy-assignable");
static_assert(std::is_move_constructible_v<Unique<int>>, "Unique must be movable");
static_assert(std::is_move_assignable_v<Unique<int>>, "Unique must be move-assignable");

// ESCAPABLE observer borrowed reference over Shared: copyable + movable (may leave the scope).
static_assert(std::is_copy_constructible_v<SharedBorrowedRef<int, false>>, "SharedBorrowedRef must be copyable (escapable)");
static_assert(std::is_move_constructible_v<SharedBorrowedRef<int, false>>, "SharedBorrowedRef must be movable");

// Access guards (Locker) expose a common public interface via detail::GuardBase:
// operator*, get(), operator->, operator bool.
template <typename G>
constexpr bool isMutableAccessGuard() {
    return std::is_same_v<decltype(*std::declval<G&>()), int&> && std::is_same_v<decltype(std::declval<G&>().get()), int&> &&
           std::is_same_v<decltype(std::declval<G&>().operator->()), int*> && std::is_same_v<decltype(static_cast<bool>(std::declval<G&>())), bool>;
}
template <typename G>
constexpr bool isConstAccessGuard() {
    return std::is_same_v<decltype(*std::declval<G&>()), const int&> && std::is_same_v<decltype(std::declval<G&>().get()), const int&> &&
           std::is_same_v<decltype(std::declval<G&>().operator->()), const int*>;
}
static_assert(isMutableAccessGuard<Locker<int, false>>());
static_assert(isConstAccessGuard<Locker<int, true>>());

// ============================================================================
// Unique - construction / access / null contract
// ============================================================================

TEST_F(TrustedCppTest, UniqueConstructAndDirectAccess) {
    Unique<int> b(5);
    EXPECT_TRUE(static_cast<bool>(b));
    EXPECT_TRUE(b.has_value());
    EXPECT_EQ(*b, 5);
    *b = 7;
    EXPECT_EQ(*b, 7);
    EXPECT_EQ(*b.operator->(), 7);
    ASSERT_NE(b.get(), nullptr);
    EXPECT_EQ(*b.get(), 7);
}

TEST_F(TrustedCppTest, UniqueConstAccess) {
    const Unique<int> b(5);
    EXPECT_EQ(*b, 5);
    EXPECT_EQ(*b.operator->(), 5);
}

TEST_F(TrustedCppTest, UniqueDefaultIsEmpty) {
    Unique<int> b;
    EXPECT_FALSE(static_cast<bool>(b));
    EXPECT_FALSE(b.has_value());
    EXPECT_EQ(b.get(), nullptr);
    EXPECT_THROW(*b, std::runtime_error);
}

TEST_F(TrustedCppTest, UniqueFromUniquePtr) {
    auto raw = std::make_unique<int>(42);
    int* rawPtr = raw.get();
    Unique<int> b(std::move(raw));
    EXPECT_EQ(b.get(), rawPtr); // identity is preserved (no value move)
    EXPECT_EQ(*b, 42);
}

TEST_F(TrustedCppTest, UniqueWithMovableValue) {
    Unique<std::string> b("hello");
    EXPECT_EQ(*b, "hello");
    b->append(" world");
    EXPECT_EQ(*b, "hello world");
}

// ============================================================================
// Unique - permanent transfer: move / swap
// ============================================================================

TEST_F(TrustedCppTest, UniqueMoveConstructor) {
    Unique<int> b1(42);
    Unique<int> b2(std::move(b1));
    EXPECT_FALSE(static_cast<bool>(b1)); // old owner permanently lost ownership
    EXPECT_TRUE(static_cast<bool>(b2));
    EXPECT_EQ(*b2, 42);
}

TEST_F(TrustedCppTest, UniqueMoveAssignment) {
    Unique<int> b1(42);
    Unique<int> b2(7);
    b2 = std::move(b1);
    EXPECT_FALSE(static_cast<bool>(b1));
    EXPECT_EQ(*b2, 42);
}

TEST_F(TrustedCppTest, UniqueSwapMemberAndStd) {
    Unique<int> a(1);
    Unique<int> b(2);
    a.swap(b);
    EXPECT_EQ(*a, 2);
    EXPECT_EQ(*b, 1);
    std::swap(a, b);
    EXPECT_EQ(*a, 1);
    EXPECT_EQ(*b, 2);
}

TEST_F(TrustedCppTest, UniqueResetAndRelease) {
    Unique<int> b(5);
    b.reset();
    EXPECT_FALSE(static_cast<bool>(b));

    Unique<int> c(9);
    auto u = c.release();
    EXPECT_FALSE(static_cast<bool>(c));
    ASSERT_NE(u, nullptr);
    EXPECT_EQ(*u, 9);
}

// ============================================================================
// Unique -> Shared (PROMOTE): shares ownership, allocates a control block
// ============================================================================

TEST_F(TrustedCppTest, UniqueToSharedViaToShared) {
    Unique<int> b(42);
    Shared<int> s = std::move(b).to_shared();
    EXPECT_FALSE(static_cast<bool>(b)); // source empty (exclusive ownership transferred)
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(*s, 42);
    // Observer borrow does NOT require sole ownership and does NOT empty the source.
    {
        auto r = s.borrow();
        EXPECT_EQ(*r.access(), 42);
    }
    EXPECT_EQ(*s, 42);
}

TEST_F(TrustedCppTest, UniqueToSharedViaConstructor) {
    Unique<int> b(7);
    Shared<int> s(std::move(b));
    EXPECT_FALSE(static_cast<bool>(b));
    EXPECT_EQ(*s, 7);
}

// ============================================================================
// Shared -> Unique (DEMOTE): use_count()==1 + value move (identity changes)
// ============================================================================

TEST_F(TrustedCppTest, SharedToUniqueViaToUnique) {
    Shared<int> s(42);
    StaticUnique<int> b = std::move(s).to_unique();
    EXPECT_FALSE(s.has_value()); // source empty
    EXPECT_EQ(*b, 42);
}

TEST_F(TrustedCppTest, SharedToUniqueChangesIdentity) {
    Shared<int> s(5);
    int* before = &s.get();
    StaticUnique<int> b = std::move(s).to_unique();
    EXPECT_NE(&b.get(), before); // value was moved into a new object
    EXPECT_EQ(*b, 5);
}

TEST_F(TrustedCppTest, SharedToUniqueWithOtherOwnerThrows) {
    Shared<int> s(42);
    Shared<int> s2 = s; // use_count == 2
    EXPECT_THROW(std::move(s).to_unique(), std::runtime_error);
    EXPECT_TRUE(s.has_value()); // unchanged on failure
}

TEST_F(TrustedCppTest, SharedToUniqueOnNullThrows) {
    Shared<int> s;
    EXPECT_THROW(std::move(s).to_unique(), std::runtime_error);
}

// ============================================================================
// Shared -> Borrow (observer, SharedBorrowedRef): non-owning, escapable, expires
// ============================================================================

TEST_F(TrustedCppTest, SharedBorrowObserverDoesNotTransferOwnership) {
    Shared<int> s(42);
    auto r = s.borrow();
    EXPECT_TRUE(r.valid());
    EXPECT_TRUE(s.has_value()); // source is NOT emptied
    {
        auto g = r.access();
        EXPECT_EQ(*g, 42);
        *g = 100;
    }
    EXPECT_TRUE(s.has_value());
    EXPECT_EQ(*s, 100);
    EXPECT_EQ(s.use_count(), 1L);
}

TEST_F(TrustedCppTest, SharedBorrowConstIsReadOnly) {
    Shared<int> s(42);
    auto r = s.borrow_const();
    auto g = r.access();
    static_assert(std::is_same_v<decltype(*g), const int&>);
    EXPECT_EQ(*g, 42);
    EXPECT_EQ(*s, 42);
}

TEST_F(TrustedCppTest, SharedBorrowDoesNotRequireSoleOwner) {
    Shared<int> s(42);
    Shared<int> s2 = s;  // use_count == 2
    auto r = s.borrow(); // observer: allowed with other owners
    EXPECT_TRUE(r.valid());
    EXPECT_EQ(*r.access(), 42);
    EXPECT_EQ(s.use_count(), 2L); // observer does NOT own
}

TEST_F(TrustedCppTest, SharedBorrowOnNullThrows) {
    Shared<int> s;
    EXPECT_THROW(s.borrow(), std::runtime_error);
    EXPECT_THROW(s.borrow_const(), std::runtime_error);
}

TEST_F(TrustedCppTest, SharedBorrowEscapesAndExpiresAfterLastOwner) {
    SharedBorrowedRef<int, false> r;
    {
        Shared<int> s(42);
        r = s.borrow();
        EXPECT_TRUE(r.valid());
    } // last strong owner is gone
    EXPECT_FALSE(r.valid());
    EXPECT_THROW(r.access(), std::runtime_error);
}

TEST_F(TrustedCppTest, SharedBorrowStaysValidWhileAnyOwnerAlive) {
    Shared<int> owner(5);
    SharedBorrowedRef<int, false> r;
    {
        Shared<int> temp = owner; // use_count == 2
        r = temp.borrow();
    } // temp gone, owner still holds the object
    EXPECT_TRUE(r.valid());
    {
        auto g = r.access();
        EXPECT_EQ(*g, 5);
    }
}

// ============================================================================
// Combined conversions + unified guard interface
// ============================================================================

TEST_F(TrustedCppTest, PromoteThenBorrow) {
    Unique<int> b(5);
    Shared<int> s = std::move(b).to_shared();
    {
        auto g = s.borrow().access();
        *g = 6;
    }
    EXPECT_EQ(*s, 6);
}

TEST_F(TrustedCppTest, DemoteThenDirectAccess) {
    Shared<int> s(5);
    StaticUnique<int> b = std::move(s).to_unique();
    *b = 8;
    EXPECT_EQ(*b, 8);
}

TEST_F(TrustedCppTest, MakeUniqueFactory) {
    auto i = make_unique<int>(42);
    EXPECT_EQ(*i, 42);
    auto str = make_unique<std::string>("hi");
    EXPECT_EQ(*str, "hi");
}

namespace {
// Reads a value through ANY guard type that shares the detail::GuardBase interface.
template <typename Guard>
int readThroughGuard(Guard& g) {
    return *g;
}
} // namespace

TEST_F(TrustedCppTest, UnifiedGuardInterfaceAtRuntime) {
    Unique<int> b(11);
    EXPECT_EQ(*b, 11); // Unique has NO guard - direct access (monopolistic)
    Shared<int> s(22);
    {
        auto g = s.borrow().access();
        EXPECT_EQ(readThroughGuard(g), 22);
    }
    Shared<int> s2(33);
    {
        auto g = s2.lock(); // shared capture
        EXPECT_EQ(readThroughGuard(g), 33);
    }
}
