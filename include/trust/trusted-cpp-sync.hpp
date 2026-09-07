// trust/trusted-cpp-sync.hpp - MULTI-THREADED reference type (SyncShared<V, Policy>).
//
// SEPARATE header, included ONLY when cross-thread synchronization is required. The plain variant
// (trust/trusted-cpp.hpp, always used when a reference exists) provides Shared/Weak/Locker without
// any synchronization primitive; this header adds a synchronized strong reference on top.
//
// Locker (defined in trust/trusted-cpp.hpp) is POLICY-AGNOSTIC: it does NOT know the sync method
// (which policy, shared vs exclusive, timeout). Here SyncShared owns the policy (Policy template
// parameter) and hands Locker an opaque release handle (detail::SyncRelease) whose unlock() is
// called by the Locker destructor.
//
// Built-in access-synchronization policies (registered in the TypeRegistry as built-in types
// under Group::kSync, marked with the `sync` attribute; referenced by name as the 2nd argument
// of @[reftype("shared"/"weak", <policy>)@]):
//   SyncSingleThreadPolicy - object usable ONLY in the thread where it was created;
//   SyncMutexPolicy        - plain exclusive mutex;
//   SyncRwMutexPolicy      - timed read/write mutex (shared read-only / exclusive write).
// All policies embed the DEADLOCK DETECTOR: a timed capture against the configurable global timeout
// (default SyncTimeoutDeadlock = 5s, trust::runtime::syncDeadlockTimeout / -fsync-deadlock=... /
// --trust:fsync-deadlock=...). Timeout semantics (single value, no boolean flag): >0 - wait up to
// that and report a deadlock on expiry; 0 - no wait (non-blocking); <0 - block forever (detector
// off, classic mutex). On a positive timeout the blocking lock() throws trust::IntMinus (deadlock).
//
// This header is self-contained (std headers only) and embedded into trust-runtime
// (ELF section "trust/trusted-cpp-sync.hpp"); the pipeline extracts it when a program uses a
// synchronized reference.
//
// RE-ENTRANCY: shared_timed_mutex is NOT recursive. Taking a lock_const() (shared) while already
// holding lock() (exclusive) on the SAME object within one thread is a user error -> self-deadlock
// / UB. It is NOT detected here (would require thread-local ownership tracking); do not re-lock the
// same synchronized object re-entrantly.

#ifndef TRUST_TRUSTED_CPP_SYNC_HPP
#define TRUST_TRUSTED_CPP_SYNC_HPP

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "trust/trusted-cpp.hpp"

