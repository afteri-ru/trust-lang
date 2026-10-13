#include "symbols/decl/decl_registry.hpp"

#include "symbols/scope_path.hpp"
#include "symbols/decl_binders.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "utils/error.hpp"
#include "utils/string_interner.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace trust {

namespace {

/// Нативное объявление: внешний C++-символ (импорт/forward с `%`/native template import).
bool isNativeFuncDecl(const AstNodeBase* decl) {
    if (decl == nullptr || !decl->is<FuncDecl>()) {
        return false;
    }
    const auto& f = *decl->as<FuncDecl>();
    return f.m_isNativeImport || f.m_isNativeTemplateImport || f.m_isNativeTemplateCtor || (!f.text().empty() && f.text().front() == '%');
}

/// В наборе есть перегрузка, объявленная нативным C++-символом.
bool anyNativeOverload(const std::vector<SymbolOverload>& overloads) {
    for (const SymbolOverload& overload : overloads) {
        if (isNativeFuncDecl(overload.decl)) {
            return true;
        }
    }
    return false;
}

/// Символ объявляет @p decl: как представитель либо как одна из перегрузок набора.
bool symbolDeclares(const Symbol& symbol, const AstNodeBase* decl) noexcept {
    if (symbol.decl == decl) {
        return true;
    }
    for (const SymbolOverload& overload : symbol.overloads) {
        if (overload.decl == decl) {
            return true;
        }
    }
    return false;
}

Symbol* bestDeclaringSymbolInScope(Scope& scope, const AstNodeBase* decl, Symbol* best) {
    for (auto& [key, candidate] : scope.symbols) {
        (void)key;
        if (symbolDeclares(candidate, decl) && (best == nullptr || candidate.overloads.size() > best->overloads.size())) {
            best = &candidate;
        }
    }
    return best;
}

} // namespace

bool isForwardDeclaration(const AstNodeBase& decl) {
    if (decl.is<VarDecl>()) {
        return decl.as<VarDecl>()->m_initializer == nullptr;
    }
    if (decl.is<FuncDecl>()) {
        return !decl.as<FuncDecl>()->m_body.has_value();
    }
    return false; // TypeDecl и прочие - всегда определение
}

bool isForwardDeclaration(const Symbol& sym) {
    EXPECT(sym.decl && "Symbol::isForwardDecl requires a declaration node");
    return isForwardDeclaration(*sym.decl);
}

bool isForeignDeclaration(const Symbol& sym, ModuleIndex currentModule) {
    // Модуль объявления материализован в узле (единственный писатель — загрузчик).
    const ModuleIndex module = sym.decl != nullptr ? sym.decl->module : ModuleIndex::invalid();
    return module.isValid() && module != currentModule;
}

