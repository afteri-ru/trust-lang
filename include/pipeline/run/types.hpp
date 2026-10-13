// include/pipeline/run/types.hpp
// Value-типы конвейера: запрос прогона и его результат. Не зависит от оркестратора
// (Pipeline) и тяжёлых компонентов (semantic/transpiler/types): потребителям build/run
// достаточно этих определений.
#pragma once

#include "pipeline/backend/backend.hpp"
#include "ast/token.hpp"
#include "ir/cfg.hpp"
#include "driver/options.hpp"
#include "ir/hir_module.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "semantic/semantic_model.hpp"
#include "solver/smt_ast.hpp"
#include "utils/string_interner.hpp"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace trust {

/// SMT-артефакт, произведённый SMT-бэкендом.
struct SmtArtifact {
    solver::SmtScript script;
    bool saved = false; ///< файл записан по SmtTarget::path
};

/// Описание прогона: что считать (steps), в каком режиме (mode) и какие бэкенды нужны.
/// Присутствие `cpp`/`smt` и есть запрос соответствующего бэкенда.
struct RunRequest {
    FrontendSteps steps = FrontendSteps::ParseAST | FrontendSteps::Semantic;
    RunMode mode = RunMode::kProgram;
    std::optional<backend::CppTarget> cpp;
    std::optional<backend::SmtTarget> smt;

    /// Аналитический прогон без программной обвязки (emit-режимы, анализ/символы).
    static RunRequest analysis() {
        RunRequest req;
        req.mode = RunMode::kAnalysis;
        return req;
    }

    /// Разбор + кодогенерация C++ в указанный output-файл source map.
    static RunRequest withCpp(MapperFile index) {
        RunRequest req;
        req.cpp = backend::CppTarget{index, {}};
        return req;
    }
};

/// Результат прогона: AST/символы/артефакты бэкендов (presence = произведено).
/// Артефакты codegen (cpp/header) — из контракта бэкенда; SOLVER-артефакт (SMT-скрипт)
/// принадлежит оркестратору: именно он печатает/исполняет скрипт (`--solver-mode`).
struct PipelineResult : backend::StageOutputs {
    std::optional<std::vector<AstNodePtr>> astNodes;
    /// HIR (клон AST + lowering) — единственный структурный вход C++-бэкенда. `astNodes` при
    /// этом остаётся НЕИЗМЕНЁННОЙ (pre-lowering) и используется solver'ом/диагностиками/LSP.
    /// Порядок узлов соответствует `astNodes` (prelude-часть в результат не попадает).
    std::optional<HirModule> hirNodes;
    /// MIR (CFG поверх HIR) — единственная производная форма для flow-анализов и верификатора.
    /// Заполняется только если гейты HIR/MIR пройдены. Ссылается на узлы HIR; время жизни
    /// обеспечивают `hirNodes` и собственная копия корней в `MirModule::roots`.
    std::optional<flow::MirModule> mir;
    /// Пул строк компиляции (владелец текстов AST/HIR/SymbolIndex). Держится вместе с
    /// результатом, чтобы тексты были валидны и после разрушения Context.
    std::shared_ptr<StringInterner> strings;
    /// Произведённый SMT-артефакт (заполнен, если запрошен `RunRequest::smt`).
    std::optional<SmtArtifact> smt;
    /// Индекс главного модуля (модуль входного файла): граница «свой/чужой» для видимости,
    /// генерация модульных TU (главный не дублируется).
    ModuleIndex mainModuleIndex{};
    /// Семантическая модель (persistent-таблица + проекции) — продукт шага Semantic.
    /// Строится ВСЕГДА при выполненном шаге, а не по запросу потребителя.
    std::optional<SemanticModel> semantics;

    bool isValid() const { return astNodes.has_value(); }
};

} // namespace trust
