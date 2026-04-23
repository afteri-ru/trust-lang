#pragma once

#include <stdexcept>
#include <memory>
#include <optional>

#include <mutex>
#include <shared_mutex>
#include <thread>

#include <format>
#include <chrono>

#include "diag/error.hpp"

namespace trust {

// Pre-definition of template class for variable with weak reference
template <typename T>
class Weak;

/*
 * Base class for shared data with the ability to multi-thread synchronize access
 */

using SyncTimeoutType = std::chrono::milliseconds;
inline constexpr SyncTimeoutType SyncTimeoutDeadlock = std::chrono::milliseconds(5000);
inline constexpr SyncTimeoutType SyncTimeoutNoWait = std::chrono::milliseconds(0);

template <typename V>
class Sync {
  public:
    // Locker needs access to data member and lock methods
    template <typename>
    friend class Locker;

    explicit Sync(V v) : data(std::move(v)) {}

    virtual ~Sync() = default;

  protected:
    V data;

    [[nodiscard]]
    virtual bool try_lock(const SyncTimeoutType &timeout = SyncTimeoutDeadlock) = 0;

    [[nodiscard]]
    virtual bool try_lock_const(const SyncTimeoutType &timeout = SyncTimeoutDeadlock) = 0;

    virtual void unlock() = 0;

    virtual void unlock_const() = 0;
};

/**
 * A class without a synchronization primitive and with the ability to work only in one application thread.
 * Used to control access to data from only one application thread without creating a synchronization object between threads.
 */
template <typename V>
class SyncSingleThread : public Sync<V> {
  public:
    explicit SyncSingleThread(V v) : Sync<V>(std::move(v)), m_thread_id(std::this_thread::get_id()) {}

  protected:
    const std::thread::id m_thread_id;

    void check_thread() const {
        if (m_thread_id != std::this_thread::get_id()) {
            FAULT("Using a single thread variable in another thread!");
        }
    }

    bool try_lock(const SyncTimeoutType &timeout) override {
        check_thread();
        if (timeout != SyncTimeoutDeadlock) {
            FAULT("Timeout is not applicable for this object type!");
        }
        return true;
    }

    void unlock() override { check_thread(); }

    bool try_lock_const(const SyncTimeoutType &timeout) override {
        check_thread();
        if (timeout != SyncTimeoutDeadlock) {
            FAULT("Timeout is not applicable for this object type!");
        }
        return true;
    }

    void unlock_const() override { check_thread(); }
};

/**
 * Class with recursive_timed_mutex for simple multithreaded synchronization
 */

template <typename V>
class SyncTimedMutex : public Sync<V> {
  public:
    explicit SyncTimedMutex(V v) : Sync<V>(std::move(v)) {}

  protected:
    bool try_lock(const SyncTimeoutType &timeout) override { return m_mutex.try_lock_for(timeout); }
    void unlock() override { m_mutex.unlock(); }
    bool try_lock_const(const SyncTimeoutType &timeout) override { return m_mutex.try_lock_for(timeout); }
    void unlock_const() override { m_mutex.unlock(); }

  private:
    mutable std::recursive_timed_mutex m_mutex;
};

/**
 * Class with shared_timed_mutex for multi-thread synchronization for exclusive read/write lock or shared read-only lock
 */

template <typename V>
class SyncTimedShared : public Sync<V> {
  public:
    explicit SyncTimedShared(V v) : Sync<V>(std::move(v)) {}

  protected:
    bool try_lock(const SyncTimeoutType &timeout) override { return m_mutex.try_lock_for(timeout); }
    void unlock() override { m_mutex.unlock(); }
    bool try_lock_const(const SyncTimeoutType &timeout) override { return m_mutex.try_lock_shared_for(timeout); }
    void unlock_const() override { m_mutex.unlock_shared(); }

