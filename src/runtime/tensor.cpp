#include "runtime/tensor.hpp"

#ifdef TRUST_HAS_TORCH

#include <torch/torch.h>

namespace trust {

TorchTensor::TorchTensor(at::Tensor t) : m_handle(std::make_shared<at::Tensor>(std::move(t))) {
}

TorchTensor::TorchTensor(TensorHandle h) : m_handle(std::move(h)) {
}

TensorHandle TorchTensor::as_var_handle() const {
    return m_handle;
}

at::Tensor &TorchTensor::native() {
    return *m_handle.get<at::Tensor>();
}

const at::Tensor &TorchTensor::native() const {
    return *m_handle.get<at::Tensor>();
}

} // namespace trust

#endif // TRUST_HAS_TORCH