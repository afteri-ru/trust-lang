#pragma once

#include <memory>
#include <type_traits>

#include "stdlib/buildin.hpp"
#include "types/value.hpp"

namespace trust {

class Types;

/**
 * @brief Tensor type integrated with the Trust type system.
 *
 * Can be used inside std::variant<Any> and registered in Types.
 */
class Tensor : public SimpleValue<Tensor, TypeKind::DenseTensor> {
  public:
    Tensor() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "Tensor", "Tensor{}"); }
    static void _register(Types &t);
};

class SparseTensor : public SimpleValue<SparseTensor, TypeKind::SparseTensor> {
  public:
    SparseTensor() = default;
    std::string to_string(bool with_type_info) const override { return format_label(with_type_info, "SparseTensor", "SparseTensor{}"); }
    static void _register(Types &t);
};

/**
 * @brief Type-erased handle for tensor backends.
 *
 * Provides type-safe access to a type-erased tensor stored as shared_ptr<void>.
 * Does not require any tensor library headers in the interface.
 */
class TensorHandle {
  public:
    TensorHandle() = default;

    /** Construct from a typed shared_ptr. */
    template <typename T>
    explicit TensorHandle(std::shared_ptr<T> ptr) : m_storage(std::move(ptr)) {}

    /** Check if the stored tensor is of type T. */
    template <typename T>
    [[nodiscard]] bool is() const noexcept {
        return static_cast<bool>(m_storage);
    }

    /** Check if a tensor is held. */
    explicit operator bool() const noexcept { return static_cast<bool>(m_storage); }

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

    /** Swap two handles. */
    friend void swap(TensorHandle &a, TensorHandle &b) noexcept {
        using std::swap;
        swap(a.m_storage, b.m_storage);
    }

  private:
    std::shared_ptr<void> m_storage;
};

// Always available — loads the tensor runtime plugin
// Returns true if the library was loaded successfully, false otherwise.
bool ensure_tensor_runtime_loaded();

} // namespace trust