#pragma once

// Компилятор C++ по умолчанию (из конфигурации CMake). Нейтральный сгенерированный заголовок
// пакета `trust`: значение нужно драйверу/CLI (`PipelineOpts::compiler`), но не принадлежит
// компоненту `toolchain` (иначе `pipeline` зависел бы от него — E20/R34).
#define TRUST_DEFAULT_COMPILER "@CMAKE_CXX_COMPILER@"
