#include "runtime/trusted_cpp_test_fixture.hpp"

#include <string>
#include <type_traits>
#include <utility>

using namespace trust;

// ============================================================================
// L2 contract conformance (see types/REFType.md §10): compile-time checks that
// every memory-model type satisfies the fixed structural contract.
// ============================================================================

// Access guards: Locker is the only guard kind (unique is monopolistic, no guard).
static_assert(AccessGuard<Locker<int, false>>);
static_assert(AccessGuard<Locker<int, true>>);

// Owning references with direct access.
static_assert(UniqueRef<Unique<int>>);
static_assert(UniqueRef<Unique<std::string>>);
static_assert(UniqueRef<Shared<int>>);
static_assert(UniqueRef<Shared<std::string>>);

// Shared (refcounted) references (direct access NOT required -> AccessShared qualifies).
static_assert(SharedRef<Shared<int>>);
static_assert(SharedRef<AccessShared<int>>);

// Borrowable references (escapable observer borrow: Shared -> SharedBorrowedRef; unique is monopolistic).
static_assert(BorrowableRef<Shared<int>>);
static_assert(!BorrowableRef<Unique<int>>);

// Weak (non-owning) references.
static_assert(WeakRef<Weak<Shared<int>>>);
static_assert(WeakRef<Weak<AccessShared<int>>>);

// Negative conformance: kinds do not accidentally satisfy the wrong contract.
static_assert(!AccessGuard<int>);
static_assert(!AccessGuard<Unique<int>>);         // owner, not a guard
static_assert(!AccessGuard<Shared<int>>);         // owner, not a guard
static_assert(!UniqueRef<Locker<int, false>>);    // guard has no get()/reset()
static_assert(!UniqueRef<AccessShared<int>>);     // guarded-only access by design
static_assert(!SharedRef<Unique<int>>);           // exclusive: not copyable, no weak observer
static_assert(!SharedRef<Weak<Shared<int>>>);     // weak observer, not an owner
static_assert(!BorrowableRef<Weak<Shared<int>>>); // weak observer, not a borrowable owner
static_assert(!BorrowableRef<AccessShared<int>>); // no borrow() on the sync backend
static_assert(!WeakRef<Locker<int, false>>);      // guard is not a weak observer

// ============================================================================
// Generic algorithms constrained by the L2 contract compile for all guards/owners.
// ============================================================================

namespace {
template <typename Guard>
    requires AccessGuard<Guard>
int readViaGuard(Guard& g) {
    return *g;
}

template <typename Owner>
    requires UniqueRef<Owner>
int readUnique(Owner& o) {
    return *o;
}
} // namespace

TEST_F(TrustedCppTest, ConceptsAccessGuardRuntime) {
    Unique<int> b(11);
    EXPECT_EQ(*b, 11); // monopolistic owner: direct access, no guard
    Shared<int> s(22);
    {
        auto g = s.lock();
        EXPECT_EQ(readViaGuard(g), 22);
    }
    {
        auto g = s.borrow().access();
        EXPECT_EQ(readViaGuard(g), 22);
    }
}

TEST_F(TrustedCppTest, ConceptsUniqueRefRuntime) {
    Unique<int> b(5);
    EXPECT_EQ(readUnique(b), 5);
    Shared<int> s(7);
    EXPECT_EQ(readUnique(s), 7);
}
