// trust/trusted-cpp.hpp - base reference types of the runtime (Shared/Weak/Locker), PLAIN variant
// WITHOUT inter-thread synchronization.
//
// Public runtime header: self-contained (standard headers only), so generated C++ programs can
// include it without the compiler's include tree. At build time it is embedded into trust-runtime
// (via #embed, ELF section "trust/trusted-cpp.hpp"); the pipeline extracts it into a temporary
// trust/ directory when a program actually uses reference types (Shared/Weak) or the capture
// operators `with` / `*` (TAKE).
//
// This header is the PLAIN (single-threaded, always used) variant: no <thread>, <mutex>,
// <shared_mutex>, <chrono>. It models a strong reference (Shared), a weak reference (Weak), the
// RAII access guard (Locker) for a reference that is ALWAYS present when a reference exists, the
// exclusive owner (Unique) - MONOPOLISTIC (move/swap only; no borrow/aliasing form).
// Multi-threaded synchronization lives in a SEPARATE header `trust/trusted-cpp-sync.hpp`
// (AccessShared<V, Policy>), included ONLY when cross-thread synchronization is required.
//
// Locker is policy-agnostic: it does NOT know and must NOT know the synchronization method
// (mutex / shared_mutex / timeout). The sync policy belongs to the owning reference type; Locker
// only provides guarded scoped access (const-correctness) and, in the sync variant, an opaque
// release handle. Raw references/pointers and unique_ptr do NOT use Locker - that is a different
// ideology (direct access, no guard).
//
// Errors are reported with standard exceptions (trust::IntMinus, inherits std::runtime_error),
// like trust/dict.hpp - the header does not depend on the compiler-internal FAULT/EXPECT mechanism.

#ifndef TRUST_TRUSTED_CPP_HPP
#define TRUST_TRUSTED_CPP_HPP

#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "trust/interrupt.hpp" // IntMinus: сбой захвата - встроенная ошибка языка (ловится {- ... -} / catch(IntMinus&))

namespace trust {

// NOTE: `unique` is MONOPOLISTIC - it has NO access policy and NO borrow/aliasing form. The only
// operations are move/swap (permanent transfer of ownership); access to the owned value is direct
// (`operator*` / `checked_deref`). Only `shared`/`weak` carry a synchronization policy
// (`AccessMutex`/`AccessRwMutex`/`AccessSingleThread`, defined in trust/trusted-cpp-sync.hpp).

namespace detail {
// Unified error diagnostic. Self-contained: throws trust::IntMinus (inherits std::runtime_error),
// so a capture failure is a BUILT-IN language error - caught by trust block {- ... -} and by C++
// (catch(std::runtime_error&) / catch(IntMinus&)). Dereferencing a null reference (lock/take of
// nullptr) is a contract violation -> this exception.
[[noreturn]] inline void trusted_cpp_error(const std::string& msg) {
    throw trust::IntMinus(msg);
}

/**
 * Opaque release handle for a Locker. Locker is policy-agnostic: it only knows that on destruction
 * it must call unlock(); the concrete synchronization (which mutex, shared vs exclusive, timeout)
 * is implemented in the sync variant (`trust/trusted-cpp-sync.hpp`), NOT here. In the plain variant
 * a Locker holds a null release_ (there is nothing to release).
 */
class LockRelease {
  public:
    virtual ~LockRelease() = default;
    virtual void unlock() noexcept = 0;
};

/**
 * CRTP base providing the COMMON public interface of access guards (Locker): operator*,
 * operator->, get(), operator bool. The derived guard must provide
 * `V* data() noexcept` and `const V* data() const noexcept`. Non-copyable, movable.
 * ReadOnly is a compile-time access flag: mutable access (false) vs const access (true).
 */
template <typename V, bool ReadOnly, typename Derived>
class GuardBase {
  public:
    using ValueType = V;
    using DataAccess = std::conditional_t<ReadOnly, const V&, V&>;
    using PointerAccess = std::conditional_t<ReadOnly, const V*, V*>;

    GuardBase() = default;
    GuardBase(const GuardBase&) = delete;
    GuardBase& operator=(const GuardBase&) = delete;
    GuardBase(GuardBase&&) noexcept = default;
    GuardBase& operator=(GuardBase&&) noexcept = default;
    ~GuardBase() = default;

