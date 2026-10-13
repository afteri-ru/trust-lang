#pragma once

// include/symbols/decl/decl_registry.hpp
// Зона регистрации объявлений: forward/definition-классификация, завершение forward,
// наборы перегрузок, идемпотентность, фиксация неоднозначности и публикация объявления в
// экспорт-поверхность модуля. Единственное место, где принимается решение
// «Inserted/Completed/Overloaded/AlreadyRegistered/Duplicate/SignatureMismatch».
// Хранилище — Scope::symbols (persistent-скоупы); SymbolArena — только синтетические слитые наборы.

#include "symbols/import/import_sites.hpp"
#include "symbols/decl/module_context.hpp"
#include "symbols/decl/module_surface.hpp"
#include "symbols/table/scope_stack.hpp"
#include "symbols/table/symbol.hpp"
#include "symbols/table/symbol_arena.hpp"
#include "symbols/table/symbol_key_factory.hpp"
#include "location/module_index.hpp"
#include "types/names.hpp"

#include <array>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace trust {

class AstNodeBase;
class TypeSystem;

/// Классификация объявления как forward: переменная без инициализатора, функция без тела.
/// TypeDecl и прочие — всегда определение.
[[nodiscard]] bool isForwardDeclaration(const AstNodeBase& decl);
[[nodiscard]] bool isForwardDeclaration(const Symbol& sym);

/// Объявление принадлежит ДРУГОМУ модулю (site-binding импорта), а не текущему.
[[nodiscard]] bool isForeignDeclaration(const Symbol& sym, ModuleIndex currentModule);

/// Видимость объявления в ЕГО модуле — вход правила `types/site_visibility.hpp`.
/// `nullptr`-объявление (ручной/тестовый символ) считается публичным.
[[nodiscard]] SiteDeclVisibility declVisibilityOf(const Symbol& sym) noexcept;

/// Регистрация одного ключа с поддержкой перегрузок/завершения forward-объявления.
[[nodiscard]] DeclResult declareOrCompleteInScope(Scope& scope, const Symbol& sym, const StorageKey& key, const TypeSystem* types);

/// Зафиксировать неоднозначность плоской привязки: кандидат-модуль существующего объявления,
/// модуль входящего и текущий модуль; узлы-кандидаты для слияния callable-наборов.
void recordAmbiguousBinding(Scope& scope, const StorageKey& flatKey, const Symbol& existing, const Symbol& incoming, ModuleIndex currentModule);

/// Символ объявления `decl` — самый полный набор перегрузок среди активных скоупов
/// (global → корень модуля → внутренние). Указатель стабилен: `ScopeStack` persistent.
[[nodiscard]] Symbol* findDeclaringSymbol(ScopeStack& scopes, ModuleContext& module, const AstNodeBase* decl);

/// Слить callable-кандидаты в один набор перегрузок; nullptr — не все кандидаты callable или
/// (при rejectDuplicateSignatures) есть одинаковые сигнатуры.
[[nodiscard]] Symbol* mergeCallableCandidates(const std::vector<const Symbol*>& candidates, bool rejectDuplicateSignatures, SymbolArena& arena);

/// Куда публиковать зарегистрированное объявление в экспорт-поверхности модуля.
enum class SurfacePolicy {
    None,            ///< локальный/блочный скоуп, привязка сайта импорта: поверхность не пишется
    DeclarationKey,  ///< первый ключ регистрации; только для реально вставленного объявления
    PositionDerived, ///< ключ по позиции в теле модуля (интерфейсный блок/область имён); только при вставке
    RootDeclaration, ///< первый ключ, в Т.Ч. для уже зарегистрированного (перенос в корень модуля)
};

/// Политика одной операции регистрации: одна точка решений, вызывающие задают только политику.
struct StorePolicy {
    SurfacePolicy surface = SurfacePolicy::None;
    /// Алиас функции: поверхность публикует объявление даже когда узел принадлежит другому модулю.
    bool aliasOfThisFile = false;
    /// Завершение forward-объявления и набор перегрузок (иначе — только вставка либо дубль).
    bool completeForward = true;
    /// Регистрация на модульном уровне: конфликт ключей видимости своего модуля — Duplicate.
    bool moduleRoot = false;
    /// Повторная привязка того же экспорта (модуль совпал, узел другой) — не дубль (сайт импорта).
    bool skipSameModuleReexport = false;
};

