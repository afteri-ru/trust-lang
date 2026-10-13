#pragma once

// include/pipeline/backend/backend.hpp
// Контракт бэкенда: value-типы стадий (запрос, цели, артефакты, окружение).
//
// Инварианты:
//   * контракт не знает про оркестратор (`Pipeline`) и агрегат прогона (`PipelineResult`);
//     стадия получает `BackendRequest` и пишет результат в `StageOutputs`;
//   * реализации стадий живут у владельцев (C++ — `pipeline`, SMT — `solver`, LLVM — новый
//     компонент), поэтому LLVM-бэкенд добавляется реализацией контракта без правок `Pipeline`;
//   * `StageOutputs::NoWork` («контрактов нет») не останавливает конвейер, `Error` останавливает
//     только при `stopOnFailure`.

#include "analysis/modes.hpp"
#include "driver/options.hpp"
#include "ir/cfg.hpp"
#include "ir/hir_module.hpp"
#include "location/location.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace trust {

class Context;
class ModuleLoader;

namespace backend {

/// C++-артефакт, произведённый C++-бэкендом.
struct CppArtifact {
    MapperFile index;                        ///< output-запись в source map
    std::filesystem::path path;              ///< путь назначения (пусто - только в памяти)
    std::vector<std::string> packageHeaders; ///< рантайм-заголовки, реально использованные кодом
    std::vector<std::string> linkLibs;       ///< `-l<имя>` из `@[link("имя")]`
    std::string entryParams;                 ///< C++-типы параметров entry (без имён, через ", ")
    bool sawEntry = false;                   ///< в ходе генерации эмитилась entry-функция
    bool saved = false;                      ///< файл записан по CppTarget::path
};

/// Заголовок модуля (`<enc>.hpp`) — интерфейсная проекция модуля для клиентов.
struct HeaderArtifact {
    MapperFile index;                        ///< output-запись в source map
    std::filesystem::path path;              ///< путь назначения
    std::vector<std::string> packageHeaders; ///< рантайм-заголовки, использованные интерфейсом
    std::vector<std::string> linkLibs;       ///< `-l<имя>` из нативных деклараций интерфейса
    /// Узлы (AST) объявлений, чьё ПОЛНОЕ определение уже эмитировано в заголовке: типы и
    /// generic/constexpr free-функции. Единица трансляции модуля их не повторяет (иначе
    /// redefinition); прочие функции/переменные в заголовке только объявлены.
    std::vector<const AstNodeBase*> publishedDeclarations;
    bool saved = false; ///< файл записан по path
};

/// Куда положить C++-артефакт.
struct CppTarget {
    MapperFile index;           ///< обязательный output-файл в source map
    std::filesystem::path path; ///< путь назначения; пусто - артефакт только в памяти
};

/// Куда положить SMT-артефакт.
struct SmtTarget {
    std::filesystem::path path; ///< путь назначения (`.smt2`); пусто - только в памяти
};

/// Информация о целевом платформенном контракте (C++ standard, ABI/имя target — по мере LLVM).
struct TargetInfo {
    std::string cpp_standard = "c++23";
};

enum class StageOutcome {
    Done,   ///< стадия выполнила работу (артефакт произведён/записан)
    NoWork, ///< стадии нечего делать (например, контрактов нет); другие стадии продолжаются
    Error,  ///< ошибка уже диагностирована; остановка зависит от stopOnFailure
};

/// Окружение стадии, предоставляемое оркестратором (сама стадия владельцем окружения не является).
struct BackendServices {
    Context& ctx;
    const PipelineOpts& opts;
    ModuleLoader& loader;
};

/// Запрос стадии: производные представления, режимы и цели. Указатели невладеющие: данные живут
/// в агрегате прогона, которым владеет оркестратор. `nullptr` = представление не построено
/// (прогон без семантики/после ICE): стадия, которой оно нужно, не запускается — оркестратор
/// сообщает внутреннюю ошибку, а не молча пропускает работу.
struct BackendRequest {
    const HirModule* hir = nullptr;
    const flow::MirModule* mir = nullptr;
    const analysis::BehavioralModes& modes;
    ModuleIndex mainModuleIndex{};
    std::optional<CppTarget> cpp; ///< запрос C++/codegen-стадии
    std::optional<SmtTarget> smt; ///< запрос SMT-стадии

    /// Гейт доказательств (`--verify=all`): все обязательства обязаны быть доказаны.
    bool verifyAll = false;
    /// Куда записать сертификат `*.trustcert` (nullopt — сертификат не пишется).
    std::optional<std::filesystem::path> certificatePath;
    /// Хеш входа для сертификата (md5 главного файла; пусто — хеш неизвестен).
    std::string inputHash;
};

/// Результаты стадий, которые описывает сам контракт. Оркестратор НАСЛЕДУЕТ этот тип и добавляет
/// свои артефакты (например SMT-скрипт): стадия приводит к типу владельца и падает с FAULT при
/// несовпадении — «тихая» потеря результата запрещена.
struct StageOutputs {
    std::optional<CppArtifact> cpp;       ///< основная кодогенерация (C++/LLVM)
    std::optional<HeaderArtifact> header; ///< интерфейсный заголовок модуля

    StageOutputs() = default;
    virtual ~StageOutputs() = default;
    StageOutputs(const StageOutputs&) = delete;
    StageOutputs& operator=(const StageOutputs&) = delete;
    StageOutputs(StageOutputs&&) noexcept = default;
    StageOutputs& operator=(StageOutputs&&) noexcept = default;
};

/// Запрос C++-кодогенерации юнита (используется C++-стадией и генерацией артефактов модулей).
struct CodegenRequest {
    const HirModule& hir;
    const CppTarget& target;
    std::optional<std::string> entryFunctionName;
    std::string programRecord;
    const HeaderArtifact* moduleHeader = nullptr;
};

} // namespace backend
} // namespace trust
