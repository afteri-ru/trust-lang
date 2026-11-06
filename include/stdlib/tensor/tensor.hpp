// stdlib/tensor/tensor.hpp - stable, self-contained tensor value type for the Trust runtime.
// Delivery: package file `include/stdlib/tensor.hpp` inside share/trust/tensor.trust
// (available via the tensor package include path). The header intentionally exposes NO libtorch
// types: the tensor backend is an implementation detail of the `tensor_cpu` toolchain plugin
// (see tensor_backend.hpp). The `tensor.trust` provider package declares that plugin
// as its artifact; the plugin itself links libtorch.
// `trust::Tensor<Elem>` is the C++ representation of the Core TrustLang type `:Tensor`:
// the backend handle owns the storage, the element type is part of the Trust type system
// (dtype/shape checks are compile-time), and copy is cheap (shared handle).

#pragma once

#include "tensor_backend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace trust {

namespace detail {

[[noreturn]] inline void tensor_fail(const char* what) {
    throw std::runtime_error(std::string("tensor: ") + what);
}

inline int64_t tensor_numel(const std::vector<int64_t>& dims) {
    int64_t n = 1;
    for (const int64_t d : dims) {
        if (d < 0) {
            tensor_fail("negative dimension");
        }
        n *= d;
    }
    return n;
}

template <typename T>
struct TensorDtypeOf {
    static constexpr bool supported = false;
    static constexpr int32_t value = -1;
};
template <>
struct TensorDtypeOf<bool> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_BOOL;
};
template <>
struct TensorDtypeOf<int8_t> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_INT8;
};
template <>
struct TensorDtypeOf<int16_t> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_INT16;
};
template <>
struct TensorDtypeOf<int32_t> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_INT32;
};
template <>
struct TensorDtypeOf<int64_t> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_INT64;
};
template <>
struct TensorDtypeOf<float> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_FLOAT32;
};
template <>
struct TensorDtypeOf<double> {
    static constexpr bool supported = true;
    static constexpr int32_t value = TRUST_TENSOR_DTYPE_FLOAT64;
};

template <typename T>
inline constexpr int32_t tensor_dtype_v = TensorDtypeOf<std::remove_cv_t<T>>::value;

} // namespace detail

/**
 * @brief Type-erased owner of a backend tensor.
 *
 * dtype/rank/shape are queried from the backend; `Tensor<Elem>` supplies the compile-time
 * element type.
 */
class TensorHandle {
  public:
    TensorHandle() = default;

    explicit TensorHandle(TrustTensorHandle handle)
    : m_storage(handle, &TensorHandle::destroy) {}

    [[nodiscard]] TrustTensorHandle get() const noexcept { return m_storage.get(); }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(m_storage); }

    [[nodiscard]] int32_t dtype() const {
        if (!m_storage) {
            detail::tensor_fail("dtype of an empty handle");
        }
        return trust_tensor_dtype(m_storage.get());
    }

    [[nodiscard]] int64_t rank() const {
        if (!m_storage) {
            detail::tensor_fail("rank of an empty handle");
        }
        return trust_tensor_rank(m_storage.get());
    }

    [[nodiscard]] int64_t numel() const {
        if (!m_storage) {
            detail::tensor_fail("numel of an empty handle");
        }
        return trust_tensor_numel(m_storage.get());
    }

    [[nodiscard]] std::vector<int64_t> shape() const {
        if (!m_storage) {
            detail::tensor_fail("shape of an empty handle");
        }
        const int64_t r = trust_tensor_rank(m_storage.get());
        std::vector<int64_t> out(static_cast<size_t>(r));
        if (r > 0 && trust_tensor_shape(m_storage.get(), out.data()) != 0) {
            detail::tensor_fail("backend failed to read the shape");
        }
        return out;
    }

    void reset() noexcept { m_storage.reset(); }

    friend void swap(TensorHandle& a, TensorHandle& b) noexcept { a.m_storage.swap(b.m_storage); }

  private:
    static void destroy(TrustTensorOpaque* p) noexcept {
        if (p != nullptr) {
            trust_tensor_destroy(p);
        }
    }

    std::shared_ptr<TrustTensorOpaque> m_storage;
};

/**
 * @brief Reference to one element of a tensor (read/write proxy).
 *
 * Required because the element lives inside the plugin and C++ needs a reference-like
 * object for `t[i] = value` / `value = t[i]`.
 */
template <typename Elem>
class TensorElementRef {
  public:
    TensorElementRef(TensorHandle handle, std::vector<int64_t> indices)
    : m_handle(std::move(handle))
    , m_indices(std::move(indices)) {}

    operator Elem() const { // NOLINT(google-explicit-constructor): element read
        Elem value{};
        if (trust_tensor_get(m_handle.get(), m_indices.data(), static_cast<int64_t>(m_indices.size()), &value) != 0) {
            detail::tensor_fail("index out of range");
        }
        return value;
    }

    TensorElementRef& operator=(Elem value) {
        if (trust_tensor_set(m_handle.get(), m_indices.data(), static_cast<int64_t>(m_indices.size()), &value) != 0) {
            detail::tensor_fail("index out of range");
        }
        return *this;
    }