    [[nodiscard]] DataAccess operator*() { return *self().data(); }
    [[nodiscard]] const V& operator*() const { return *self().data(); }
    [[nodiscard]] PointerAccess operator->() { return self().data(); }
    [[nodiscard]] const V* operator->() const { return self().data(); }
    [[nodiscard]] DataAccess get() { return *self().data(); }
    [[nodiscard]] const V& get() const { return *self().data(); }
    [[nodiscard]] explicit operator bool() const noexcept { return self().data() != nullptr; }

  private:
    [[nodiscard]] Derived& self() noexcept { return static_cast<Derived&>(*this); }
    [[nodiscard]] const Derived& self() const noexcept { return static_cast<const Derived&>(*this); }
};
} // namespace detail

/**
 * Безопасное разыменование не-владеющего указателя: возвращает ССЫЛКУ НА ДАННЫЕ
 * (семантика std::reference_wrapper<T>::get() - доступ к данным через ссылку) БЕЗ UB на nullptr:
 * нулевой указатель - contract violation → бросается trust::IntMinus (как lock()/lock_const()
 * при истёкшей ссылке). Используется оператором `*` для unique/ptr (прямой доступ к данным).
 */
template <typename T>
inline T& checked_deref(T* ptr) {
    if (!ptr) {
        detail::trusted_cpp_error("dereference of a null reference (nullptr)");
    }
    return *ptr;
}

// -- Forward declarations of the owning reference types and their access guards ----------------
template <typename V, typename D = std::default_delete<V>>
class Unique;

template <typename V>
class StaticUnique;

template <typename V>
class Shared;

template <typename V, bool ReadOnly>
class SharedBorrowedRef;

template <typename V, bool ReadOnly>
class Locker;

// Self-reference mixin (K1): forward declarations so Shared can attach the control block.
template <typename V>
class EnableSharedFromThis;

namespace detail {
template <typename V>
void attach_weak_this(const std::shared_ptr<V>& p);
} // namespace detail

/**
 * ESCAPABLE observer borrowed reference over a `Shared` (refcounted) object (result of
 * Shared::borrow()/borrow_const()). Escapable/copyable; does NOT extend the object lifetime (it
 * holds a std::weak_ptr - an observer, not a strong owner). Once the last strong owner is gone the
 * reference expires (valid() == false, access() reports trust::IntMinus).
 *
 * Access is provided ONLY through `auto g = ref.access();`, which locks the weak pointer into a
 * local Locker (a shared capture pinning the object for the duration of the access).
 */
template <typename V, bool ReadOnly = false>
class SharedBorrowedRef {
  public:
    using ValueType = V;

    SharedBorrowedRef() noexcept = default;
    SharedBorrowedRef(const SharedBorrowedRef&) = default;
    SharedBorrowedRef& operator=(const SharedBorrowedRef&) = default;
    SharedBorrowedRef(SharedBorrowedRef&&) noexcept = default;
    SharedBorrowedRef& operator=(SharedBorrowedRef&&) noexcept = default;

    [[nodiscard]] bool valid() const noexcept { return !weak_.expired(); }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }
    [[nodiscard]] bool has_value() const noexcept { return valid(); }

    /// Access through a local Locker guard; throws if the reference has expired.
    [[nodiscard]] Locker<V, ReadOnly> access() const {
        std::shared_ptr<V> shared = weak_.lock();
        if (!shared) {
            detail::trusted_cpp_error("borrowed reference expired (shared object destroyed)");
        }
        return Locker<V, ReadOnly>(std::move(shared));
    }

  private:
    template <typename>
    friend class Shared;

    explicit SharedBorrowedRef(std::shared_ptr<V> ptr) noexcept
    : weak_(std::move(ptr)) {}

    std::weak_ptr<V> weak_;
};

/**
 * RAII access guard for a reference (result of lock()/lock_const(), i.e. the "take" of a
 * reference). Holds a shared (aliasing) access to the value and does NOT transfer/empty the
 * source reference - it is a shared capture.
 *
 * ReadOnly is a COMPILE-TIME access flag:
 *   Locker<V,false> - mutable access; operator* / operator-> / get() return V& / V* / V&;
 *   Locker<V,true>  - read-only access; return const V& / const V* / const V&.
 * A read-only lock cannot mutate the data at the type level (const-correctness).
 *
 * Locker is POLICY-AGNOSTIC: it does not know the synchronization method. For the plain variant
 * release_ is null; for the sync variant (`trust/trusted-cpp-sync.hpp`) it holds an opaque
 * release handle whose unlock() is invoked on destruction. Non-copyable, movable (move transfers
 * the release handle, so the lock is not released early).
 */
