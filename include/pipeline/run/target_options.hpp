// Целевой контракт из нейтральной модели опций драйвера — ЕДИНСТВЕННАЯ точка вычисления
// (R37/E20). Результат хранит владелец контракта (`TargetContract`); стадии бэкенда читают
// контракт, а не `PipelineOpts`, и повторно его не вычисляют (`recompute-instead-of-query`).
#pragma once

#include "driver/options.hpp"
#include "target/target_info.hpp"

namespace trust {

[[nodiscard]] inline TargetInfo targetInfoFromOptions(const PipelineOpts& opts) {
    TargetInfo info;
    info.provider = opts.compiler;
    return info;
}

} // namespace trust