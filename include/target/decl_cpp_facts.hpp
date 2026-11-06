// Целевые C++-факты ОБЪЯВЛЕНИЙ: C/C++-имя нативной привязки (импорт `x := %cpp ...;`, экспорт
// `@[export("...")]`) и C++-суффикс сигнатуры (`FunctionIdentity::cppSuffix`). ЕДИНСТВЕННЫЙ владелец
// факта (R1); принадлежит контракту цели (`TargetContract`, `target/`), а НЕ языковой модели.
//
// Фазы (одно хранилище, один владелец — R1):
//   * на фазе конвертации Term→AST (до появления `DeclId`) имя живёт во ВРЕМЕННОМ носителе узла
//     (`Decl::m_pendingNativeCppName`, деталь конвертера);
//   * `assignDeclarationIds` переносит его сюда под ключ `DeclId` (R23) и очищает узел: после
//     назначения id языковая модель целевое C++-имя НЕ хранит.
//
// Целевые факты ТИПА (C++-имя/include/libs/шаблон) — `CppTypeFacts` (см. `type_cpp_facts.hpp`).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

#include "location/decl_id.hpp"

namespace trust {

class DeclCppFacts {
  public:
    /// C/C++-имя нативной привязки объявления — целевой факт (единственное хранилище, R1).
    void setCppName(DeclId id, std::string cppName);
    /// C++-суффикс сигнатуры (`FunctionIdentity::cppSuffix` — часть сгенерированного C++-имени) —
    /// целевой факт объявления (единственное хранилище, R1). Заполняет семантика после идентификации.
    void setCppSuffix(DeclId id, std::string cppSuffix);
    /// C/C++-имя нативной привязки; пусто — привязки нет.
    [[nodiscard]] std::string_view cppNameOf(DeclId id) const noexcept;
    /// C++-суффикс сигнатуры; пусто — нет (entry/оператор/нативная привязка).
    [[nodiscard]] std::string_view cppSuffixOf(DeclId id) const noexcept;

  private:
    struct Facts {
        std::string cppName;   ///< C/C++-имя нативной привязки (пусто — привязки нет)
        std::string cppSuffix; ///< C++-суффикс сигнатуры (пусто — не назначался)
    };

    struct DeclIdHash {
        std::size_t operator()(DeclId id) const noexcept {
            const std::uint64_t h = (static_cast<std::uint64_t>(id.ownerHash()) << 32) | id.ordinal();
            return static_cast<std::size_t>(h * 1099511628211ull);
        }
    };

    std::unordered_map<DeclId, Facts, DeclIdHash> m_byDecl;
};

} // namespace trust