namespace trust {

// Sync timeouts. SyncTimeoutType is std::chrono::milliseconds (signed). Semantics of a blocking
// timeout value:
//   >0 - wait up to that many ms; on expiry the blocking lock() reports a deadlock;
//   0  - no wait (non-blocking, try-once);
//   <0 - wait until the end (block forever, deadlock detector disabled).
// SyncTimeoutNoWait = 0 (default for try_lock*). The default blocking timeout is the named
// constant SyncTimeoutDeadlock (5s) - the SINGLE source of the default for both the runtime and
// the -fsync-deadlock option. It is overridable at compile time (-fsync-deadlock=<ms|s|nano>) and
// at program start by the system option --trust:fsync-deadlock=<ms|s|nano> (or
// --trust:no-fsync-deadlock to block forever).
using SyncTimeoutType = std::chrono::milliseconds;
inline constexpr SyncTimeoutType SyncTimeoutNoWait = std::chrono::milliseconds(0);
inline constexpr SyncTimeoutType SyncTimeoutBlockForever = std::chrono::milliseconds(-1);
/// ЕДИНЫЙ дефолт таймаута детектора взаимной блокировки (5s): и для рантайма, и для опции.
inline constexpr SyncTimeoutType SyncTimeoutDeadlock = std::chrono::seconds(5);

// ----------------------------------------------------------------------------
// Runtime configuration of the deadlock detector (applies to EVERY sync object).
// Default: SyncTimeoutDeadlock (5s). Single timeout value with 0/-1/+ semantics (see above) -
// NO separate boolean "enabled" flag.
// ----------------------------------------------------------------------------
namespace runtime {

/// Глобальный таймаут детектора взаимной блокировки (все объекты синхронизации). Дефолт 5s.
inline SyncTimeoutType& syncDeadlockTimeoutRef() {
    static SyncTimeoutType d = SyncTimeoutDeadlock;
    return d;
}
[[nodiscard]] inline SyncTimeoutType syncDeadlockTimeout() {
    return syncDeadlockTimeoutRef();
}

/// Установка таймаута детектора (положительный - таймаут; 0 - без ожидания; <0 - ждать до конца).
inline void setSyncDeadlockTimeout(SyncTimeoutType t) {
    syncDeadlockTimeoutRef() = t;
}

/// Разбор значения времени с суффиксом (единицы из <chrono>): "5" (секунды), "5s", "500ms",
/// "5000000ns"/"5nano". БЕЗ суффикса - СЕКУНДЫ. Возвращает false при некорректном вводе.
inline bool parseSyncDuration(std::string_view s, SyncTimeoutType& out) {
    if (s.empty()) {
        return false;
    }
    // Отделяем числовую часть от суффикса.
    std::size_t i = 0;
    while (i < s.size() && (s[i] >= '0' && s[i] <= '9')) {
        ++i;
    }
    if (i == 0) {
        return false;
    }
    uint64_t value = 0;
    for (std::size_t j = 0; j < i; ++j) {
        if (value > (std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(s[j] - '0')) / 10) {
            return false; // переполнение
        }
        value = value * 10 + static_cast<uint64_t>(s[j] - '0');
    }
    const std::string_view suffix = s.substr(i);
    if (suffix.empty() || suffix == "s") {
        out = std::chrono::duration_cast<SyncTimeoutType>(std::chrono::seconds(value));
    } else if (suffix == "ms") {
        out = std::chrono::milliseconds(value);
    } else if (suffix == "nano" || suffix == "ns" || suffix == "n") {
        out = std::chrono::duration_cast<SyncTimeoutType>(std::chrono::nanoseconds(value));
    } else {
        return false;
    }
    return true;
}

/// Разбор строки времени с fallback: при некорректном вводе возвращает fallback (обычно
/// глобальный дефолт SyncTimeoutDeadlock). ЕДИНЫЙ источник для транспилятора (пер-объектный
/// таймаут reftype) и рантайма.
[[nodiscard]] inline SyncTimeoutType syncTimeoutFromString(std::string_view s, SyncTimeoutType fallback) {
    SyncTimeoutType t;
    return parseSyncDuration(s, t) ? t : fallback;
}

/// Установка таймаута из строки (s|ms|nano, без суффикса = секунды). Возвращает false при
/// некорректном значении. Используется транспилятором для compile-time дефолта
/// (-fsync-deadlock=...): в начало main встраивается вызов этой функции.
inline bool setSyncDeadlockFromString(std::string_view s) {
    SyncTimeoutType t;
    if (!parseSyncDuration(s, t)) {
        return false;
    }
    syncDeadlockTimeoutRef() = t;
    return true;
}

/// Применяет системные опции среды (единый префикс `--trust:`, собранные parseArgs в
/// `ParsedArgs::env`). Обрабатываются ДО программно-специфичных аргументов main и НЕ передаются
/// в программу. Тот же стандартный сценарий, что и compile-time флаги:
///   --trust:fsync-deadlock=<ms|s|nano>  - перекрыть таймаут детектора (= -fsync-deadlock=...);
///   --trust:fno-sync-deadlock           - детектор выключен, блокировка до конца (= -fno-sync-deadlock).
inline void applySystemEnv(const std::vector<std::string>& env) {
    for (const std::string& arg : env) {
        const std::string_view v(arg);
        if (!v.starts_with("--trust:")) {
            continue;
        }
        const std::string_view body = v.substr(std::string_view("--trust:").size());
        if (body.starts_with("fsync-deadlock=")) {
            SyncTimeoutType t;
            if (parseSyncDuration(body.substr(std::string_view("fsync-deadlock=").size()), t)) {
                syncDeadlockTimeoutRef() = t;
            }
        } else if (body == "fno-sync-deadlock") {
            syncDeadlockTimeoutRef() = SyncTimeoutBlockForever; // ждать до конца (детектор выкл)
        }
    }
}

} // namespace runtime

namespace detail {

// Shared synchronized storage: the value plus the access policy that guards it.
// default_timeout - пер-объектный таймаут детектора (перекрывает глобальный) либо nullopt =
// используется глобальный trust::runtime::syncDeadlockTimeout(). Задаётся третьим аргументом
// reftype (`@[reftype("shared", <policy>, <timeout>)@]`).
template <typename V, typename Policy>
struct SyncData {
    V value;
    mutable Policy policy;
    std::optional<SyncTimeoutType> default_timeout;
    explicit SyncData(V v)
    : value(std::move(v)) {}
    SyncData(V v, SyncTimeoutType t)
    : value(std::move(v))
    , default_timeout(t) {}
};

// Opaque release handle handed to Locker. Locker does NOT know the policy: it only calls
// unlock() on destruction. This handle knows whether the lock was shared or exclusive and
// forwards the release to the owning policy (which decides what to unlock).
template <typename V, typename Policy>
class SyncRelease : public detail::LockRelease {
  public:
    explicit SyncRelease(std::shared_ptr<SyncData<V, Policy>> data, bool shared_mode)
    : data_(std::move(data))
    , shared_mode_(shared_mode) {}

