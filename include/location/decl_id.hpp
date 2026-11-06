// Идентичность объявления (`DeclId`): детерминированный id, назначаемый владельцем представления
// в порядке объявлений модуля-владельца (R3/R23). Не зависит от порядка анализа/адресов и переживает
// клонирование; синтетические узлы (созданные lowering/анализатором после назначения) id не несут.
#pragma once

#include <cstdint>
#include <string_view>

namespace trust {

/// Идентификатор объявления: `ownerHash` (стабильный хеш логического имени модуля-владельца) + порядковый
/// номер объявления в модуле. Значение 0 (ordinal == 0) — «id отсутствует» (синтетика, неидентифицируемый узел).
class DeclId {
  public:
    DeclId() = default;

    [[nodiscard]] static DeclId fromParts(std::uint32_t ownerHash, std::uint32_t ordinal) noexcept {
        return DeclId{(static_cast<std::uint64_t>(ownerHash) << 32) | ordinal};
    }

    [[nodiscard]] bool isValid() const noexcept { return ordinal() != 0; }
    [[nodiscard]] std::uint32_t ownerHash() const noexcept { return static_cast<std::uint32_t>(m_value >> 32); }
    [[nodiscard]] std::uint32_t ordinal() const noexcept { return static_cast<std::uint32_t>(m_value & 0xFFFFFFFFu); }

    friend bool operator==(DeclId a, DeclId b) noexcept { return a.m_value == b.m_value; }
    friend bool operator!=(DeclId a, DeclId b) noexcept { return a.m_value != b.m_value; }
    /// Полный детерминированный порядок по значению id: упорядоченные контейнеры с ключом `DeclId`
    /// (например, таблицы фактов кодогена) итерируются одинаково при одинаковом входе (R42).
    friend bool operator<(DeclId a, DeclId b) noexcept { return a.m_value < b.m_value; }

  private:
    explicit DeclId(std::uint64_t value) noexcept
    : m_value(value) {}

    std::uint64_t m_value = 0;
};

/// Стабильный 32-битный хеш логического имени модуля-владельца (FNV-1a). Одинаковое имя — одинаковый хеш
/// на любом прогоне; смена имени модуля меняет id его объявлений (владелец id — объявление, R23).
[[nodiscard]] inline std::uint32_t declarationOwnerHash(std::string_view moduleLogicalName) noexcept {
    std::uint32_t h = 2166136261u;
    for (const char c : moduleLogicalName) {
        h ^= static_cast<unsigned char>(c);
        h *= 16777619u;
    }
    return h;
}

} // namespace trust
