// trust/tensor_backend.hpp - C ABI of the tensor backend plugin.
//
// The header is delivered by stdlib.trust (include/trust/tensor_backend.hpp) and is the
// ONLY contract between generated programs and the `tensor_cpu` toolchain plugin. Generated
// code never sees libtorch types; `trust::Tensor<Elem>` calls these functions and the plugin
// implements them over libtorch.
//
// Error contract: functions returning a handle return nullptr on failure; functions returning
// int return 0 on success and non-zero on failure. `trust::Tensor<Elem>` converts failures into
// `std::runtime_error` (no silent fallback).

#pragma once

#include <cstdint>

extern "C" {

/// Element dtype of a tensor. The numeric values are ABI-stable.
enum TrustTensorDtype : int32_t {
    TRUST_TENSOR_DTYPE_BOOL = 0,
    TRUST_TENSOR_DTYPE_INT8 = 1,
    TRUST_TENSOR_DTYPE_INT16 = 2,
    TRUST_TENSOR_DTYPE_INT32 = 3,
    TRUST_TENSOR_DTYPE_INT64 = 4,
    TRUST_TENSOR_DTYPE_FLOAT32 = 5,
    TRUST_TENSOR_DTYPE_FLOAT64 = 6,
};

/// Opaque tensor storage (owned by the plugin).
struct TrustTensorOpaque;
using TrustTensorHandle = TrustTensorOpaque*;

/// Creates a dense CUDA-independent (CPU) tensor from a contiguous row-major buffer.
/// `data` may be null when `count == 0` (zero-initialized tensor).
TrustTensorHandle trust_tensor_create(int32_t dtype, const int64_t* dims, int64_t rank, const void* data, int64_t count);

/// Destroys a tensor handle (nullptr is allowed).
void trust_tensor_destroy(TrustTensorHandle handle);

int32_t trust_tensor_dtype(TrustTensorHandle handle);
int64_t trust_tensor_rank(TrustTensorHandle handle);
int64_t trust_tensor_numel(TrustTensorHandle handle);

/// Writes `rank` dimensions into `out`.
int trust_tensor_shape(TrustTensorHandle handle, int64_t* out);

/// Reads/writes one element. Negative indices are normalized as `index + dim`.
int trust_tensor_get(TrustTensorHandle handle, const int64_t* indices, int64_t count, void* out);
int trust_tensor_set(TrustTensorHandle handle, const int64_t* indices, int64_t count, const void* value);

/// Element-wise binary operations (same dtype and broadcast-compatible shapes).
TrustTensorHandle trust_tensor_add(TrustTensorHandle lhs, TrustTensorHandle rhs);
TrustTensorHandle trust_tensor_sub(TrustTensorHandle lhs, TrustTensorHandle rhs);
TrustTensorHandle trust_tensor_mul(TrustTensorHandle lhs, TrustTensorHandle rhs);
TrustTensorHandle trust_tensor_div(TrustTensorHandle lhs, TrustTensorHandle rhs);

/// Element-wise operation with a scalar of the tensor dtype.
TrustTensorHandle trust_tensor_add_scalar(TrustTensorHandle lhs, const void* value);
TrustTensorHandle trust_tensor_sub_scalar(TrustTensorHandle lhs, const void* value);
TrustTensorHandle trust_tensor_mul_scalar(TrustTensorHandle lhs, const void* value);
TrustTensorHandle trust_tensor_div_scalar(TrustTensorHandle lhs, const void* value);

/// Matrix multiplication.
TrustTensorHandle trust_tensor_matmul(TrustTensorHandle lhs, TrustTensorHandle rhs);

/// Sum of all elements (0-dim tensor of the same dtype).
TrustTensorHandle trust_tensor_sum(TrustTensorHandle handle);

/// Returns a view with a new shape (same data).
TrustTensorHandle trust_tensor_reshape(TrustTensorHandle handle, const int64_t* dims, int64_t rank);

/// 1-D inclusive slice `t[start..stop..step]` (negative bounds are normalized; step > 0).
TrustTensorHandle trust_tensor_slice(TrustTensorHandle handle, int64_t start, int64_t stop, int64_t step);

} // extern "C"