template <typename V, bool ReadOnly = false>
class Locker : public detail::GuardBase<V, ReadOnly, Locker<V, ReadOnly>> {
  public:
    using ValueType = V;

    /// Value-owning guard (for `kLocker` codegen variable declarations: `trust::Locker<T> x(v)`).
    explicit Locker(V value)
    : value_(std::make_shared<V>(std::move(value))) {}

    /// Plain shared-capture guard: aliases the value of an existing Shared; no release.
    explicit Locker(std::shared_ptr<V> value)
    : value_(std::move(value)) {}

    /// Sync shared-capture guard: value + opaque release handle (from trusted-cpp-sync.hpp).
    Locker(std::shared_ptr<V> value, std::shared_ptr<detail::LockRelease> release)
    : value_(std::move(value))
    , release_(std::move(release)) {}

    ~Locker() {
        if (release_) {
            release_->unlock();
        }
    }

    // Non-copyable (a guard cannot be duplicated).
    Locker(const Locker&) = delete;
    Locker& operator=(const Locker&) = delete;

    // Movable: transfer the release handle so the lock stays held until the new owner is gone.
    Locker(Locker&&) noexcept = default;
    Locker& operator=(Locker&&) noexcept = default;

    [[nodiscard]] V* data() noexcept { return value_.get(); }
    [[nodiscard]] const V* data() const noexcept { return value_.get(); }

  private:
    std::shared_ptr<V> value_;
    std::shared_ptr<detail::LockRelease> release_;
};

/**
 * Strong (owning) reference - PLAIN variant, WITHOUT inter-thread synchronization. This is the
 * reference type that is ALWAYS used when a reference exists; multi-threaded synchronization
 * requires the separate `trust::AccessShared<V, Policy>` (trust/trusted-cpp-sync.hpp).
 *
 * Provides direct access (operator* / operator-> / get()) and guarded capture
 * (lock()/lock_const() -> Locker). lock()/lock_const() are a SHARED capture - they do NOT empty
 * the reference and do NOT transfer ownership; a null reference throws trust::IntMinus on any
 * dereference (contract: dereferencing nullptr is an error, never a silent fallback).
 */
template <typename V>
class Weak; // forward declaration (defined below); used as the return of Shared::weak()

template <typename V>
class Shared {
  public:
    using ValueType = V;
    using DataType = V;
    using WeakType = std::weak_ptr<V>;
    using SharedType = std::shared_ptr<V>;

    Shared()
    : ptr_(nullptr) {}

    explicit Shared(const V& val)
    : ptr_(std::make_shared<V>(val)) {
        detail::attach_weak_this(ptr_);
    }

    explicit Shared(V&& val)
    : ptr_(std::make_shared<V>(std::move(val))) {
        detail::attach_weak_this(ptr_);
    }

    /// ADOPT an external resource with a custom deleter (e.g. FILE*/fd/handle): shares ownership
    /// of a raw resource that is released by `deleter` when the last owner is gone. A null `ptr`
    /// yields an EMPTY reference and the deleter is not stored/invoked (no UB on dereference).
    template <typename D>
    Shared(V* ptr, D deleter)
    : ptr_(ptr ? std::shared_ptr<V>(ptr, std::move(deleter)) : std::shared_ptr<V>()) {
        detail::attach_weak_this(ptr_);
    }

    /// Static factory: adopt a resource with a custom deleter (see the (V*, D) constructor).
    template <typename D>
    [[nodiscard]] static Shared adopt(V* ptr, D deleter) {
        return Shared(ptr, std::move(deleter));
    }

    /// Static factory: wrap an EXISTING control block (e.g. an aliasing view; no attach).
    [[nodiscard]] static Shared from_shared_ptr(std::shared_ptr<V> ptr) noexcept { return Shared(std::move(ptr)); }

    Shared(const Shared&) = default;
    Shared& operator=(const Shared&) = default;

    Shared(Shared&&) noexcept = default;
    Shared& operator=(Shared&&) noexcept = default;

    /// PROMOTE: takes exclusive ownership from a Unique (any deleter) and shares it (allocates a
    /// control block). The source Unique becomes empty.
    template <typename D>
    explicit Shared(Unique<V, D>&& unique)
    : ptr_(unique.release()) {
        detail::attach_weak_this(ptr_);
    }

