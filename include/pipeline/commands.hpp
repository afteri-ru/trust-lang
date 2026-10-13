// include/pipeline/commands.hpp
// CLI/use-case команды поверх конвейера: лексемы, solver, emit и компиляция.
// Не являются методами Pipeline (CLI-политика отделена от ядра компиляции).
#pragma once

#include "driver/options.hpp"
#include "location/location.hpp"

namespace trust {

class Context;
class Pipeline;

int runLexemesCommand(Context& ctx, const PipelineOpts& opts, MapperFile inputFile);
int runSolverCommand(Pipeline& pipeline, Context& ctx, const PipelineOpts& opts, MapperFile inputFile);
int runOtherEmitCommand(Pipeline& pipeline, Context& ctx, const PipelineOpts& opts, MapperFile inputFile);

} // namespace trust