DeclResult declareOrCompleteInScope(Scope& scope, const Symbol& sym, const StorageKey& key, const TypeSystem* types) {
    auto it = scope.symbols.find(key);
    if (it == scope.symbols.end()) {
        scope.symbols.emplace(key, sym);
        // Namespace-путь перегрузки — из КЛЮЧА регистрации, если объявление не несёт своего:
        // алиас/привязка сайта сохраняют путь СОБСТВЕННОГО объявления цели (ключ описывает
        // видимое имя, а не место определения).
        for (SymbolOverload& overload : scope.symbols.find(key)->second.overloads) {
            if (overload.namespacePath.empty()) {
                overload.namespacePath = key.namespacePath;
            }
        }
        return DeclResult::Inserted;
    }
    Symbol& existing = it->second;
    EXPECT(sym.decl && "declareOrComplete requires a declaration node");
    // То же объявление (тот же узел AST) - идемпотентная повторная регистрация. Проверяется
    // ДО forward/definition-логики: одна AST на модуль ⇒ тот же узел = тот же модуль.
    if (existing.decl == sym.decl) {
        return DeclResult::AlreadyRegistered;
    }

    // Функции: одно имя адресует НАБОР перегрузок. Идентичность — C++-идентичность
    // FunctionIdentity::sameCppOverload с binder-контекстом КОНКРЕТНОГО объявления.
    const bool bothFunc = existing.decl != nullptr && existing.decl->is<FuncDecl>() && sym.decl->is<FuncDecl>();
    if (bothFunc) {
        EXPECT(types != nullptr && "function overload identity requires a TypeSystem");
        const FunctionIdentity& identity = types->identity();

        // Единый набор: сигнатура + параметры + объявление в одном элементе.
        // Инвариант: функциональный символ ВСЕГДА несёт >= 1 запись (её создаёт
        // регистрация объявления); нарушение — внутренняя ошибка.
        EXPECT(!existing.overloads.empty() && "function symbol must carry at least one overload entry");
        EXPECT(!sym.overloads.empty() && "function declaration must carry its overload entry");
        std::vector<SymbolOverload>& entries = existing.overloads;
        SymbolOverload incoming = sym.overloads.front();
        if (incoming.namespacePath.empty()) {
            incoming.namespacePath = key.namespacePath;
        }

        std::size_t found = entries.size();
        for (std::size_t i = 0; i < entries.size(); ++i) {
            EXPECT(entries[i].decl != nullptr && "function overload entry requires a declaration node");
            // То же объявление (тот же узел): повторная регистрация идемпотентна.
            if (entries[i].decl == sym.decl) {
                return DeclResult::AlreadyRegistered;
            }
            if (identity.compare(entries[i].signature, bindersOfDecl(entries[i].decl), incoming.signature, bindersOfDecl(incoming.decl),
                                 IdentityScope::kOverload) != SignatureRelation::kDifferent) {
                found = i;
                break;
            }
        }
        // Нативный C++-символ не различает `using`-алиасы: если C++-сигнатуры совпадают,
        // объявление неразличимо для внешнего символа (номинальные Trust-типы тут не помогают).
        bool nativeCollision = false;
        if (found == entries.size() && (isNativeFuncDecl(sym.decl) || anyNativeOverload(entries))) {
            for (std::size_t i = 0; i < entries.size(); ++i) {
                if (identity.compare(entries[i].signature, bindersOfDecl(entries[i].decl), incoming.signature, bindersOfDecl(incoming.decl),
                                     IdentityScope::kInterop) != SignatureRelation::kDifferent) {
                    nativeCollision = true;
                    break;
                }
            }
        }

        if (nativeCollision) {
            return DeclResult::Duplicate;
        }

        if (found < entries.size()) {
            const bool existingForward = isForwardDeclaration(*entries[found].decl);
            const bool newDefinition = !isForwardDeclaration(*sym.decl);
            if (existingForward && newDefinition) {
                // Declaration и definition эмитятся в C++ как ОДНА функция. Один вызов
                // kDeclaration проверяет и параметры, и хвост контракта (return/trust): забыть
                // вторую половину проверки невозможно.
                if (identity.compare(entries[found].signature, bindersOfDecl(entries[found].decl), incoming.signature, bindersOfDecl(incoming.decl),
                                     IdentityScope::kDeclaration) != SignatureRelation::kEquivalent) {
                    return DeclResult::SignatureMismatch;
                }
                // Default-аргументы принадлежат API forward-объявления; определение может их не
                // повторять — переносим их в completing-символ.
                SymbolOverload completed = incoming;
                const CallableParamInfo& oldInfo = entries[found].params;
                if (oldInfo.valid() && completed.params.valid() && oldInfo.hasDefault.size() == completed.params.hasDefault.size()) {
                    for (std::size_t i = 0; i < completed.params.hasDefault.size(); ++i) {
                        if (oldInfo.hasDefault[i]) {
                            completed.params.hasDefault[i] = true;
                        }
                    }
                }
                entries[found] = std::move(completed);
                if (found == 0) {
                    existing.type = sym.type;
                    existing.decl = sym.decl;
                }
                return DeclResult::Completed;
            }
            return DeclResult::Duplicate;
        }

        entries.push_back(incoming);
        return DeclResult::Overloaded;
    }

    // Завершение forward-объявления определением того же kind в том же скоупе (не-функции).
    if (isForwardDeclaration(existing) && !isForwardDeclaration(*sym.decl) && existing.decl->kind() == sym.decl->kind()) {
        existing = sym;
        return DeclResult::Completed;
    }
    return DeclResult::Duplicate;
}

