#include "types/tensors.hpp"
#include "types/types.hpp"

#include <dlfcn.h>
#include <mutex>
#include <stdexcept>

namespace trust {

void Tensor::_register(Types& t) {
    t.add(TypeInfo(TypeKind::DenseTensor, "trust::Tensor"));

    t.add(TypeInfo(TypeKind::Tensors, "trust::Tensor"));

    t.add_headers(TypeKind::Tensors, {"torch.h"});
    t.add_libraries(TypeKind::Tensors, {"runtime"});
}

void SparseTensor::_register(Types& t) {
    t.add(TypeInfo(TypeKind::SparseTensor, "trust::SparseTensor"));
}

bool ensure_tensor_runtime_loaded() {
    static std::once_flag flag;
    static bool result = false;
    std::call_once(flag, [] {
        const char* candidates[] = {
            "libtensor_cpu.so",
            "./libtensor_cpu.so",
#ifdef TRUST_PLUGIN_DIR
            TRUST_PLUGIN_DIR "/libtensor_cpu.so",
#endif
            nullptr,
        };

        for (int i = 0; candidates[i] != nullptr; ++i) {
            void* handle = dlopen(candidates[i], RTLD_NOW | RTLD_GLOBAL);
            if (handle) {
                result = true;
                return;
            }
        }
    });
    return result;
}

} // namespace trust