  private:
    mutable std::shared_timed_mutex m_mutex;
};

/**
 * RAII lock guard for Sync objects.
 * Captures and locks a Sync object in constructor, releases in destructor.
 * Non-copyable, movable.
 *
 * V - Variable value type
 */

template <typename V>
class Locker {
  public:
    Locker(std::shared_ptr<Sync<V>> sync, bool read_only, const SyncTimeoutType &timeout = SyncTimeoutDeadlock)
        : sync_(std::move(sync)), read_only_(read_only) {
        if (!sync_) {
            FAULT("Object missing (null pointer exception)");
        }
        bool ok = read_only_ ? sync_->try_lock_const(timeout) : sync_->try_lock(timeout);
        if (!ok) {
            FAULT("try_lock{} timeout", read_only_ ? " read only" : "");
        }
    }

    ~Locker() {
        if (sync_) {
            if (read_only_) {
                sync_->unlock_const();
            } else {
                sync_->unlock();
            }
        }
    }

    // Non-copyable
    Locker(const Locker &) = delete;
    Locker &operator=(const Locker &) = delete;

    // Movable
    Locker(Locker &&other) noexcept : sync_(std::move(other.sync_)), read_only_(other.read_only_) { other.sync_ = nullptr; }

    Locker &operator=(Locker &&other) noexcept {
        if (this != &other) {
            if (sync_) {
                if (read_only_) {
                    sync_->unlock_const();
                } else {
                    sync_->unlock();
                }
            }
            sync_ = std::move(other.sync_);
            read_only_ = other.read_only_;
            other.sync_ = nullptr;
        }
        return *this;
    }

    V &operator*() { return sync_->data; }
    const V &operator*() const { return sync_->data; }
    V *operator->() { return &sync_->data; }
    const V *operator->() const { return &sync_->data; }

    /// Returns true if this Locker holds a valid lock (hasn't been moved from)
    [[nodiscard]] explicit operator bool() const noexcept { return sync_ != nullptr; }

  private:
    std::shared_ptr<Sync<V>> sync_;
    bool read_only_;
};

/**
 * Reference variable (shared pointer) with optional multi-threaded access control.
 *
 * By default using template @ref SyncSingleThread without multithreaded access control.
 * @see @ref SyncSingleThread, @ref SyncTimedMutex, @ref SyncTimedShared
 */

template <typename V, template <typename> typename S = SyncSingleThread>
class Shared {
  public:
    using ValueType = V;
    using DataType = S<V>;
    using WeakType = std::weak_ptr<DataType>;
    using SharedType = std::shared_ptr<DataType>;

    Shared() : ptr_(nullptr) {}

    explicit Shared(const V &val) : ptr_(std::make_shared<DataType>(val)) {}

    explicit Shared(V &&val) : ptr_(std::make_shared<DataType>(std::move(val))) {}

    Shared(const Shared &other) = default;
    Shared &operator=(const Shared &other) = default;

    Shared(Shared &&other) noexcept = default;
    Shared &operator=(Shared &&other) noexcept = default;

    static Locker<V> make_auto(const SharedType &shared, bool read_only, const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        return Locker<V>(shared, read_only, timeout);
    }

    [[nodiscard]]
    Locker<V> lock(const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        return make_auto(ptr_, false, timeout);
    }

    [[nodiscard]]
    Locker<V> lock_const(const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        return make_auto(ptr_, true, timeout);
    }

    /// Non-blocking lock attempt
    [[nodiscard]]
    std::optional<Locker<V>> try_lock(const SyncTimeoutType &timeout = SyncTimeoutNoWait) {
        if (!ptr_)
            return std::nullopt;
        try {
            return Locker<V>(ptr_, false, timeout);
        } catch (const std::runtime_error &) {
            return std::nullopt;
        }
    }