SiteDeclVisibility declVisibilityOf(const Symbol& sym) noexcept {
    if (sym.decl == nullptr) {
        return SiteDeclVisibility::Public;
    }
    switch (sym.decl->visibility) {
    case AstNodeBase::Visibility::Private:
        return SiteDeclVisibility::Private;
    case AstNodeBase::Visibility::Protected:
        return SiteDeclVisibility::Protected;
    case AstNodeBase::Visibility::Public:
        return SiteDeclVisibility::Public;
    }
    return SiteDeclVisibility::Public;
}

void recordAmbiguousBinding(Scope& scope, const StorageKey& flatKey, const Symbol& existing, const Symbol& incoming, ModuleIndex currentModule) {
    const StorageKey key = internStorageKey(flatKey);
    const std::array<ModuleIndex, 3> modules{
        existing.decl != nullptr ? existing.decl->module : ModuleIndex::invalid(),
        incoming.decl != nullptr ? incoming.decl->module : ModuleIndex::invalid(),
        currentModule,
    };
    AmbiguousBinding& conflict = scope.ambiguous[key];
    for (const ModuleIndex module : modules) {
        if (module.isValid() && std::find(conflict.modules.begin(), conflict.modules.end(), module) == conflict.modules.end()) {
            conflict.modules.push_back(module);
        }
    }
    const std::array<const AstNodeBase*, 2> decls{existing.decl, incoming.decl};
    for (const AstNodeBase* decl : decls) {
        if (decl != nullptr && std::find(conflict.decls.begin(), conflict.decls.end(), decl) == conflict.decls.end()) {
            conflict.decls.push_back(decl);
        }
    }
}

Symbol* mergeCallableCandidates(const std::vector<const Symbol*>& candidates, bool rejectDuplicateSignatures, SymbolArena& arena) {
    if (candidates.size() < 2) {
        return nullptr;
    }
    for (const Symbol* candidate : candidates) {
        if (candidate == nullptr || candidate->decl == nullptr || !candidate->decl->is<FuncDecl>() || candidate->overloads.empty()) {
            return nullptr;
        }
    }
    Symbol merged = *candidates.front();
    merged.overloads.clear();
    for (const Symbol* candidate : candidates) {
        for (const SymbolOverload& overload : candidate->overloads) {
            bool duplicate = false;
            for (const SymbolOverload& existing : merged.overloads) {
                if (existing.decl == overload.decl) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                merged.overloads.push_back(overload);
            }
        }
    }
    // Одинаковые сигнатуры не разрешаются аргументами. Для плоского имени это неоднозначность
    // имени; для `using` сохраняется диагностика резолвера перегрузок.
    if (rejectDuplicateSignatures) {
        for (std::size_t i = 0; i < merged.overloads.size(); ++i) {
            for (std::size_t j = i + 1; j < merged.overloads.size(); ++j) {
                if (merged.overloads[i].signature == merged.overloads[j].signature) {
                    return nullptr;
                }
            }
        }
    }
    return &arena.adopt(std::move(merged));
}

DeclarationRegistry::DeclarationRegistry(ScopeStack& scopes, ModuleSurface& surface, SymbolArena& arena, ModuleContext& module, ImportSiteRegistry& imports,
                                         const StorageKeyFactory& keys, const TypeSystem* types)
: m_scopes(&scopes)
, m_surface(&surface)
, m_arena(&arena)
, m_module(&module)
, m_imports(&imports)
, m_keys(&keys)
, m_types(types) {
}

void DeclarationRegistry::rebind(ScopeStack& scopes, ModuleSurface& surface, SymbolArena& arena, ModuleContext& module, ImportSiteRegistry& imports,
                                 const StorageKeyFactory& keys) noexcept {
    m_scopes = &scopes;
    m_surface = &surface;
    m_arena = &arena;
    m_module = &module;
    m_imports = &imports;
    m_keys = &keys;
}

