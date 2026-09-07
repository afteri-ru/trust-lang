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
// <shared_mutex>, <chrono>. It models a strong reference (Shared), a weak reference (Weak) and the
// RAII access guard (Locker) for a reference that is ALWAYS present when a reference exists.
// Multi-threaded synchronization lives in a SEPARATE header `trust/trusted-cpp-sync.hpp`
// (SyncShared<V, Policy>), included ONLY when cross-thread synchronization is required.
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

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "trust/interrupt.hpp" // IntMinus: сбой захвата - встроенная ошибка языка (ловится {- ... -} / catch(IntMinus&))

namespace trust {

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
class Locker {
  public:
    using ValueType = V;
    using DataAccess = std::conditional_t<ReadOnly, const V&, V&>;
    using PointerAccess = std::conditional_t<ReadOnly, const V*, V*>;

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

    [[nodiscard]] DataAccess operator*() { return *value_; }
    [[nodiscard]] const V& operator*() const { return *value_; }
    [[nodiscard]] PointerAccess operator->() { return value_.get(); }
    [[nodiscard]] const V* operator->() const { return value_.get(); }
    [[nodiscard]] DataAccess get() { return *value_; }
    [[nodiscard]] const V& get() const { return *value_; }

    /// True if this Locker holds a valid access (hasn't been moved from).
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

  private:
    std::shared_ptr<V> value_;
    std::shared_ptr<detail::LockRelease> release_;
};

/**
 * Strong (owning) reference - PLAIN variant, WITHOUT inter-thread synchronization. This is the
 * reference type that is ALWAYS used when a reference exists; multi-threaded synchronization
 * requires the separate `trust::SyncShared<V, Policy>` (trust/trusted-cpp-sync.hpp).
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
    : ptr_(std::make_shared<V>(val)) {}

    explicit Shared(V&& val)
    : ptr_(std::make_shared<V>(std::move(val))) {}

    Shared(const Shared&) = default;
    Shared& operator=(const Shared&) = default;

    Shared(Shared&&) noexcept = default;
    Shared& operator=(Shared&&) noexcept = default;

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

    [[nodiscard]] explicit operator bool() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] bool has_value() const noexcept { return ptr_ != nullptr; }

    [[nodiscard]] SharedType get_shared() const { return ptr_; }

  private:
    void check() const {
        if (!ptr_) {
            detail::trusted_cpp_error("access to null shared reference");
        }
    }

    SharedType ptr_;
};

/**
 * Weak (non-owning) reference to a strong reference (Shared / SyncShared).
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

} // namespace trust

#endif // TRUST_TRUSTED_CPP_HPP
