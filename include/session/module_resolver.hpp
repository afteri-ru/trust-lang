// include/session/module_resolver.hpp
// Узкий интерфейс загрузки модулей для парсера (низкий слой, БЕЗ зависимостей от
// ModuleLoader/Parser). Аналог Clang `ExternalASTSource`: интерфейс объявлен в нижнем слое,
// реализация (ModuleLoader) - выше. Благодаря этому syntax/ast не зависят от module_loader.

#pragma once

#include "location/module_name.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace trust {

class Context;

/// ЕДИНАЯ диагностика «модуль не загружен» для ссылок (`\path::name`) и сайтов импорта:
/// текст/severity задаются в одной точке (парсер, конвертер Term→AST, загрузчик).
void reportModuleNotLoaded(Context& ctx, MapperRange range, std::string_view spelling);

class ModuleResolver : public ModuleNameLookup {
  public:
    ModuleResolver() = default;
    ~ModuleResolver() override = default;

    ModuleResolver(const ModuleResolver&) = delete;
    ModuleResolver& operator=(const ModuleResolver&) = delete;

    /// Загрузить файл или группу файлов по идентификатору (идемпотентно).
    /// Обычный импорт возвращает один индекс, групповой (`\\module()`) — несколько.
    [[nodiscard]] virtual std::vector<ModuleIndex> resolveModules(std::string_view moduleId, MapperRange range) = 0;

    /// Индекс УЖЕ загруженного модуля по spelling-идентификатору (сайт импорта или
    /// module-алиас). НЕ загружает и не парсит. Область module-алиасов — активный модуль.
    [[nodiscard]] virtual std::optional<ModuleIndex> findLoaded(std::string_view moduleId) const = 0;

    /// Проверить, загружен ли модуль по идентификатору (без загрузки).
    [[nodiscard]] virtual bool isModuleLoaded(std::string_view moduleId) const = 0;

    /// Логическое имя модуля (заголовок без `;` или stem программы). Индекс обязан быть
    /// зарегистрированным (иначе EXPECT). ЕДИНСТВЕННАЯ реализация — `ModuleLoader`.
    [[nodiscard]] std::string_view moduleLogicalName(ModuleIndex index) const override = 0;

    /// Зарегистрировать алиас МОДУЛЯ (`\m :- \pkg\deep\mod;`). `target` обязан быть загружен.
    /// `owner` — модуль объявления: алиас виден только ему и не экспортируется.
    /// false — spelling уже занят другим алиасом того же владельца либо импортом ЭТОГО владельца.
    virtual bool registerModuleAlias(std::string_view alias, ModuleIndex target, ModuleIndex owner) = 0;

    /// Проверить привязку алиаса модуля БЕЗ регистрации: `owner` объявил `alias → target`.
    /// Используется семантикой для валидации дублей (единственный писатель — парсер).
    [[nodiscard]] virtual bool hasModuleAlias(ModuleIndex owner, std::string_view alias, ModuleIndex target) const = 0;

    /// Видит ли @p importer protected-имена (`_::`) модуля @p owner: тот же файл, та же группа
    /// проекта или тот же пакет.
    [[nodiscard]] virtual bool canAccessProtected(ModuleIndex importer, ModuleIndex owner) const = 0;

    /// Модуль, в чьём теле сейчас идёт разбор (верх стека загрузки). Нужен для области
    /// module-алиасов; вне загрузки — FAULT.
    [[nodiscard]] virtual ModuleIndex currentModuleIndex() const = 0;
};

} // namespace trust