bool DeclarationRegistry::hasConflictingDeclaration(const Scope& scope, const Symbol& sym, std::span<const StorageKey> keys) const {
    // Конфликт — только между разными ключами видимости СВОЕГО модуля; плоский ключ может
    // принадлежать другому модулю (неоднозначность, не дубль). Ключи самой регистрации решает
    // per-key регистрация (завершение forward/набор перегрузок).
    for (const StorageKey& key : m_keys->ownKeys(m_module->currentModule(), sym.name)) {
        if (std::find(keys.begin(), keys.end(), key) != keys.end()) {
            continue;
        }
        const auto it = scope.symbols.find(key);
        if (it != scope.symbols.end() && it->second.decl != sym.decl) {
            return true;
        }
    }
    return false;
}

DeclResult DeclarationRegistry::store(Scope& scope, const Symbol& sym, std::span<const StorageKey> keys, const StorePolicy& policy) {
    // Ключи регистрации всегда непусты: «ни вставлено, ни найдено» невозможно.
    EXPECT(!keys.empty() && "DeclarationRegistry::store: declaration storage keys must never be empty");
    if (policy.moduleRoot && hasConflictingDeclaration(scope, sym, keys)) {
        return DeclResult::Duplicate;
    }
    const ModuleIndex currentModule = m_module->currentModule();
    DeclResult result = DeclResult::Inserted;
    bool insertedAny = false;
    bool recognizedAny = false;
    for (const StorageKey& key : keys) {
        auto existing = scope.symbols.find(key);
        if (existing != scope.symbols.end()) {
            // То же объявление (тот же узел AST) — идемпотентная повторная регистрация
            // (ромбовидный/повторный импорт: одна AST на модуль).
            if (sym.decl != nullptr && existing->second.decl == sym.decl) {
                recognizedAny = true;
                continue;
            }
            // Тот же экспорт (модуль совпал, узел/фасад другой) — не неоднозначность и не дубль.
            if (policy.skipSameModuleReexport && sym.decl != nullptr && existing->second.decl != nullptr && existing->second.decl->module == sym.decl->module) {
                recognizedAny = true;
                continue;
            }
        }
        // Плоская привязка занята объявлением другого модуля — это неоднозначность имени,
        // а не дубль объявления (разрешается квалификацией).
        if (existing != scope.symbols.end() && isExportedVisibility(key.visibility) && isForeignDeclaration(existing->second, currentModule)) {
            recordAmbiguousBinding(scope, key, existing->second, sym, currentModule);
            recognizedAny = true;
            continue;
        }
        if (!policy.completeForward) {
            if (existing != scope.symbols.end()) {
                return DeclResult::Duplicate;
            }
            scope.symbols.emplace(key, sym);
            // Namespace-путь перегрузки — из КЛЮЧА регистрации, если объявление не несёт своего
            // (алиас/привязка сайта сохраняют путь собственного объявления цели).
            for (SymbolOverload& overload : scope.symbols.find(key)->second.overloads) {
                if (overload.namespacePath.empty()) {
                    overload.namespacePath = key.namespacePath;
                }
            }
            result = DeclResult::Inserted;
            insertedAny = true;
            continue;
        }
        const DeclResult step = declareOrCompleteInScope(scope, sym, key, m_types);
        if (step == DeclResult::Duplicate || step == DeclResult::SignatureMismatch) {
            return step;
        }
        if (step == DeclResult::AlreadyRegistered) {
            recognizedAny = true;
            continue;
        }
        result = step;
        insertedAny = true;
    }
    switch (policy.surface) {
    case SurfacePolicy::None:
        break;
    case SurfacePolicy::DeclarationKey:
        if (insertedAny) {
            publishSurface(sym, keys.front());
        }
        break;
    case SurfacePolicy::PositionDerived:
        if (insertedAny) {
            if (const auto key = surfaceKeyForPosition(sym)) {
                publishSurface(sym, *key);
            }
        }
        break;
    case SurfacePolicy::RootDeclaration:
        if (insertedAny || recognizedAny) {
            // Чужое объявление (реэкспорт) публикует поверхность владельца: в поверхности
            // текущего модуля оно не дублируется. Алиас — имя ЭТОГО файла.
            const ModuleIndex declModule = sym.decl != nullptr ? sym.decl->module : ModuleIndex::invalid();
            if (policy.aliasOfThisFile || !declModule.isValid() || declModule == currentModule) {
                publishSurface(sym, keys.front());
            }
        }
        break;
    }
    if (insertedAny) {
        return result;
    }
    EXPECT(recognizedAny && "DeclarationRegistry::store: every key was either inserted or recognized");
    return DeclResult::AlreadyRegistered;
}