    void unlock() noexcept override {
        if (shared_mode_) {
            data_->policy.unlock_shared();
        } else {
            data_->policy.unlock();
        }
    }

  private:
    std::shared_ptr<SyncData<V, Policy>> data_;
    bool shared_mode_;
};
} // namespace detail

// ----------------------------------------------------------------------------
// Встроенные политики синхронизации доступа. Все предоставляют ЕДИНЫЙ интерфейс,
// используемый SyncShared:
//   bool try_lock(const SyncTimeoutType&)     - эксклюзивный timed-захват;
//   bool try_lock_shared(const SyncTimeoutType&) - разделяемый timed-захват (для rw), иначе = try_lock;
//   void unlock() / unlock_shared()           - освобождение (для не-shared политик shared == эксклюзив).
// Детектор взаимной блокировки встроен через timed-захват + глобальный таймаут trust::runtime.
// ----------------------------------------------------------------------------

/// Конфайнмент: объект доступен ТОЛЬКО в потоке, где он был создан. Нет примитива блокировки -
/// при захвате проверяется, что вызывающий поток == поток создания (иначе ошибка). Таймаут не
/// применяется (проверка мгновенна).
class SyncSingleThreadPolicy {
  public:
    SyncSingleThreadPolicy()
    : owner_(std::this_thread::get_id()) {}

    bool try_lock(const SyncTimeoutType& /*timeout*/) {
        check_owner();
        return true;
    }
    bool try_lock_shared(const SyncTimeoutType& /*timeout*/) {
        check_owner();
        return true;
    }
    void unlock() noexcept {}
    void unlock_shared() noexcept {}

  private:
    void check_owner() const {
        if (std::this_thread::get_id() != owner_) {
            detail::trusted_cpp_error("single-thread sync object used from another thread");
        }
    }
    std::thread::id owner_;
};

/// Обычный (эксклюзивный) mutex. std::timed_mutex для поддержки детектора.
class SyncMutexPolicy {
  public:
    bool try_lock(const SyncTimeoutType& t) { return m_.try_lock_for(t); }
    bool try_lock_shared(const SyncTimeoutType& t) { return m_.try_lock_for(t); }
    void unlock() noexcept { m_.unlock(); }
    void unlock_shared() noexcept { m_.unlock(); }

  private:
    mutable std::timed_mutex m_;
};

/// Timed read/write mutex: разделяемый (read-only) и эксклюзивный (write) режимы.
class SyncRwMutexPolicy {
  public:
    bool try_lock(const SyncTimeoutType& t) { return m_.try_lock_for(t); }
    bool try_lock_shared(const SyncTimeoutType& t) { return m_.try_lock_shared_for(t); }
    void unlock() noexcept { m_.unlock(); }
    void unlock_shared() noexcept { m_.unlock_shared(); }