/// Зона регистрации объявлений таблицы символов: единственная точка записи `Scope::symbols` и
/// единственный писатель экспорт-поверхности по факту регистрации.
class DeclarationRegistry {
  public:
    DeclarationRegistry(ScopeStack& scopes, ModuleSurface& surface, SymbolArena& arena, ModuleContext& module, ImportSiteRegistry& imports,
                        const StorageKeyFactory& keys, const TypeSystem* types = nullptr);
    void setTypes(const TypeSystem* types) noexcept { m_types = types; }
    /// Перепривязать зоны после перемещения таблицы (сама зона адресов не хранит).
    void rebind(ScopeStack& scopes, ModuleSurface& surface, SymbolArena& arena, ModuleContext& module, ImportSiteRegistry& imports,
                const StorageKeyFactory& keys) noexcept;

    /// Связать УЖЕ проанализированный модуль с текущим сайтом импорта: привязать объявления его
    /// поверхности (плоский экспорт либо site-hidden ключи) и реэкспорт имён владельцев.
    /// Конфликт плоского ключа здесь не диагностируется — он станет `ambiguous name` на
    /// сайте ИСПОЛЬЗОВАНИЯ имени.
    void bindImportedModule(const ImportSite& site);

    /// Ключи привязки объявления модуля `declarationModule` с ТЕКУЩЕГО сайта (объявление может
    /// принадлежать другому модулю — привязка сайта не переключает текущий модуль).
    [[nodiscard]] std::vector<StorageKey> declarationStorageKeysIn(ModuleIndex declarationModule, std::string_view qualifiedName,
                                                                   SiteDeclVisibility declVisibility) const;

    /// Зарегистрировать `sym` под всеми ключами видимости; поверхность пишется по политике.
    [[nodiscard]] DeclResult store(Scope& scope, const Symbol& sym, std::span<const StorageKey> keys, const StorePolicy& policy);

    /// Завершить объявленный тип определением (forward → определение, тот же TypeId): обновляет
    /// записи текущего скоупа с тем же TypeId и публикует определение в экспорт-поверхность.
    /// `store` для TypeDecl не подходит: `isForwardDeclaration` считает любой TypeDecl определением.
    void completeType(const Symbol& sym, const StorageKey& key);

    /// Регистрация в ГЛОБАЛЬНОМ скоупе (уровень 0) с поддержкой перегрузок/завершения forward
    /// (статически регистрируемые члены классов). Поверхность модуля не пишется.
    [[nodiscard]] DeclResult storeGlobal(const Symbol& sym);

    /// То же без перегрузок: существующее имя не перезаписывается (персистентный
    /// квалифицированный алиас нативного шаблона).
    void storeGlobalIfAbsent(const Symbol& sym);

    /// Опубликовать объявление в экспорт-поверхности текущего модуля. Авторитетный источник —
    /// живой/владеющий символ; путь объявления у перегрузок — из ключа регистрации.
    void publishSurface(const Symbol& sym, const StorageKey& key);

    /// Ключ экспорт-поверхности для объявления модульного уровня вне корневого скоупа
    /// (интерфейсный блок/область имён импортируемого модуля). `nullopt` — не модульный уровень.
    [[nodiscard]] std::optional<StorageKey> surfaceKeyForPosition(const Symbol& sym) const;

    /// Символ объявления `decl` — самый полный набор перегрузок среди активных скоупов
    /// (global → корень модуля → внутренние). Указатель стабилен: `ScopeStack` persistent.
    [[nodiscard]] Symbol* findDeclaringSymbol(const AstNodeBase* decl) const noexcept;

  private:
    /// Привязать одно объявление к текущему сайту импорта (плоский экспорт либо site-hidden ключи,
    /// маски сайта учитываются `types/site_visibility.hpp`). Единая точка для собственных
    /// объявлений модуля и для РЕЭКСПОРТИРУЕМЫХ имён фасада.
    void bindSiteDeclaration(std::string_view qualifiedName, const Symbol& sym, ModuleIndex declarationModule);
    /// Вход правила видимости (`types/site_visibility.hpp`) для текущего сайта импорта.
    [[nodiscard]] SiteVisibilityInput siteInput(ModuleIndex declarationModule, std::string_view qualifiedName, SiteDeclVisibility declVisibility) const;
    /// Объявление в file-private области на уровне модуля (не внутри функции).
    [[nodiscard]] bool atFilePrivateLevel() const noexcept;

    /// true, если регистрация `sym` конфликтует с объявлением ПОД ДРУГИМ ключом своего модуля
    /// (private vs экспорт-идентичность vs site-hidden). Ключи из `keys` не проверяются: на них
    /// решение принимает регистрация per-key (перегрузка/завершение forward).
    [[nodiscard]] bool hasConflictingDeclaration(const Scope& scope, const Symbol& sym, std::span<const StorageKey> keys) const;

    ScopeStack* m_scopes;
    ModuleSurface* m_surface;
    SymbolArena* m_arena;
    ModuleContext* m_module;
    ImportSiteRegistry* m_imports;
    const StorageKeyFactory* m_keys;
    const TypeSystem* m_types = nullptr;
};

} // namespace trust
