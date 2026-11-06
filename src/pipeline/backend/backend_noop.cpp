// Стаб-стадия бэкенда (R37): доказательство, что бэкенд подключается СТРОКОЙ таблицы
// `BACKEND_STAGE_LIST` и потребляет контракт `BackendRequest` (HIR/MIR) ДАННЫМИ, без зависимости от
// `transpiler`/`stdlib` и без правок языковой модели. Ничего не эмитит (NoWork эквивалент): стадия
// читает только HIR-контракт. TU намеренно не включает заголовков `transpiler`/`stdlib`.
#include "pipeline/backend/stages.hpp"

#include "utils/error.hpp"

namespace trust {
namespace backend {

bool noopStageRequested(const BackendRequest& req) noexcept {
    return req.noop;
}

StageOutcome runNoopStage(BackendServices&, const BackendRequest& req, StageOutputs&) {
    // Предпосылку `needsHir` проверяет оркестратор (runBackendStages); здесь — контракт стадии.
    EXPECT(req.hir != nullptr && "noop stage requires HIR");
    return StageOutcome::Done;
}

} // namespace backend
} // namespace trust