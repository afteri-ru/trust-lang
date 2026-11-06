// Зеркалит запрещённую границу: конвейер компиляции включает заголовок слоя сборки (toolchain).
#include "toolchain/toolchain.hpp"

int pipeline_uses_toolchain() { return toolchain::value(); }
