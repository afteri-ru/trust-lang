#include "runtime/tensor.hpp"
#include "runtime/var.hpp"

#include <dlfcn.h>
#include <mutex>
#include <stdexcept>

namespace trust {

void ensure_tensor_runtime_loaded() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        // Try to load the plugin from several known locations.
        const char *candidates[] = {
            "libtensor_cpu.so",
            "./libtensor_cpu.so",
#ifdef TRUST_PLUGIN_DIR
            TRUST_PLUGIN_DIR "/libtensor_cpu.so",
#endif
            nullptr,
        };

        for (int i = 0; candidates[i] != nullptr; ++i) {
            void *handle = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
            if (handle)
                return; // loaded successfully
        }

        // If all attempts fail, throw — tensor operations are unavailable.
        throw std::runtime_error("Cannot load libtensor_cpu.so: " + std::string(dlerror()));
    });
}

#ifdef TRUST_HAS_TORCH

TorchTensor TorchTensor::from_var(const Var &v) {
    TensorHandle handle = v.as<TensorHandle>();
    at::Tensor *ptr = handle.get<at::Tensor>();
    if (!ptr) {
        throw std::runtime_error("TensorHandle does not contain at::Tensor");
    }
    return TorchTensor(*ptr);
}

TorchTensor::TorchTensor(at::Tensor t) : m_handle(std::make_shared<at::Tensor>(std::move(t))) {
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

TorchTensor::TorchTensor(TensorHandle h) : m_handle(std::move(h)) {
}

#endif // TRUST_HAS_TORCH

} // namespace trust