    // -- Direct access (plain, no sync). Throws on null (deref of nullptr is an error). --
    [[nodiscard]] V& operator*() {
        check();
        return *ptr_;
    }
    [[nodiscard]] const V& operator*() const {
        check();
        return *ptr_;
    }
    [[nodiscard]] V* operator->() {
        check();
        return ptr_.get();
    }
    [[nodiscard]] const V* operator->() const {
        check();
        return ptr_.get();
    }
    [[nodiscard]] V& get() {
        check();
        return *ptr_;
    }
    [[nodiscard]] const V& get() const {
        check();
        return *ptr_;
    }

    // make_auto / make_auto_const - shared-capture guard factory used by Weak<T> (contract).
    static Locker<V, false> make_auto(const SharedType& shared) { return Locker<V, false>(shared); }
    static Locker<V, true> make_auto_const(const SharedType& shared) { return Locker<V, true>(shared); }

    // try_make_auto / try_make_auto_const - non-blocking factory used by Weak<T>::try_lock*.
    // For the plain variant there is no lock to fail: nullopt only if the reference is empty.
    static std::optional<Locker<V, false>> try_make_auto(const SharedType& shared) {
        if (!shared) {
            return std::nullopt;
        }
        return Locker<V, false>(shared);
    }
    static std::optional<Locker<V, true>> try_make_auto_const(const SharedType& shared) {
        if (!shared) {
            return std::nullopt;
        }
        return Locker<V, true>(shared);
    }

    /// Guarded capture (the "take" of a reference). SHARED capture: does NOT empty the source,
    /// does NOT transfer ownership. Throws on null (deref of nullptr is an error).
    [[nodiscard]] Locker<V, false> lock() const {
        if (!ptr_) {
            detail::trusted_cpp_error("lock: null shared reference");
        }
        return Locker<V, false>(ptr_);
    }

    /// Guarded read-only capture (const V& at the type level).
    [[nodiscard]] Locker<V, true> lock_const() const {
        if (!ptr_) {
            detail::trusted_cpp_error("lock: null shared reference");
        }
        return Locker<V, true>(ptr_);
    }

    /// Non-blocking exclusive capture; nullopt only if the object is empty.
    [[nodiscard]] std::optional<Locker<V, false>> try_lock() const noexcept {
        if (!ptr_) {
            return std::nullopt;
        }
        return Locker<V, false>(ptr_);
    }

    /// Non-blocking read-only capture; nullopt only if the object is empty.
    [[nodiscard]] std::optional<Locker<V, true>> try_lock_const() const noexcept {
        if (!ptr_) {
            return std::nullopt;
        }
        return Locker<V, true>(ptr_);
    }

    /// Observer (escapable) borrow: returns a NON-OWNING reference that does NOT extend the object
    /// lifetime (it holds a weak observer, not a strong owner). Access is through
    /// `auto g = ref.access();` (a local Locker). The reference expires once the last strong owner
    /// is gone; unlike the old move-out borrow it does NOT require use_count()==1 and does NOT
    /// empty this reference.
    [[nodiscard]] SharedBorrowedRef<V, false> borrow() const {
        check();
        return SharedBorrowedRef<V, false>(ptr_);
    }

    /// Observer read-only borrow (const V& at the type level); same contract as borrow().
    [[nodiscard]] SharedBorrowedRef<V, true> borrow_const() const {
        check();
        return SharedBorrowedRef<V, true>(ptr_);
    }

    /// DEMOTE: converts to exclusive (unique, MONOPOLISTIC) ownership. Requires use_count()==1.
    /// The VALUE is MOVED into a new inline unique object, so the object IDENTITY CHANGES (old
    /// address/references become invalid) and V must be movable; this reference becomes empty.
    [[nodiscard]] StaticUnique<V> to_unique() && {
        if (!ptr_) {
            detail::trusted_cpp_error("to_unique: null shared reference");
        }
        if (ptr_.use_count() != 1) {
            detail::trusted_cpp_error("to_unique: shared reference has other owners");
        }
        V value(std::move(*ptr_));
        ptr_.reset();
        return StaticUnique<V>(std::move(value));
    }

    Shared& set(const V& value) {
        check();
        *ptr_ = value;
        return *this;
    }

    Shared& set(V&& value) {
        check();
        *ptr_ = std::move(value);
        return *this;
    }

    /// Releases the strong reference (the object may be destroyed if no weak refs remain).
    void reset() noexcept { ptr_.reset(); }

