// tensor.cpp - implementation of the tensor_cpu backend (C ABI over libtorch).
//
// Compiled only when WITH_TORCH is enabled (target `tensor_cpu`). Generated programs link
// `libtensor_cpu` and call the C ABI declared in trust/tensor_backend.hpp; libtorch stays
// an implementation detail of this plugin.
#include "runtime/trust/tensor.hpp"

#include <torch/torch.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

struct TrustTensorOpaque {
    at::Tensor value;
};

namespace {

[[noreturn]] void fail(const char* what) {
    throw std::runtime_error(std::string("tensor_cpu: ") + what);
}

at::Tensor& tensor_of(TrustTensorHandle handle) {
    if (handle == nullptr) {
        fail("null tensor handle");
    }
    return handle->value;
}

const at::Tensor& tensor_of_const(TrustTensorHandle handle) {
    if (handle == nullptr) {
        fail("null tensor handle");
    }
    return handle->value;
}

at::ScalarType scalar_type(int32_t dtype) {
    switch (dtype) {
    case TRUST_TENSOR_DTYPE_BOOL:
        return at::kBool;
    case TRUST_TENSOR_DTYPE_INT8:
        return at::kChar;
    case TRUST_TENSOR_DTYPE_INT16:
        return at::kShort;
    case TRUST_TENSOR_DTYPE_INT32:
        return at::kInt;
    case TRUST_TENSOR_DTYPE_INT64:
        return at::kLong;
    case TRUST_TENSOR_DTYPE_FLOAT32:
        return at::kFloat;
    case TRUST_TENSOR_DTYPE_FLOAT64:
        return at::kDouble;
    default:
        fail("unknown dtype");
    }
}

int32_t dtype_of(at::ScalarType type) {
    switch (type) {
    case at::kBool:
        return TRUST_TENSOR_DTYPE_BOOL;
    case at::kChar:
        return TRUST_TENSOR_DTYPE_INT8;
    case at::kShort:
        return TRUST_TENSOR_DTYPE_INT16;
    case at::kInt:
        return TRUST_TENSOR_DTYPE_INT32;
    case at::kLong:
        return TRUST_TENSOR_DTYPE_INT64;
    case at::kFloat:
        return TRUST_TENSOR_DTYPE_FLOAT32;
    case at::kDouble:
        return TRUST_TENSOR_DTYPE_FLOAT64;
    default:
        fail("unsupported tensor dtype");
    }
}

int64_t flat_offset(const at::Tensor& t, const int64_t* indices, int64_t count) {
    const int64_t rank = static_cast<int64_t>(t.dim());
    if (count == 1 && rank != 1) {
        // Плоский доступ `t[i]` (и `item()` для 0-dim): индекс по всему буферу.
        int64_t idx = indices[0];
        const int64_t numel = t.numel();
        if (idx < 0) {
            idx += numel;
        }
        if (idx < 0 || idx >= numel) {
            fail("flat index out of range");
        }
        return idx;
    }
    if (count != rank) {
        fail("index count does not match tensor rank");
    }
    int64_t offset = 0;
    for (int64_t axis = 0; axis < rank; ++axis) {
        const int64_t dim = t.size(axis);
        int64_t idx = indices[axis];
        if (idx < 0) {
            idx += dim;
        }
        if (idx < 0 || idx >= dim) {
            fail("index out of range");
        }
        offset += idx * t.stride(axis);
    }
    return offset;
}

TrustTensorHandle wrap(at::Tensor value) {
    return new TrustTensorOpaque{std::move(value)};
}

template <typename Fn>
TrustTensorHandle binary_impl(TrustTensorHandle lhs, TrustTensorHandle rhs, Fn&& fn) {
    const at::Tensor& a = tensor_of_const(lhs);
    const at::Tensor& b = tensor_of_const(rhs);
    if (a.scalar_type() != b.scalar_type()) {
        fail("element types differ");
    }
    return wrap(fn(a, b));
}

at::Scalar scalar_from(const void* value, int32_t dtype) {
    switch (dtype) {
    case TRUST_TENSOR_DTYPE_BOOL:
        return at::Scalar(*static_cast<const bool*>(value));
    case TRUST_TENSOR_DTYPE_INT8:
        return at::Scalar(static_cast<int64_t>(*static_cast<const int8_t*>(value)));
    case TRUST_TENSOR_DTYPE_INT16:
        return at::Scalar(static_cast<int64_t>(*static_cast<const int16_t*>(value)));
    case TRUST_TENSOR_DTYPE_INT32:
        return at::Scalar(static_cast<int64_t>(*static_cast<const int32_t*>(value)));
    case TRUST_TENSOR_DTYPE_INT64:
        return at::Scalar(*static_cast<const int64_t*>(value));
    case TRUST_TENSOR_DTYPE_FLOAT32:
        return at::Scalar(*static_cast<const float*>(value));
    case TRUST_TENSOR_DTYPE_FLOAT64:
        return at::Scalar(*static_cast<const double*>(value));
    default:
        fail("unknown scalar dtype");
    }
}

template <typename Fn>
TrustTensorHandle scalar_impl(TrustTensorHandle lhs, const void* value, Fn&& fn) {
    const at::Tensor& a = tensor_of_const(lhs);
    const int32_t dtype = dtype_of(a.scalar_type());
    return wrap(fn(a, scalar_from(value, dtype)));
}

} // namespace