    /// Non-blocking const lock attempt
    [[nodiscard]]
    std::optional<Locker<V>> try_lock_const(const SyncTimeoutType &timeout = SyncTimeoutNoWait) {
        if (!ptr_)
            return std::nullopt;
        try {
            return Locker<V>(ptr_, true, timeout);
        } catch (const std::runtime_error &) {
            return std::nullopt;
        }
    }

    Shared &set(const V &value, const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        auto guard_lock = lock(timeout);
        *guard_lock = value;
        return *this;
    }

    Shared &set(V &&value, const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        auto guard_lock = lock(timeout);
        *guard_lock = std::move(value);
        return *this;
    }

    [[nodiscard]]
    Weak<Shared> weak() {
        return Weak<Shared>(*this);
    }

    [[nodiscard]]
    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

    [[nodiscard]]
    bool has_value() const noexcept {
        return ptr_ != nullptr;
    }

    [[nodiscard]]
    SharedType get_shared() const {
        return ptr_;
    }

  private:
    SharedType ptr_;
};

/**
 * A wrapper template class for storing weak pointers to shared variables
 */

template <typename T>
class Weak {
  public:
    using ValueType = typename T::ValueType;
    using WeakType = typename T::WeakType;
    using SharedType = typename T::SharedType;

    Weak() : weak_ptr_() {}

    explicit Weak(const T &ptr) : weak_ptr_(ptr.get_shared()) {}

    Weak(const Weak &other) : weak_ptr_(other.weak_ptr_) {}

    Weak &operator=(const Weak &other) {
        weak_ptr_ = other.weak_ptr_;
        return *this;
    }

    Weak(Weak &&other) noexcept : weak_ptr_(std::move(other.weak_ptr_)) { other.weak_ptr_.reset(); }

    Weak &operator=(Weak &&other) noexcept {
        weak_ptr_ = std::move(other.weak_ptr_);
        other.weak_ptr_.reset();
        return *this;
    }

    Weak &operator=(const T &ptr) {
        weak_ptr_ = ptr.get_shared();
        return *this;
    }

    [[nodiscard]]
    Locker<ValueType> lock(const SyncTimeoutType &timeout = SyncTimeoutDeadlock) const {
        SharedType shared = weak_ptr_.lock();
        if (!shared) {
            FAULT("Weak pointer has expired (lock() returned null)");
        }
        return T::make_auto(shared, false, timeout);
    }

    [[nodiscard]]
    Locker<ValueType> lock_const(const SyncTimeoutType &timeout = SyncTimeoutDeadlock) const {
        SharedType shared = weak_ptr_.lock();
        if (!shared) {
            FAULT("Weak pointer has expired (lock() returned null)");
        }
        return T::make_auto(shared, true, timeout);
    }

    /// Non-blocking lock attempt — returns nullopt if expired or lock failed
    [[nodiscard]]
    std::optional<Locker<ValueType>> try_lock(const SyncTimeoutType &timeout = SyncTimeoutNoWait) const {
        SharedType shared = weak_ptr_.lock();
        if (!shared)
            return std::nullopt;
        try {
            return T::make_auto(shared, false, timeout);
        } catch (const std::runtime_error &) {
            return std::nullopt;
        }
    }

    /// Non-blocking const lock attempt — returns nullopt if expired or lock failed
    [[nodiscard]]
    std::optional<Locker<ValueType>> try_lock_const(const SyncTimeoutType &timeout = SyncTimeoutNoWait) const {
        SharedType shared = weak_ptr_.lock();
        if (!shared)
            return std::nullopt;
        try {
            return T::make_auto(shared, true, timeout);
        } catch (const std::runtime_error &) {
            return std::nullopt;
        }
    }

    Weak &set(const ValueType &value, const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        auto guard_lock = lock(timeout);
        *guard_lock = value;
        return *this;
    }

    Weak &set(ValueType &&value, const SyncTimeoutType &timeout = SyncTimeoutDeadlock) {
        auto guard_lock = lock(timeout);
        *guard_lock = std::move(value);
        return *this;
    }

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