#pragma once

#include <memory>
#include <type_traits>

#ifdef TRUST_HAS_TORCH
#include <torch/torch.h>
#endif

namespace trust {

// Forward declarations
class Var;

/**
 * @brief Type-erased handle for tensor backends.
 *
 * Provides type-safe access to a type-erased tensor stored as shared_ptr<void>.
 * Does not require any tensor library headers in the interface.
 */
class TensorHandle {
    friend class TorchTensor;

  public:
    TensorHandle() = default;

    /** Construct from a typed shared_ptr. */
    template <typename T>
    explicit TensorHandle(std::shared_ptr<T> ptr) : m_storage(std::move(ptr)) {}

    /** Check if the stored tensor is of type T. */
    template <typename T>
    [[nodiscard]] bool is() const noexcept {
#ifndef NDEBUG
        if (!m_storage)
            return false;
        // Try to verify type by checking if pointer is valid.
        // Without RTTI we can't fully verify, but we can at least check non-null.
#endif
        return static_cast<bool>(m_storage);
    }

    /** Get typed pointer to the stored tensor. */
    template <typename T>
    [[nodiscard]] T *get() const noexcept {
        if (m_storage) {
            return static_cast<T *>(m_storage.get());
        }
        return nullptr;
    }

    /** Replace the stored tensor with a new typed pointer. */
    template <typename T>
    void set(std::shared_ptr<T> ptr) {
        m_storage = std::move(ptr);
    }

    /** Check if a tensor is held. */
    explicit operator bool() const noexcept { return static_cast<bool>(m_storage); }

    /** Swap two handles. */
    friend void swap(TensorHandle &a, TensorHandle &b) noexcept {
        using std::swap;
        swap(a.m_storage, b.m_storage);
    }

  private:
    std::shared_ptr<void> m_storage;
};

// Always available — loads the tensor runtime plugin
void ensure_tensor_runtime_loaded();

#ifdef TRUST_HAS_TORCH

/**
 * @brief Wrapper providing type-safe access to TensorHandle via at::Tensor.
 *
 * When this header is included with TRUST_HAS_TORCH defined, all torch/torch.h
 * declarations are available. The underlying shared library (libtensor_cpu.so)
 * is auto-loaded on first call to from_var() or as_var_handle().
 */
class TorchTensor {
  public:
    // Construct from Var containing a Tensor
    static TorchTensor from_var(const Var &v);

    // Construct directly from a newly created at::Tensor
    explicit TorchTensor(at::Tensor t);

    // Produce a TensorHandle suitable for storing back into Var
    TensorHandle as_var_handle() const;

    // Direct access — no indirection, all torch functions available
    at::Tensor &native();
    const at::Tensor &native() const;

  private:
    explicit TorchTensor(TensorHandle h);
    TensorHandle m_handle;
};

#endif // TRUST_HAS_TORCH

} // namespace trust