    [[nodiscard]] Weak<Shared> weak() const { return Weak<Shared>(*this); }

    /// Number of strong owners of the referenced object (std::shared_ptr::use_count semantics).
    [[nodiscard]] long use_count() const noexcept { return ptr_.use_count(); }

    /// Identity comparison: equal iff both references point to the same object (or both are null).
    [[nodiscard]] bool operator==(const Shared& other) const noexcept { return ptr_ == other.ptr_; }
    [[nodiscard]] bool operator!=(const Shared& other) const noexcept { return !(*this == other); }

    /// Control-block ordering (std::owner_less semantics; usable as a map/set comparator).
    [[nodiscard]] bool owner_before(const Shared& other) const noexcept { return ptr_.owner_before(other.ptr_); }

    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] bool has_value() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] SharedType get_shared() const { return ptr_; }

  private:
    void check() const {
        if (!ptr_) {
            detail::trusted_cpp_error("access to null shared reference");
        }
    }

    /// Wraps an EXISTING control block (used by EnableSharedFromThis::shared_from_this); no attach.
    explicit Shared(std::shared_ptr<V> ptr) noexcept
    : ptr_(std::move(ptr)) {}

    template <typename>
    friend class EnableSharedFromThis;

    template <typename, bool>
    friend class SharedBorrowedRef;

    SharedType ptr_;
};

/**
 * Weak (non-owning) reference to a strong reference (Shared / AccessShared).
 * Does not extend the object lifetime; lock()/lock_const() return a Locker if the object is alive,
 * otherwise (expired) they throw trust::IntMinus. try_lock* return nullopt on expiry.
 * Works with any T exposing the reference-wrapper contract: ValueType, WeakType, SharedType,
 * get_shared(), static make_auto/make_auto_const.
 */
template <typename T>
class Weak {
  public:
    using ValueType = typename T::ValueType;
    using WeakType = typename T::WeakType;
    using SharedType = typename T::SharedType;

    Weak()
    : weak_ptr_() {}

    explicit Weak(const T& ptr)
    : weak_ptr_(ptr.get_shared()) {}

    Weak(const Weak& other)
    : weak_ptr_(other.weak_ptr_) {}

    Weak& operator=(const Weak& other) {
        weak_ptr_ = other.weak_ptr_;
        return *this;
    }

    Weak(Weak&& other) noexcept
    : weak_ptr_(std::move(other.weak_ptr_)) {
        other.weak_ptr_.reset();
    }

    Weak& operator=(Weak&& other) noexcept {
        weak_ptr_ = std::move(other.weak_ptr_);
        other.weak_ptr_.reset();
        return *this;
    }

    Weak& operator=(const T& ptr) {
        weak_ptr_ = ptr.get_shared();
        return *this;
    }

    /// Blocking exclusive capture; throws if the object has already been destroyed.
    [[nodiscard]]
    Locker<ValueType, false> lock() const {
        SharedType shared = weak_ptr_.lock();
        if (!shared) {
            detail::trusted_cpp_error("Weak pointer has expired (lock() returned null)");
        }
        return T::make_auto(shared);
    }

    /// Blocking read-only capture; throws if the object has already been destroyed.
    [[nodiscard]]
    Locker<ValueType, true> lock_const() const {
        SharedType shared = weak_ptr_.lock();
        if (!shared) {
            detail::trusted_cpp_error("Weak pointer has expired (lock() returned null)");
        }
        return T::make_auto_const(shared);
    }