    TensorElementRef& operator=(const TensorElementRef& other) { return *this = static_cast<Elem>(other); }

  private:
    TensorHandle m_handle;
    std::vector<int64_t> m_indices;
};

/**
 * @brief Dense tensor value with a compile-time element type.
 *
 * Shape is a runtime property (checked by the compiler when it is statically known).
 * Copies share the backend storage, matching tensor value semantics.
 */
template <typename Elem>
class Tensor {
    static_assert(detail::TensorDtypeOf<Elem>::supported, "trust::Tensor<Elem>: unsupported element type");

  public:
    using value_type = Elem;

    Tensor() = default;

    explicit Tensor(TensorHandle handle)
    : m_handle(std::move(handle)) {
        if (!m_handle) {
            detail::tensor_fail("null handle");
        }
        if (m_handle.dtype() != detail::tensor_dtype_v<Elem>) {
            detail::tensor_fail("handle dtype does not match the element type");
        }
    }

    /// Creates a tensor from a shape and row-major data (`product(shape) == data.size()`).
    Tensor(std::initializer_list<int64_t> shape, std::initializer_list<Elem> data)
    : Tensor(make_handle(shape, data.begin(), static_cast<int64_t>(data.size()))) {}

    Tensor(std::initializer_list<int64_t> shape, const std::vector<Elem>& data)
    : Tensor(make_handle(shape, data.data(), static_cast<int64_t>(data.size()))) {}

    /// Zero-initialized tensor of the given shape.
    explicit Tensor(std::initializer_list<int64_t> shape)
    : Tensor(make_handle(shape, nullptr, 0)) {}

    [[nodiscard]] bool valid() const noexcept { return static_cast<bool>(m_handle); }
    [[nodiscard]] explicit operator bool() const noexcept { return valid(); }
    [[nodiscard]] bool empty() const { return numel() == 0; }

    [[nodiscard]] int32_t dtype() const { return m_handle.dtype(); }
    [[nodiscard]] int64_t rank() const { return m_handle.rank(); }
    [[nodiscard]] int64_t size() const { return m_handle.numel(); }
    [[nodiscard]] int64_t numel() const { return m_handle.numel(); }
    [[nodiscard]] std::vector<int64_t> shape() const { return m_handle.shape(); }
    [[nodiscard]] const TensorHandle& handle() const noexcept { return m_handle; }

    /// Single-index element access (rank-1 tensors; flat order for other ranks).
    TensorElementRef<Elem> operator[](int64_t index) { return TensorElementRef<Elem>(m_handle, {index}); }
    [[nodiscard]] Elem operator[](int64_t index) const { return TensorElementRef<Elem>(m_handle, {index}); }

    /// Multi-index element access `t(i, j, ...)`; negative indices are supported.
    template <typename... Idx>
        requires(sizeof...(Idx) > 0 && (std::is_integral_v<std::remove_cvref_t<Idx>> && ...))
    TensorElementRef<Elem> operator()(Idx... idx) {
        return TensorElementRef<Elem>(m_handle, {static_cast<int64_t>(idx)...});
    }

    /// Bounds-checked alias of operator() (used by the Trust method `at`).
    template <typename... Idx>
        requires(sizeof...(Idx) > 0 && (std::is_integral_v<std::remove_cvref_t<Idx>> && ...))
    TensorElementRef<Elem> at(Idx... idx) {
        return TensorElementRef<Elem>(m_handle, {static_cast<int64_t>(idx)...});
    }

    template <typename... Idx>
        requires(sizeof...(Idx) > 0 && (std::is_integral_v<std::remove_cvref_t<Idx>> && ...))
    [[nodiscard]] Elem at(Idx... idx) const {
        return TensorElementRef<Elem>(m_handle, {static_cast<int64_t>(idx)...});
    }

    template <typename... Idx>
        requires(sizeof...(Idx) > 0 && (std::is_integral_v<std::remove_cvref_t<Idx>> && ...))
    [[nodiscard]] Elem operator()(Idx... idx) const {
        return TensorElementRef<Elem>(m_handle, {static_cast<int64_t>(idx)...});
    }

    /// The only element of a 1-element tensor.
    [[nodiscard]] Elem item() const {
        if (numel() != 1) {
            detail::tensor_fail("item() requires exactly one element");
        }
        return static_cast<Elem>(TensorElementRef<Elem>(m_handle, {0}));
    }

    [[nodiscard]] Tensor sum() const {
        const TrustTensorHandle h = trust_tensor_sum(m_handle.get());
        if (h == nullptr) {
            detail::tensor_fail("sum failed");
        }
        return Tensor(TensorHandle(h));
    }

    [[nodiscard]] Tensor reshape(std::initializer_list<int64_t> shape) const {
        std::vector<int64_t> dims(shape);
        return reshape_impl(dims.data(), static_cast<int64_t>(dims.size()));
    }

