// module_registry.hpp - реестр загруженных модулей
// ModuleRegistry - реестр на динамическом массиве.
// Идентификация модулей - только по индексу (ModuleIndex, strong type).
// moduleId (std::string_view) используется ТОЛЬКО при загрузке для
// детекции циклических зависимостей.
//
// Детекция циклической зависимости: если запись уже есть в реестре, но ещё не
// переведена в Ready (модуль в процессе загрузки) - FAULT.
// AST модуля конвертируется РОВНО ОДИН раз (загрузчиком, сразу после парсинга) и хранится
// в m_bodyAst как РАЗДЕЛЯЕМЫЙ вектор (`shared_ptr`): сайты импорта и модульные единицы
// трансляции держат тот же вектор без копирования списка. Term-тело после конверсии НЕ удерживается.

#pragma once

#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "location/module_path.hpp"
#include "module_loader/module_export.hpp"
#include "syntax/term/term_types.h"
#include "utils/error.hpp"
#include "utils/string_view_hash.hpp"
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust {

// ----------------------------------------------------------------------------
// ModuleRegistry - реестр загруженных модулей
// ----------------------------------------------------------------------------

class ModuleRegistry {
  public:
    ModuleRegistry() = default;

    ModuleRegistry(const ModuleRegistry&) = delete;
    ModuleRegistry& operator=(const ModuleRegistry&) = delete;
    ModuleRegistry(ModuleRegistry&&) noexcept = default;
    ModuleRegistry& operator=(ModuleRegistry&&) noexcept = default;

    /// Получить существующий загруженный модуль или создать новый для загрузки.
    /// @param moduleId Канонический идентификатор модуля (filePath).
    /// @return Индекс записи о модуле в массиве.
    ///         Если модуль уже загружен - возвращает существующий индекс.
    ///         Если модуль не найден - создаёт новую запись и вставляет в реестр.
    ///         FAULT при циклической зависимости (запись создана, AST ещё не сконвертирован).
    [[nodiscard]] ModuleIndex getOrLoad(std::string_view moduleId);

    /// Проверить, загружен ли модуль по индексу (AST сконвертирован).
    /// Единственный признак загрузки — наличие `m_bodyAst` (отдельного состояния нет).
    /// FAULT если index >= count().
    [[nodiscard]] bool isLoaded(ModuleIndex index) const;

    /// Получить канонический идентификатор модуля (путь исходника) по индексу.
    /// View в стабильное хранилище реестра (адрес не меняется при росте).
    /// FAULT если index >= count().
    [[nodiscard]] std::string_view modulePath(ModuleIndex index) const;

    /// Индекс модуля по каноническому идентификатору (путь), без загрузки.
    /// ЕДИНСТВЕННАЯ каноническая карта идентичности: алиасы импорта (spelling сайта) хранит
    /// загрузчик, а не реестр.
    [[nodiscard]] std::optional<ModuleIndex> findIndex(std::string_view moduleId) const;

    /// Полные факты интерфейса модуля, посчитанные ОДИН раз; сайты импорта получают данные
    /// через `ModuleLoader::siteProjection` (кэш `SiteProjection`), заголовок использует определения.
    /// FAULT если index >= count().
    void setInterfaceFacts(ModuleIndex index, ModuleInterfaceFacts facts);
    [[nodiscard]] const ModuleInterfaceFacts& interfaceFacts(ModuleIndex index) const;
    [[nodiscard]] bool hasInterfaceFacts(ModuleIndex index) const;

    /// Установить сконвертированное тело модуля (AST), хранимое ОДИН раз и РАЗДЕЛЯЕМОЕ
    /// (`shared_ptr`): сайты импорта и модульные TU держат тот же вектор узлов, копий нет.
    /// Используется семантикой (анализ модуля один раз), сбором экспорт-набора и кодогенерацией.
    /// FAULT если index >= count().
    void setBodyAst(ModuleIndex index, std::vector<AstNodePtr> bodyAst);

    /// Получить сконвертированное тело модуля (AST). FAULT если index >= count().
    [[nodiscard]] const std::vector<AstNodePtr>& bodyAst(ModuleIndex index) const;

    /// Разделяемое тело модуля (владелец — реестр, lifetime не зависит от Pipeline).
    /// Мутировать элементы разрешено ТОЛЬКО семантике: маркеры раскрытия заменяются на месте
    /// (тело анализируется один раз на модуль).
    [[nodiscard]] std::shared_ptr<std::vector<AstNodePtr>> bodyAstShared(ModuleIndex index) const;

    /// Зарегистрировать идентичность, НЕ выводимую из логического имени (программа без заголовка:
    /// путь от scope root). Для файлов с заголовком идентичность выводится из имени.
    void setIdentity(ModuleIndex index, ModulePath identity);
    /// Логическая идентичность модуля: сохранённая либо выведенная из логического имени
    /// (заголовок — тот же логический путь). `nullopt` — идентичности нет.
    [[nodiscard]] std::optional<ModulePath> identity(ModuleIndex index) const;

    /// Установить scope root модуля (для prefix-type путей). FAULT если index >= count().
    void setScope(ModuleIndex index, std::filesystem::path scopeRoot);
    /// Scope root модуля (project root или package root). FAULT если index >= count().
    [[nodiscard]] const std::filesystem::path& scopeRoot(ModuleIndex index) const;

    /// Установить/получить логическое имя модуля (заголовок без завершающей `;`; для
    /// программы без заголовка — stem файла). FAULT если index >= count().
    void setLogicalName(ModuleIndex index, std::string name);
    [[nodiscard]] const std::string& logicalName(ModuleIndex index) const;

    /// Количество зарегистрированных модулей.
    [[nodiscard]] std::size_t count() const noexcept { return m_modules.size(); }

  private:
    struct ModuleRecord {
        std::string m_logicalName;         ///< Логическое имя (заголовок или stem).
        std::filesystem::path m_scopeRoot; ///< Корень проекта/пакета для prefix-type путей.

        /// Сконвертированное тело модуля (AST), хранимое один раз. `nullptr` — ещё не
        /// сконвертировано (единственный признак загруженности: цикл и кэш работают на нём).
        /// Пустое тело — валидный случай (`make_shared` пустого вектора).
        std::shared_ptr<std::vector<AstNodePtr>> m_bodyAst;
        /// Идентичность, не выводимая из логического имени (программа без заголовка). Для файлов с
        /// заголовком идентичность парсится из имени — второй копии нет.
        std::optional<ModulePath> m_identity;
        /// Факты интерфейса: экспорт-набор + определения. Считаются один раз.
        std::optional<ModuleInterfaceFacts> m_interfaceFacts;
    };

    std::vector<ModuleRecord> m_modules;
    /// ЕДИНСТВЕННОЕ хранилище канонического id модуля (пути): `deque` даёт стабильные адреса
    /// элементов, поэтому ключи карты — view без второй копии строки.
    std::deque<std::string> m_moduleIds;
    /// Канонический id → индекс (O(1) поиск). Ключи — view в `m_moduleIds`.
    std::unordered_map<std::string_view, ModuleIndex, StringViewHash, std::equal_to<>> m_indexById;
};

} // namespace trust