  private:
    mutable std::shared_timed_mutex m_;
};

/**
 * Strong (owning) reference WITH multi-threaded access control - the "sync" variant.
 * Default Policy = MutexPolicy (simple exclusive synchronization); pass RwMutexPolicy for a
 * shared read-only lock_const() and SyncSingleThreadPolicy for per-thread confinement.
 *
 * Access is ONLY through lock()/lock_const()/try_lock* (which return a Locker). There is no direct
 * raw access: a synchronized reference must be locked to read or write safely.
 * lock()/lock_const() throw trust::IntMinus on a null reference and on deadlock (lock timeout);
 * try_lock* return std::nullopt on an empty reference or lock failure.
 */
template <typename V, typename Policy = SyncMutexPolicy>
class SyncShared {
  public:
    using ValueType = V;
    using WeakType = std::weak_ptr<detail::SyncData<V, Policy>>;
    using SharedType = std::shared_ptr<detail::SyncData<V, Policy>>;

    SyncShared()
    : data_(nullptr) {}

    explicit SyncShared(const V& val)
    : data_(std::make_shared<detail::SyncData<V, Policy>>(val)) {}

    explicit SyncShared(V&& val)
    : data_(std::make_shared<detail::SyncData<V, Policy>>(std::move(val))) {}

    /// Конструктор с пер-объектным таймаутом детектора (перекрывает глобальный).
    SyncShared(const V& val, SyncTimeoutType default_timeout)
    : data_(std::make_shared<detail::SyncData<V, Policy>>(val, default_timeout)) {}

    SyncShared(V&& val, SyncTimeoutType default_timeout)
    : data_(std::make_shared<detail::SyncData<V, Policy>>(std::move(val), default_timeout)) {}

    SyncShared(const SyncShared&) = default;
    SyncShared& operator=(const SyncShared&) = default;

    SyncShared(SyncShared&&) noexcept = default;
    SyncShared& operator=(SyncShared&&) noexcept = default;

    /// Эффективный таймаут значения (0/-1/+ семантика): <0 -> блокировать до конца (max).
    [[nodiscard]] static SyncTimeoutType effective_timeout(SyncTimeoutType t) { return (t < SyncTimeoutType::zero()) ? SyncTimeoutType::max() : t; }

    /// Эффективный глобальный таймаут (без учёта пер-объектного) для фабрик make_auto*.
    [[nodiscard]] static SyncTimeoutType block_timeout() { return effective_timeout(runtime::syncDeadlockTimeout()); }

    /// Эффективный таймаут для блокирующих lock()/lock_const(): пер-объектный default_timeout
    /// перекрывает глобальный. Отрицательное значение = ждать до конца (детектор выкл).
    [[nodiscard]] static SyncTimeoutType block_timeout_for(const SharedType& shared) {
        if (shared && shared->default_timeout.has_value()) {
            return effective_timeout(*shared->default_timeout);
        }
        return effective_timeout(runtime::syncDeadlockTimeout());
    }

    // make_auto / make_auto_const - guarded-capture factory used by Weak<T> (contract).
    static Locker<V, false> make_auto(const SharedType& shared, const SyncTimeoutType& timeout = block_timeout()) {
        if (!shared) {
            detail::trusted_cpp_error("lock: null sync shared reference");
        }
        if (!shared->policy.try_lock(timeout)) {
            detail::trusted_cpp_error("deadlock detected: lock timeout");
        }
        return make_guard(shared, /*shared_mode=*/false);
    }

    static Locker<V, true> make_auto_const(const SharedType& shared, const SyncTimeoutType& timeout = block_timeout()) {
        if (!shared) {
            detail::trusted_cpp_error("lock: null sync shared reference");
        }
        if (!shared->policy.try_lock_shared(timeout)) {
            detail::trusted_cpp_error("deadlock detected: lock timeout");
        }
        return make_guard_const(shared, /*shared_mode=*/true);
    }

    // try_make_auto / try_make_auto_const - non-blocking factories used by Weak<T>::try_lock*.
    // nullopt ONLY on empty reference or lock failure (never swallows a programming error - there
    // is none here besides a null reference, which is handled).
    static std::optional<Locker<V, false>> try_make_auto(const SharedType& shared, const SyncTimeoutType& timeout = SyncTimeoutNoWait) {
        if (!shared) {
            return std::nullopt;
        }
        if (!shared->policy.try_lock(timeout)) {
            return std::nullopt;
        }
        return make_guard(shared, /*shared_mode=*/false);
    }

