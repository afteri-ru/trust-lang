#include "runtime/trusted_cpp_test_fixture.hpp"

#include <stdexcept>

using namespace trust;

// ============================================================================
// Self-reference mixin (K1 in types/REFType.md §8): EnableSharedFromThis<V>
// ============================================================================

namespace {
struct Node : EnableSharedFromThis<Node> {
    int value = 0;
    Node() = default;
    explicit Node(int v)
    : value(v) {}
};
} // namespace

TEST_F(TrustedCppTest, SharedFromThisReturnsSelfHeldShared) {
    Shared<Node> n(Node(5));
    auto self = n.get_shared()->shared_from_this();
    EXPECT_TRUE(self.has_value());
    EXPECT_EQ(self->value, 5);
}

TEST_F(TrustedCppTest, SharedFromThisKeepsObjectAlive) {
    Shared<Node> self;
    {
        Shared<Node> n(Node(7));
        self = n.get_shared()->shared_from_this();
    }
    // The original owner is gone, but the self-reference keeps the object alive.
    EXPECT_TRUE(self.has_value());
    EXPECT_EQ(self->value, 7);
}

TEST_F(TrustedCppTest, SharedFromThisWithoutOwnerThrows) {
    Node stack(1);
    EXPECT_THROW(stack.shared_from_this(), std::runtime_error);
}

TEST_F(TrustedCppTest, WeakFromThisExpiresWithOwner) {
    Weak<Shared<Node>> w;
    {
        Shared<Node> n(Node(3));
        w = n.get_shared()->weak_from_this();
        EXPECT_TRUE(w.has_value());
    }
    EXPECT_FALSE(w.has_value());
    EXPECT_THROW(w.lock(), std::runtime_error);
}

TEST_F(TrustedCppTest, WeakFromThisWithoutOwnerIsEmpty) {
    Node stack(1);
    auto w = stack.weak_from_this();
    EXPECT_FALSE(w.has_value());
}

TEST_F(TrustedCppTest, CopyDoesNotInheritSelfReference) {
    Shared<Node> n(Node(4));
    Node copy = *n; // copies the object, not the owner link
    EXPECT_THROW(copy.shared_from_this(), std::runtime_error);
}

TEST_F(TrustedCppTest, PromoteUniqueToSharedAttachesSelfReference) {
    Unique<Node> b(Node(6));
    Shared<Node> s = std::move(b).to_shared();
    auto self = s.get_shared()->shared_from_this();
    EXPECT_EQ(self->value, 6);
}