void DeclarationRegistry::completeType(const Symbol& sym, const StorageKey& key) {
    EXPECT(sym.decl != nullptr && "DeclarationRegistry::completeType: symbol must carry a declaration node");
    // Определение из тела за forward'ом интерфейса — реализация владельца: клиент должен видеть
    // forward, поэтому текущий символ НЕ обновляем. В файле без блока интерфейса экспорт-набор
    // содержит определение — обновляем все записи с тем же TypeId.
    if (!sym.decl->isExportImplementation) {
        for (const StorageKey& variant : m_keys->variants(m_module->currentModule(), sym.name)) {
            auto it = m_scopes->current().symbols.find(variant);
            if (it != m_scopes->current().symbols.end() && it->second.decl != nullptr && it->second.type.getTypeId() == sym.type.getTypeId()) {
                it->second.decl = sym.decl;
                it->second.type = sym.type;
            }
        }
    }
    publishSurface(sym, key);
}

DeclResult DeclarationRegistry::storeGlobal(const Symbol& sym) {
    return declareOrCompleteInScope(m_scopes->front(), sym, m_keys->localKey(sym.name), m_types);
}

void DeclarationRegistry::storeGlobalIfAbsent(const Symbol& sym) {
    m_scopes->front().symbols.try_emplace(m_keys->localKey(sym.name), sym);
}

void DeclarationRegistry::publishSurface(const Symbol& sym, const StorageKey& key) {
    if (sym.decl == nullptr) {
        return; // ручной/тестовый символ без узла-объявления
    }
    if (sym.decl->isExportImplementation) {
        return; // определение из тела за forward'ом интерфейса — не экспортная поверхность
    }
    const StorageKey interned = internStorageKey(key);
    // Persistent-скоупы: объявление принадлежит живому узлу дерева, поэтому поверхность хранит
    // указатель на сам символ, а не на снимок в SymbolArena. Арена остаётся только для
    // синтетических символов без скоупа (слитые наборы перегрузок).
    Symbol* stored = findDeclaringSymbol(sym.decl);
    if (stored == nullptr) {
        stored = &m_arena->adopt(sym);
    }
    // Снимок должен нести путь объявления у каждой перегрузки: кодоген выбирает C++-имя
    // ВЫБРАННОЙ перегрузки, а не spelling ссылки (`using`/namespace-алиас).
    if (stored != nullptr && !key.namespacePath.empty()) {
        for (SymbolOverload& overload : stored->overloads) {
            if (overload.namespacePath.empty()) {
                overload.namespacePath = internText(key.namespacePath);
            }
        }
    }
    m_surface->recordDeclaration(m_module->currentModule(), ExportRecord{interned, stored});
}