    /// Variadic form used by generated code: `t.reshape(2, 3)`.
    template <typename... Dims>
        requires(sizeof...(Dims) > 0 && (std::is_integral_v<std::remove_cvref_t<Dims>> && ...))
    [[nodiscard]] Tensor reshape(Dims... dims) const {
        const std::array<int64_t, sizeof...(Dims)> shape{static_cast<int64_t>(dims)...};
        return reshape_impl(shape.data(), static_cast<int64_t>(shape.size()));
    }

    [[nodiscard]] Tensor matmul(const Tensor& other) const {
        const TrustTensorHandle h = trust_tensor_matmul(m_handle.get(), other.m_handle.get());
        if (h == nullptr) {
            detail::tensor_fail("matmul failed");
        }
        return Tensor(TensorHandle(h));
    }

    /// 1-D inclusive slice (used by `t[a..b..step]`).
    [[nodiscard]] Tensor slice(int64_t start, int64_t stop, int64_t step = 1) const {
        const TrustTensorHandle h = trust_tensor_slice(m_handle.get(), start, stop, step);
        if (h == nullptr) {
            detail::tensor_fail("slice failed");
        }
        return Tensor(TensorHandle(h));
    }

    [[nodiscard]] Tensor operator+(const Tensor& other) const { return binary(other, trust_tensor_add); }
    [[nodiscard]] Tensor operator-(const Tensor& other) const { return binary(other, trust_tensor_sub); }
    [[nodiscard]] Tensor operator*(const Tensor& other) const { return binary(other, trust_tensor_mul); }
    [[nodiscard]] Tensor operator/(const Tensor& other) const { return binary(other, trust_tensor_div); }

    [[nodiscard]] Tensor operator+(Elem value) const { return scalar(value, trust_tensor_add_scalar); }
    [[nodiscard]] Tensor operator-(Elem value) const { return scalar(value, trust_tensor_sub_scalar); }
    [[nodiscard]] Tensor operator*(Elem value) const { return scalar(value, trust_tensor_mul_scalar); }
    [[nodiscard]] Tensor operator/(Elem value) const { return scalar(value, trust_tensor_div_scalar); }

    friend Tensor operator+(Elem value, const Tensor& t) { return t + value; }
    friend Tensor operator-(Elem value, const Tensor& t) { return t.scalar_reversed(value, trust_tensor_sub); }
    friend Tensor operator*(Elem value, const Tensor& t) { return t * value; }
    friend Tensor operator/(Elem value, const Tensor& t) { return t.scalar_reversed(value, trust_tensor_div); }

  private:
    using BinaryFn = TrustTensorHandle (*)(TrustTensorHandle, TrustTensorHandle);
    using ScalarFn = TrustTensorHandle (*)(TrustTensorHandle, const void*);

    static TensorHandle make_handle(std::initializer_list<int64_t> shape, const Elem* data, int64_t count) {
        std::vector<int64_t> dims(shape);
        const int64_t expected = detail::tensor_numel(dims);
        if (count != 0 && count != expected) {
            detail::tensor_fail("initializer size does not match the shape");
        }
        const TrustTensorHandle h = trust_tensor_create(detail::tensor_dtype_v<Elem>, dims.data(), static_cast<int64_t>(dims.size()), data, expected);
        if (h == nullptr) {
            detail::tensor_fail("backend failed to create a tensor");
        }
        return TensorHandle(h);
    }

    Tensor reshape_impl(const int64_t* dims, int64_t rank) const {
        std::vector<int64_t> shape(dims, dims + rank);
        if (detail::tensor_numel(shape) != numel()) {
            detail::tensor_fail("reshape changes the number of elements");
        }
        const TrustTensorHandle h = trust_tensor_reshape(m_handle.get(), dims, rank);
        if (h == nullptr) {
            detail::tensor_fail("reshape failed");
        }
        return Tensor(TensorHandle(h));
    }

    Tensor binary(const Tensor& other, BinaryFn fn) const {
        if (!m_handle || !other.m_handle) {
            detail::tensor_fail("operation on an empty tensor");
        }
        if (m_handle.dtype() != other.m_handle.dtype()) {
            detail::tensor_fail("element types differ");
        }
        const TrustTensorHandle h = fn(m_handle.get(), other.m_handle.get());
        if (h == nullptr) {
            detail::tensor_fail("element-wise operation failed");
        }
        return Tensor(TensorHandle(h));
    }

    /// `value op t`: materialize `value` as a 1-element tensor and reuse the binary op.
    Tensor scalar_reversed(Elem value, BinaryFn fn) const {
        const Tensor tmp({1}, {value});
        const TrustTensorHandle h = fn(tmp.m_handle.get(), m_handle.get());
        if (h == nullptr) {
            detail::tensor_fail("scalar operation failed");
        }
        return Tensor(TensorHandle(h));
    }

    Tensor scalar(Elem value, ScalarFn fn) const {
        if (!m_handle) {
            detail::tensor_fail("operation on an empty tensor");
        }
        const TrustTensorHandle h = fn(m_handle.get(), &value);
        if (h == nullptr) {
            detail::tensor_fail("scalar operation failed");
        }
        return Tensor(TensorHandle(h));
    }

    TensorHandle m_handle;
};

} // namespace trust
