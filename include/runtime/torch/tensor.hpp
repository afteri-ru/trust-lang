#pragma once

#include "stdlib/tensor/tensor.hpp"

#ifdef TRUST_HAS_TORCH

#include <torch/torch.h>

#include <stdexcept>
#include <utility>

namespace trust {

/// Torch-only helpers. Declared in src/runtime/tensor.cpp (tensor_cpu plugin).
extern "C" {
void* trust_tensor_native(TrustTensorHandle handle);
TrustTensorHandle trust_tensor_from_native(const void* at_tensor);
}

/// Thin adapter exposing a torch `at::Tensor` over the plugin's opaque handle.
/// Used by torch-facing tests and by provider code; generated programs never see it.
class TorchTensor {
  public:
    explicit TorchTensor(at::Tensor t)
    : m_handle(trust_tensor_from_native(&t)) {
        if (!m_handle) {
            throw std::runtime_error("trust::TorchTensor: cannot adopt at::Tensor");
        }
    }

    explicit TorchTensor(TensorHandle h)
    : m_handle(std::move(h)) {
        if (!m_handle) {
            throw std::runtime_error("trust::TorchTensor: null TensorHandle");
        }
    }

    TensorHandle as_var_handle() const { return m_handle; }

    at::Tensor& native() {
        auto* p = static_cast<at::Tensor*>(trust_tensor_native(m_handle.get()));
        if (p == nullptr) {
            throw std::runtime_error("trust::TorchTensor: empty native tensor");
        }
        return *p;
    }

    const at::Tensor& native() const { return const_cast<TorchTensor*>(this)->native(); }

  private:
    TensorHandle m_handle;
};

} // namespace trust

#endif // TRUST_HAS_TORCH