    /// Non-blocking exclusive capture; nullopt if expired or the lock failed.
    [[nodiscard]]
    std::optional<Locker<ValueType, false>> try_lock() const noexcept {
        SharedType shared = weak_ptr_.lock();
        if (!shared) {
            return std::nullopt;
        }
        try {
            return T::try_make_auto(shared);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    /// Non-blocking read-only capture; nullopt if expired or the lock failed.
    [[nodiscard]]
    std::optional<Locker<ValueType, true>> try_lock_const() const noexcept {
        SharedType shared = weak_ptr_.lock();
        if (!shared) {
            return std::nullopt;
        }
        try {
            return T::try_make_auto_const(shared);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    Weak& set(const ValueType& value) {
        auto guard = lock();
        *guard = value;
        return *this;
    }

    Weak& set(ValueType&& value) {
        auto guard = lock();
        *guard = std::move(value);
        return *this;
    }

    /// Resets the weak reference (stops observing the object).
    void reset() noexcept { weak_ptr_.reset(); }

    [[nodiscard]]
    explicit operator bool() const noexcept {
        return !weak_ptr_.expired();
    }

    [[nodiscard]]
    bool has_value() const noexcept {
        return !weak_ptr_.expired();
    }

    [[nodiscard]]
    SharedType lock_shared() const {
        return weak_ptr_.lock();
    }

  private:
    mutable WeakType weak_ptr_;
};

/**
 * Self-reference mixin (K1 in types/REFType.md §8): lets an object hand out `Shared`/`Weak`
 * references to ITSELF. The unique type `V` must derive from `EnableSharedFromThis<V>`;
 * `Shared<V>` attaches the control block when it creates/adopts such an object. `shared_from_this()`
 * throws `trust::IntMinus` if the object is not currently unique by a `Shared` (or the reference
 * expired). Copying the object does NOT copy the internal weak reference (like
 * `std::enable_shared_from_this`).
 */
template <typename V>
class EnableSharedFromThis {
  public:
    EnableSharedFromThis() = default;

    // Copying/moving the object must NOT copy the internal weak reference (owner may change).
    EnableSharedFromThis(const EnableSharedFromThis&) noexcept {}
    EnableSharedFromThis& operator=(const EnableSharedFromThis&) noexcept { return *this; }
    EnableSharedFromThis(EnableSharedFromThis&&) noexcept {}
    EnableSharedFromThis& operator=(EnableSharedFromThis&&) noexcept { return *this; }

    /// Returns a `Shared` sharing ownership with the current owner; throws if not unique / expired.
    [[nodiscard]] Shared<V> shared_from_this() const {
        std::shared_ptr<V> p = weak_this_.lock();
        if (!p) {
            detail::trusted_cpp_error("shared_from_this: object is not unique by a Shared (or expired)");
        }
        return Shared<V>(std::move(p));
    }

    /// Returns a `Weak` observer of the current owner (empty if not unique / expired).
    [[nodiscard]] Weak<Shared<V>> weak_from_this() const noexcept { return Weak<Shared<V>>(Shared<V>(weak_this_.lock())); }

    /// Internal (called by `Shared`): attach the owner's control block.
    void attach_weak_this_impl(std::weak_ptr<V> w) const noexcept { weak_this_ = std::move(w); }

  private:
    mutable std::weak_ptr<V> weak_this_;
};

namespace detail {
/// Attaches the owning control block to an `EnableSharedFromThis` base (no-op if V does not derive).
template <typename V>
void attach_weak_this(const std::shared_ptr<V>& p) {
    if constexpr (std::is_base_of_v<EnableSharedFromThis<V>, V>) {
        if (p) {
            p->attach_weak_this_impl(std::weak_ptr<V>(p));
        }
    }
}
} // namespace detail

/**
 * Exclusive (unique) owning reference - the Trust equivalent of std::unique_ptr, used for owning
 * kinds that need a pointer wrapper (currently `@[deleter(D)]`). Holds the object with a
 * std::unique_ptr (no control block, no refcount). MONOPOLISTIC and NON-COPYABLE: ownership
 * transfers ONLY via move/swap; there is NO borrow/aliasing form (a monopolistic owner cannot be
 * referenced by a second name). Access to the owned value is direct: `operator*` / `checked_deref`.
 *
 * Conversions: to_shared() PROMOTES to shared ownership (allocates a control block; this becomes
 * empty).
 */
template <typename V, typename D>
class Unique {
  public:
    using ValueType = V;

    Unique() noexcept
    : ptr_(nullptr) {}

    explicit Unique(const V& val)
    : ptr_(std::unique_ptr<V, D>(new V(val))) {}

    explicit Unique(V&& val)
    : ptr_(std::unique_ptr<V, D>(new V(std::move(val)))) {}

    /// Takes ownership of an existing unique_ptr (builder / conversions / adopt).
    explicit Unique(std::unique_ptr<V, D> ptr) noexcept
    : ptr_(std::move(ptr)) {}

    /// Static factory: adopt an external resource with a custom deleter (D is part of the type).
    template <typename Del>
    [[nodiscard]] static Unique adopt(V* ptr, Del deleter) {
        return Unique(std::unique_ptr<V, D>(ptr, std::move(deleter)));
    }

    ~Unique() noexcept = default;

    // Exclusive ownership: not copyable; ownership transfers only via move/swap.
    Unique(const Unique&) = delete;
    Unique& operator=(const Unique&) = delete;
    Unique(Unique&&) noexcept = default;
    Unique& operator=(Unique&& other) noexcept {
        if (this != &other) {
            ptr_ = std::move(other.ptr_);
        }
        return *this;
    }

    /// std::swap support (permanent exchange of ownership).
    void swap(Unique& other) noexcept { ptr_.swap(other.ptr_); }

    [[nodiscard]] V* get() noexcept { return ptr_.get(); }
    [[nodiscard]] const V* get() const noexcept { return ptr_.get(); }

    [[nodiscard]] V& operator*() {
        check();
        return *ptr_;
    }
    [[nodiscard]] const V& operator*() const {
        check();
        return *ptr_;
    }
    [[nodiscard]] V* operator->() {
        check();
        return ptr_.get();
    }
    [[nodiscard]] const V* operator->() const {
        check();
        return ptr_.get();
    }

    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }
    [[nodiscard]] bool has_value() const noexcept { return ptr_ != nullptr; }

    void reset() noexcept { ptr_.reset(); }

    /// Releases exclusive ownership as a unique_ptr (this becomes empty).
    [[nodiscard]] std::unique_ptr<V, D> release() noexcept { return std::move(ptr_); }

    /// Identity comparison: equal iff both references point to the same object (or both are null).
    [[nodiscard]] bool operator==(const Unique& other) const noexcept { return ptr_ == other.ptr_; }
    [[nodiscard]] bool operator!=(const Unique& other) const noexcept { return !(*this == other); }

    /// PROMOTE: transfers exclusive ownership into shared ownership (allocates a control block;
    /// this becomes empty).
    [[nodiscard]] Shared<V> to_shared() && { return Shared<V>(std::move(*this)); }

  private:
    void check() const {
        if (!ptr_) {
            detail::trusted_cpp_error("access to null unique pointer");
        }
    }

    std::unique_ptr<V, D> ptr_;
};

/// Factory mirroring std::make_unique for the exclusive owning reference (used by codegen).
template <typename V, typename... Args>
[[nodiscard]] Unique<V> make_unique(Args&&... args) {
    return Unique<V>(std::make_unique<V>(std::forward<Args>(args)...));
}

/**
 * Zero-overhead move-only обёртка СТАТИЧЕСКОГО эксклюзивного владения (`unique` без deleter'а).
 * Значение хранится INLINE: без heap, без refcount, без указателя (sizeof == sizeof(V)). Уникальность
 * и перенос владения обеспечиваются статически АНАЛИЗАТОРОМ; копирование удалено как
 * defense-in-depth (C++-уровень). Освобождение значения — автоматическое (деструктор V), отдельного
 * deleter'а нет (для внешних ресурсов с deleter'ом используется `trust::Unique<T, D>`).
 */
template <typename V>
class StaticUnique {
  public:
    using ValueType = V;

    StaticUnique() = default;
    explicit StaticUnique(const V& v)
    : value_(v) {}
    explicit StaticUnique(V&& v)
    : value_(std::move(v)) {}

    // Статическое эксклюзивное владение: не копируется (move-only).
    StaticUnique(const StaticUnique&) = delete;
    StaticUnique& operator=(const StaticUnique&) = delete;
    StaticUnique(StaticUnique&&) noexcept = default;
    StaticUnique& operator=(StaticUnique&&) noexcept = default;

    [[nodiscard]] V& get() noexcept { return value_; }
    [[nodiscard]] const V& get() const noexcept { return value_; }
    [[nodiscard]] V& operator*() noexcept { return value_; }
    [[nodiscard]] const V& operator*() const noexcept { return value_; }
    [[nodiscard]] V* data() noexcept { return &value_; }
    [[nodiscard]] const V* data() const noexcept { return &value_; }
    [[nodiscard]] explicit operator bool() const noexcept { return true; }

    void swap(StaticUnique& other) noexcept {
        using std::swap;
        swap(value_, other.value_);
    }

  private:
    V value_{}; ///< значение хранится inline (zero-cost)
};

/// std::swap support для StaticUnique.
template <typename V>
inline void swap(StaticUnique<V>& a, StaticUnique<V>& b) noexcept {
    a.swap(b);
}

// -- Copy-on-write / clone helpers (library layer; require a copyable V) ----------------------
/// Deep copy of a shared reference (allocates a new control block).
template <typename V>
[[nodiscard]] Shared<V> clone(const Shared<V>& source) {
    return Shared<V>(*source);
}

/// Deep copy of an exclusive reference (any deleter; requires copyable V).
template <typename V, typename D>
[[nodiscard]] Unique<V, D> clone(const Unique<V, D>& source) {
    return Unique<V, D>(*source);
}

/// Copy-on-write detach: if `ref` is shared with other strong owners, replaces it with a private
/// copy; otherwise a no-op (the sole owner keeps mutating in place). NOTE: `use_count()` is not a
/// synchronization primitive - call detach() under external synchronization in multithreaded code.
template <typename V>
void detach(Shared<V>& ref) {
    if (ref.use_count() != 1) {
        ref = Shared<V>(*ref);
    }
}

/// ALIASING: returns a `Shared<U>` that points to a subobject `ptr` inside `owner` while SHARING
/// ownership with it (the whole owner stays alive as long as the alias exists). The owner must be
/// non-empty. Typical use: referencing a member/subobject of an unique object.
template <typename U, typename V>
[[nodiscard]] Shared<U> alias(const Shared<V>& owner, U* ptr) {
    if (!owner.has_value()) {
        detail::trusted_cpp_error("alias: owner is empty");
    }
    return Shared<U>::from_shared_ptr(std::shared_ptr<U>(owner.get_shared(), ptr));
}

// -- L2 contract concepts (see types/REFType.md §10) -----------------------------------------
// These concepts fix the COMMON interface of the memory-model types so that any backend
// (std-shared / intrusive / atomic / custom-deleter) is interchangeable and can be covered by a
// single conformance test suite. They are purely structural (no runtime overhead) and do not
// change any behaviour.
#if defined(__cpp_concepts) && __cpp_concepts >= 201907L

/// Access guard (Locker): non-copyable, movable, provides data()/operator*/get()/operator->/operator bool.
template <typename T>
concept AccessGuard = requires(T& g, const T& cg) {
    typename T::ValueType;
    g.data();
    cg.data();
    static_cast<bool>(g);
    *g;
    g.get();
    g.operator->();
    requires !std::is_copy_constructible_v<T>;
    requires std::is_move_constructible_v<T>;
};

/// Owning reference with DIRECT data access (Unique / Shared): get()/operator*/bool/reset().
/// `unique` is MONOPOLISTIC (no guarded capture); `shared` additionally provides lock()/lock_const().
template <typename T>
concept UniqueRef = requires(T& r) {
    typename T::ValueType;
    r.get();
    static_cast<bool>(r);
    *r;
    r.reset();
};

/// Shared (refcounted) reference with a weak observer and guarded access. Direct access is NOT
/// required (AccessShared exposes access only through lock()/lock_const()).
template <typename T>
concept SharedRef = std::is_copy_constructible_v<T> && requires(T& r, const T& cr) {
    typename T::ValueType;
    r.weak();
    r.get_shared();
    cr.lock();
    cr.lock_const();
    static_cast<bool>(r);
};

/// Reference supporting ESCAPABLE observer borrow: borrow()/borrow_const() return a borrowed
/// reference (`Shared` -> SharedBorrowedRef) whose access() yields an AccessGuard (Locker).
/// `unique` is monopolistic and has NO borrow form.
template <typename T>
concept BorrowableRef = requires(T& r) {
    r.borrow();
    r.borrow_const();
    requires AccessGuard<decltype(r.borrow().access())>;
    requires AccessGuard<decltype(r.borrow_const().access())>;
};

/// Weak (non-owning) reference: validity check + upgrade to an owner.
template <typename T>
concept WeakRef = requires(T& w) {
    w.has_value();
    static_cast<bool>(w);
    w.lock();
    w.lock_const();
    w.try_lock();
};

#endif // __cpp_concepts

} // namespace trust

// -- std::hash support (hashes the identity/address of the referenced object) ------------------
namespace std {
template <typename V>
struct hash<trust::Shared<V>> {
    [[nodiscard]] size_t operator()(const trust::Shared<V>& value) const noexcept {
        return std::hash<const void*>()(static_cast<const void*>(value.get_shared().get()));
    }
};
template <typename V, typename D>
struct hash<trust::Unique<V, D>> {
    [[nodiscard]] size_t operator()(const trust::Unique<V, D>& value) const noexcept { return std::hash<const void*>()(static_cast<const void*>(value.get())); }
};
} // namespace std

#endif // TRUST_TRUSTED_CPP_HPP