std::optional<StorageKey> DeclarationRegistry::surfaceKeyForPosition(const Symbol& sym) const {
    // Путь от текущего скоупа вверх до узла-корня модуля (исключая его): namespace-сегменты
    // задаются creators-блоками ScopeBlock в порядке снаружи внутрь.
    const std::size_t rootNode = m_module->moduleRootNode();
    std::vector<const AstNodeBase*> creatorChain; // внутренний -> внешний
    for (std::size_t node = m_scopes->currentIndex(); node != rootNode;) {
        const Scope& scope = m_scopes->node(node);
        if (scope.parent == Scope::kNoParent) {
            break;
        }
        creatorChain.push_back(scope.creator);
        node = scope.parent;
    }
    std::string positionPath;
    for (auto it = creatorChain.rbegin(); it != creatorChain.rend(); ++it) {
        const AstNodeBase* creator = *it;
        if (creator == nullptr || creator->kind() != ParserToken::Kind::ScopeBlock) {
            return std::nullopt; // функция/тип/локальный скоуп: имя не модульного уровня
        }
        const auto& block = static_cast<const ScopeBlock&>(*creator);
        if (block.isModuleInterface()) {
            continue; // интерфейсный блок - контейнер уровня модуля без namespace-сегмента
        }
        const std::string_view text = block.text();
        if (block.is_private() || text.empty() || text == "{") {
            return std::nullopt; // приватная область/кодовый блок
        }
        if (block.is_protected() || text == "::") {
            continue; // protected/глобальная область - без namespace-сегмента
        }
        if (text.find("::") == std::string_view::npos) {
            return std::nullopt; // метка/именованный кодовый блок, не namespace
        }
        // Разбор сегментов — единая точка `symbols/scope_path.hpp` (та же, что у текущей позиции).
        for (const std::string_view segment : namespaceSegmentsOf(text)) {
            positionPath = utils::qualifyTrustName(positionPath, segment);
        }
    }
    const std::string_view declaredPath = utils::namespacePathOf(sym.name);
    if (!declaredPath.empty()) {
        positionPath = utils::qualifyTrustName(positionPath, declaredPath);
    }
    const std::string qualified = utils::qualifyTrustName(positionPath, utils::shortNameOf(sym.name));
    return m_keys->identityKey(m_module->currentModule(), qualified);
}

Symbol* DeclarationRegistry::findDeclaringSymbol(const AstNodeBase* decl) const noexcept {
    return trust::findDeclaringSymbol(*m_scopes, *m_module, decl);
}

Symbol* findDeclaringSymbol(ScopeStack& scopes, ModuleContext& module, const AstNodeBase* decl) {
    if (decl == nullptr) {
        return nullptr;
    }
    Symbol* best = bestDeclaringSymbolInScope(scopes.node(0), decl, nullptr);
    const bool hasRoot = module.hasModuleRoot();
    const std::size_t rootNode = module.moduleRootNode();
    if (hasRoot && rootNode != 0) {
        best = bestDeclaringSymbolInScope(scopes.node(rootNode), decl, best);
    }
    for (std::size_t index = scopes.depth(); index-- > 0;) {
        const std::size_t node = scopes.activePath()[index];
        if (node == 0 || (hasRoot && node == rootNode)) {
            continue;
        }
        best = bestDeclaringSymbolInScope(scopes.node(node), decl, best);
    }
    return best;
}

void DeclarationRegistry::bindImportedModule(const ImportSite& site) {
    // Сайт записан `pushImportScope` (тело анализируется впервые) либо ещё НЕТ (повторный сайт уже
    // проанализированного тела): запись идемпотентна, поэтому вызывается в обеих точках.
    m_imports->record(site);
    const ModuleIndex imported = site.imported;
    if (!m_surface->hasModule(imported)) {
        // Модуль без объявлений и без реэкспорта: связывать нечего (не ошибка). Вызывающий
        // вызывает этот метод только для УЖЕ проанализированного модуля.
        return;
    }
    // Стек сайтов активен только на время привязки (RAII: FatalError анализатора не оставит его).
    ImportSiteRegistry::Guard activeScope(*m_imports, site);
    for (const ExportRecord& declaration : m_surface->declarationsOf(imported)) {
        if (declaration.symbol == nullptr) {
            continue;
        }
        // Видимость с сайта — ОДНО правило (`types/site_visibility.hpp`): private и protected
        // без доступа группы не привязываются, маска/транзитивность решаются при построении ключей.
        const std::string qualifiedName = utils::qualifyTrustName(declaration.key.namespacePath, declaration.key.name);
        const SiteVisibility siteVisibility = siteVisibilityOf(siteInput(imported, qualifiedName, declVisibilityOf(*declaration.symbol)));
        if (siteVisibility == SiteVisibility::NotVisible) {
            continue;
        }
        // Приватность не зависит от сайта; видимость экспорта - свойство КОНКРЕТНОГО сайта
        // (маска + глубина цепочки), поэтому ключи считаются с активным сайтом.
        if (!isFilePrivateVisibility(declaration.key.visibility)) {
            bindSiteDeclaration(qualifiedName, *declaration.symbol, imported);
            continue;
        }
        // file-private: единственная привязка, без маски-фильтрации.
        m_scopes->current().symbols.try_emplace(
            internStorageKey(moduleKey(imported, declaration.key.namespacePath, declaration.key.name, /*importHidden=*/false)), *declaration.symbol);
    }
    // Реэкспорт: фасад публикует имена ВЛАДЕЛЬЦЕВ; привязываются те же ключи, что у собственных
    // экспортов (маски сайта-импортёра применяются правилом `siteKeysFor`).
    for (const ExportRecord& name : m_surface->reExportsOf(imported)) {
        // Protected-ось реэкспорта принадлежит ВЛАДЕЛЬЦУ имени, а не сайту фасада:
        // публикуются только те имена, что владелец отдал в экспорт-набор (см. MEMORY).
        if (name.symbol != nullptr && name.symbol->decl != nullptr && name.symbol->decl->module.isValid()) {
            bindSiteDeclaration(utils::qualifyTrustName(name.key.namespacePath, name.key.name), *name.symbol, name.symbol->decl->module);
        }
    }
}

