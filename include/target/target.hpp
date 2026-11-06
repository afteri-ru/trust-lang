// Контракт C++-бэкенда: целевые C++-факты, вынесенные ВНЕ языковой модели (R1/R4/R37).
// Владелец — per-run `TargetContract` (создаёт `Pipeline`); потребители читают через порт
// `Context::target()`. Компонент НЕ зависит от `types`/`ast`. Общая платформенная часть — `TargetInfo`;
// будущий LLVM-бэкенд вводит СВОЙ контракт с тем же портом и не наследует C++-факты (E20/R37).
#pragma once

#include "target/decl_cpp_facts.hpp"
#include "target/method_delegates.hpp"
#include "target/target_info.hpp"
#include "target/type_cpp_facts.hpp"

#include <utility>

namespace trust {

/// Целевые факты прогона: C++-имя/суффикс объявления (`DeclCppFacts`), C++-факты типа
/// (`TypeCppFacts`), делегаты нативных методов (`MethodDelegates`) и целевой контракт платформы
/// (`TargetInfo`). Владелец — per-run, создаёт `Pipeline`.
class TargetContract {
  public:
    TargetContract() = default;
    explicit TargetContract(TargetInfo info) noexcept
    : m_targetInfo(std::move(info)) {}

    /// Единственный аксессор целевой платформы (R37/E20): стадии читают его, а не опции драйвера.
    [[nodiscard]] const TargetInfo& targetInfo() const noexcept { return m_targetInfo; }

    [[nodiscard]] const DeclCppFacts& declCppFacts() const noexcept { return m_declCpp; }
    [[nodiscard]] DeclCppFacts& declCppFacts() noexcept { return m_declCpp; }

    [[nodiscard]] const TypeCppFacts& typeCppFacts() const noexcept { return m_typeCpp; }
    [[nodiscard]] TypeCppFacts& typeCppFacts() noexcept { return m_typeCpp; }

    [[nodiscard]] const MethodDelegates& methodDelegates() const noexcept { return m_methodDelegates; }
    [[nodiscard]] MethodDelegates& methodDelegates() noexcept { return m_methodDelegates; }

  private:
    TargetInfo m_targetInfo;
    DeclCppFacts m_declCpp;
    TypeCppFacts m_typeCpp;
    MethodDelegates m_methodDelegates;
};

} // namespace trust