extern "C" {

TrustTensorHandle trust_tensor_create(int32_t dtype, const int64_t* dims, int64_t rank, const void* data, int64_t count) {
    try {
        std::vector<int64_t> sizes(dims, dims + rank);
        at::Tensor t = at::empty(sizes, at::TensorOptions().dtype(scalar_type(dtype)).device(at::kCPU));
        if (data == nullptr) {
            // Zero-initialized tensor of the declared shape.
            t.zero_();
        } else if (count > 0) {
            if (count != t.numel()) {
                fail("data size does not match the shape");
            }
            std::memcpy(t.data_ptr(), data, static_cast<size_t>(t.nbytes()));
        }
        return wrap(std::move(t));
    } catch (...) {
        return nullptr;
    }
}

void trust_tensor_destroy(TrustTensorHandle handle) {
    delete handle;
}

int32_t trust_tensor_dtype(TrustTensorHandle handle) {
    try {
        return dtype_of(tensor_of_const(handle).scalar_type());
    } catch (...) {
        return -1;
    }
}

int64_t trust_tensor_rank(TrustTensorHandle handle) {
    try {
        return static_cast<int64_t>(tensor_of_const(handle).dim());
    } catch (...) {
        return -1;
    }
}

int64_t trust_tensor_numel(TrustTensorHandle handle) {
    try {
        return tensor_of_const(handle).numel();
    } catch (...) {
        return -1;
    }
}

int trust_tensor_shape(TrustTensorHandle handle, int64_t* out) {
    try {
        const at::Tensor& t = tensor_of_const(handle);
        for (int64_t i = 0; i < static_cast<int64_t>(t.dim()); ++i) {
            out[i] = t.size(i);
        }
        return 0;
    } catch (...) {
        return 1;
    }
}

int trust_tensor_get(TrustTensorHandle handle, const int64_t* indices, int64_t count, void* out) {
    try {
        const at::Tensor& t = tensor_of_const(handle);
        const int64_t offset = flat_offset(t, indices, count);
        const size_t bytes = static_cast<size_t>(t.element_size());
        std::memcpy(out, static_cast<const char*>(t.const_data_ptr()) + offset * static_cast<int64_t>(bytes), bytes);
        return 0;
    } catch (...) {
        return 1;
    }
}

int trust_tensor_set(TrustTensorHandle handle, const int64_t* indices, int64_t count, const void* value) {
    try {
        at::Tensor& t = tensor_of(handle);
        const int64_t offset = flat_offset(t, indices, count);
        const size_t bytes = static_cast<size_t>(t.element_size());
        std::memcpy(static_cast<char*>(t.data_ptr()) + offset * static_cast<int64_t>(bytes), value, bytes);
        return 0;
    } catch (...) {
        return 1;
    }
}

TrustTensorHandle trust_tensor_add(TrustTensorHandle lhs, TrustTensorHandle rhs) {
    try {
        return binary_impl(lhs, rhs, [](const at::Tensor& a, const at::Tensor& b) { return at::add(a, b); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_sub(TrustTensorHandle lhs, TrustTensorHandle rhs) {
    try {
        return binary_impl(lhs, rhs, [](const at::Tensor& a, const at::Tensor& b) { return at::sub(a, b); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_mul(TrustTensorHandle lhs, TrustTensorHandle rhs) {
    try {
        return binary_impl(lhs, rhs, [](const at::Tensor& a, const at::Tensor& b) { return at::mul(a, b); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_div(TrustTensorHandle lhs, TrustTensorHandle rhs) {
    try {
        return binary_impl(lhs, rhs, [](const at::Tensor& a, const at::Tensor& b) {
            const at::ScalarType dtype = a.scalar_type();
            at::Tensor result = at::div(a, b);
            // Integer `/` in TrustLang keeps the tensor dtype (torch would promote to float).
            if (dtype != at::kFloat && dtype != at::kDouble && dtype != at::kHalf && dtype != at::kBFloat16) {
                result = result.to(dtype);
            }
            return result;
        });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_add_scalar(TrustTensorHandle lhs, const void* value) {
    try {
        return scalar_impl(lhs, value, [](const at::Tensor& a, const at::Scalar& v) { return at::add(a, v); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_sub_scalar(TrustTensorHandle lhs, const void* value) {
    try {
        return scalar_impl(lhs, value, [](const at::Tensor& a, const at::Scalar& v) { return at::sub(a, v); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_mul_scalar(TrustTensorHandle lhs, const void* value) {
    try {
        return scalar_impl(lhs, value, [](const at::Tensor& a, const at::Scalar& v) { return at::mul(a, v); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_div_scalar(TrustTensorHandle lhs, const void* value) {
    try {
        return scalar_impl(lhs, value, [](const at::Tensor& a, const at::Scalar& v) {
            at::Tensor result = at::div(a, v);
            const at::ScalarType dtype = a.scalar_type();
            if (dtype != at::kFloat && dtype != at::kDouble && dtype != at::kHalf && dtype != at::kBFloat16) {
                result = result.to(dtype);
            }
            return result;
        });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_matmul(TrustTensorHandle lhs, TrustTensorHandle rhs) {
    try {
        return binary_impl(lhs, rhs, [](const at::Tensor& a, const at::Tensor& b) { return at::matmul(a, b); });
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_sum(TrustTensorHandle handle) {
    try {
        const at::Tensor& t = tensor_of_const(handle);
        if (t.scalar_type() == at::kBool) {
            fail("sum is not defined for Bool tensors");
        }
        return wrap(at::sum(t).to(t.scalar_type()));
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_reshape(TrustTensorHandle handle, const int64_t* dims, int64_t rank) {
    try {
        const at::Tensor& t = tensor_of_const(handle);
        std::vector<int64_t> sizes(dims, dims + rank);
        return wrap(t.reshape(sizes));
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_slice(TrustTensorHandle handle, int64_t start, int64_t stop, int64_t step) {
    try {
        const at::Tensor& t = tensor_of_const(handle);
        if (t.dim() != 1) {
            fail("slice requires a one-dimensional tensor");
        }
        if (step <= 0) {
            fail("slice step must be positive");
        }
        const int64_t n = t.size(0);
        int64_t s = start < 0 ? start + n : start;
        int64_t e = stop < 0 ? stop + n : stop;
        if (s < 0 || e < 0 || s >= n || e >= n || s > e) {
            fail("slice bounds out of range");
        }
        return wrap(t.slice(0, s, e + 1, step));
    } catch (...) {
        return nullptr;
    }
}

/// Torch-only helpers used by `trust::TorchTensor` (runtime/torch/tensor.hpp). Not part of the
/// generated-code contract; declared in the torch-facing header only.
void* trust_tensor_native(TrustTensorHandle handle) {
    try {
        return &tensor_of(handle);
    } catch (...) {
        return nullptr;
    }
}

TrustTensorHandle trust_tensor_from_native(const void* at_tensor) {
    try {
        if (at_tensor == nullptr) {
            fail("null at::Tensor");
        }
        return wrap(*static_cast<const at::Tensor*>(at_tensor));
    } catch (...) {
        return nullptr;
    }
}

} // extern "C"