void DeclarationRegistry::bindSiteDeclaration(std::string_view qualifiedName, const Symbol& sym, ModuleIndex declarationModule) {
    // Привязка не диагностируется здесь: конфликт плоского ключа фиксируется как неоднозначность
    // и сообщается на сайте ИСПОЛЬЗОВАНИЯ имени.
    for (const StorageKey& key : declarationStorageKeysIn(declarationModule, qualifiedName, declVisibilityOf(sym))) {
        const DeclResult stored =
            store(m_scopes->current(), sym, std::span<const StorageKey>(&key, 1), StorePolicy{.completeForward = false, .skipSameModuleReexport = true});
        (void)stored;
    }
}

std::vector<StorageKey> DeclarationRegistry::declarationStorageKeysIn(ModuleIndex declarationModule, std::string_view qualifiedName,
                                                                      SiteDeclVisibility declVisibility) const {
    // Ключи возвращаются ИНТЕРНИРОВАННЫМИ: они кладутся в скоуп и обязаны пережить аргументы
    // (текст может прийти из временной строки), а lookup этого не требует - там views живут
    // до конца вызова. Построение ключей — единственная точка StorageKeyFactory.
    // `__::`-область модуля делает объявление файл-приватным независимо от узла.
    const SiteDeclVisibility effective = (atFilePrivateLevel() || declVisibility == SiteDeclVisibility::Private) ? SiteDeclVisibility::Private : declVisibility;
    const bool insideAnalyzedBody = !m_imports->empty() && m_imports->top().imported == declarationModule;
    if (!insideAnalyzedBody) {
        // Объявление вне тела импортируемого модуля: только идентичность модуля (плоская привязка
        // появляется у сайта импорта, а не у владельца).
        return {m_keys->ownerKey(declarationModule, qualifiedName, effective)};
    }
    return m_keys->siteKeys(siteInput(declarationModule, qualifiedName, effective));
}

SiteVisibilityInput DeclarationRegistry::siteInput(ModuleIndex declarationModule, std::string_view qualifiedName, SiteDeclVisibility declVisibility) const {
    const ImportSite& site = m_imports->top();
    return SiteVisibilityInput{
        site.importer,
        declarationModule,
        declVisibility,
        utils::namespacePathOf(qualifiedName),
        utils::shortNameOf(qualifiedName),
        site.masks,
        m_imports->depth() == 1 ? SiteAccess::Direct : SiteAccess::Transitive,
        site.protectedAllowed,
    };
}

bool DeclarationRegistry::atFilePrivateLevel() const noexcept {
    return m_module->inFilePrivateScope(m_scopes->current().kind == ScopeKind::ModuleRoot);
}

} // namespace trust
