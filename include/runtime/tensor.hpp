#pragma once

#include "types/tensors.hpp"

#ifdef TRUST_HAS_TORCH
#include <memory>
#include <torch/torch.h>

namespace trust {

class TorchTensor {
  public:
    // Construct directly from a newly created at::Tensor
    explicit TorchTensor(at::Tensor t);

    // Construct from TensorHandle
    explicit TorchTensor(TensorHandle h);

    // Produce a TensorHandle suitable for storing back into Var
    TensorHandle as_var_handle() const;

    // Direct access — no indirection, all torch functions available
    at::Tensor& native();
    const at::Tensor& native() const;

  private:
    TensorHandle m_handle;
};

} // namespace trust

#endif // TRUST_HAS_TORCH