    static std::optional<Locker<V, true>> try_make_auto_const(const SharedType& shared, const SyncTimeoutType& timeout = SyncTimeoutNoWait) {
        if (!shared) {
            return std::nullopt;
        }
        if (!shared->policy.try_lock_shared(timeout)) {
            return std::nullopt;
        }
        return make_guard_const(shared, /*shared_mode=*/true);
    }

    /// Guarded exclusive capture (Locker<V,false>). Throws on null / deadlock. Без аргумента
    /// используется эффективный таймаут (пер-объектный default_timeout или глобальный).
    [[nodiscard]] Locker<V, false> lock() const { return make_auto(data_, block_timeout_for(data_)); }
    [[nodiscard]] Locker<V, false> lock(const SyncTimeoutType& timeout) const { return make_auto(data_, timeout); }

    /// Guarded read-only capture (Locker<V,true> -> const V&). Throws on null / deadlock.
    [[nodiscard]] Locker<V, true> lock_const() const { return make_auto_const(data_, block_timeout_for(data_)); }
    [[nodiscard]] Locker<V, true> lock_const(const SyncTimeoutType& timeout) const { return make_auto_const(data_, timeout); }

    /// Non-blocking exclusive capture; nullopt if the object is empty or the lock failed.
    [[nodiscard]] std::optional<Locker<V, false>> try_lock(const SyncTimeoutType& timeout = SyncTimeoutNoWait) const {
        if (!data_) {
            return std::nullopt;
        }
        if (!data_->policy.try_lock(timeout)) {
            return std::nullopt;
        }
        return make_guard(data_, /*shared_mode=*/false);
    }

    /// Non-blocking read-only capture; nullopt if the object is empty or the lock failed.
    [[nodiscard]] std::optional<Locker<V, true>> try_lock_const(const SyncTimeoutType& timeout = SyncTimeoutNoWait) const {
        if (!data_) {
            return std::nullopt;
        }
        if (!data_->policy.try_lock_shared(timeout)) {
            return std::nullopt;
        }
        return make_guard_const(data_, /*shared_mode=*/true);
    }

    SyncShared& set(const V& value) {
        auto guard = lock();
        *guard = value;
        return *this;
    }
    SyncShared& set(const V& value, const SyncTimeoutType& timeout) {
        auto guard = lock(timeout);
        *guard = value;
        return *this;
    }

    SyncShared& set(V&& value) {
        auto guard = lock();
        *guard = std::move(value);
        return *this;
    }
    SyncShared& set(V&& value, const SyncTimeoutType& timeout) {
        auto guard = lock(timeout);
        *guard = std::move(value);
        return *this;
    }

    /// Releases the strong reference (the object may be destroyed if no weak refs remain).
    void reset() noexcept { data_.reset(); }

    [[nodiscard]] Weak<SyncShared> weak() const { return Weak<SyncShared>(*this); }

    [[nodiscard]] explicit operator bool() const noexcept { return data_ != nullptr; }

    [[nodiscard]] bool has_value() const noexcept { return data_ != nullptr; }

    [[nodiscard]] SharedType get_shared() const { return data_; }

  private:
    // Build a Locker aliasing the value inside `shared`; the policy lock is already held.
    static Locker<V, false> make_guard(const SharedType& shared, bool shared_mode) {
        auto value_ptr = std::shared_ptr<V>(shared, &shared->value);
        auto release = std::make_shared<detail::SyncRelease<V, Policy>>(shared, shared_mode);
        return Locker<V, false>(std::move(value_ptr), std::move(release));
    }

    static Locker<V, true> make_guard_const(const SharedType& shared, bool shared_mode) {
        auto value_ptr = std::shared_ptr<V>(shared, &shared->value);
        auto release = std::make_shared<detail::SyncRelease<V, Policy>>(shared, shared_mode);
        return Locker<V, true>(std::move(value_ptr), std::move(release));
    }

    SharedType data_;
};

} // namespace trust

#endif // TRUST_TRUSTED_CPP_SYNC_HPP
