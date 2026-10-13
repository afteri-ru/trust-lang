// src/semantic/core/name_resolution_nodes.cpp
// Группы узлов обхода `NameResolutionPass`: скоуп-контейнеры, with, объявления функций/лямбд,
// catch-ветки, термины trust-контрактов и пост-обработка (типизация + синтетические временные).
#include "semantic/core/name_resolution.hpp"
#include "diag/flag_values.hpp"
#include "semantic/typer/ellipsis.hpp"
#include "semantic/solver.hpp"
#include "semantic/ref/stack_check.hpp"
#include "symbols/symbol_table.hpp"
#include "semantic/typer/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/import_masks.hpp"
#include "utils/strings.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

namespace {

/// RAII: восстановление логического module-context даже при исключении анализатора.
struct ModuleContextGuard {
    SymbolTable& symbols;
    ModuleIndex previous;
    explicit ModuleContextGuard(SymbolTable& table, ModuleIndex module)
    : symbols(table)
    , previous(table.currentModule()) {
        symbols.setCurrentModule(module);
    }
    ~ModuleContextGuard() { symbols.setCurrentModule(previous); }
    ModuleContextGuard(const ModuleContextGuard&) = delete;
    ModuleContextGuard& operator=(const ModuleContextGuard&) = delete;
};

/// RAII: вход/выход из private области (`__::`/`__{...}`) на уровне модуля.
struct FilePrivateGuard {
    SymbolTable& symbols;
    explicit FilePrivateGuard(SymbolTable& table)
    : symbols(table) {
        symbols.enterFilePrivate();
    }
    ~FilePrivateGuard() { symbols.exitFilePrivate(); }
    FilePrivateGuard(const FilePrivateGuard&) = delete;
    FilePrivateGuard& operator=(const FilePrivateGuard&) = delete;
};

} // namespace

// Скоуп-контейнеры (модуль/блок/ScopeBlock/TryCatchStmt): вложенный скоуп на тело.
void NameResolutionPass::analyzeScopeContainer(AstNodePtr& self) {
    enterScope(*self);
    analyzeChildren(self);
    exitScope();
}

// Модуль: корневой открывает скоуп программы; сайт импорта SCOPE-TRANSPARENT -
// объявления импортируемого модуля регистрируются в ТЕКУЩЕМ скоупе (включение исходника).
// Это делает повторное включение и совпадение имён из разных модулей видимыми для
// проверки дублей (declare/declareOrComplete/registerType).
void NameResolutionPass::doModuleDecl(AstNodePtr& self, bool) {
    auto& mn = static_cast<ModuleNode&>(*self);
    // Логический module-context: видимость private определяется принадлежностью модулю,
    // а не физическим местом AST (модуль может быть вставлен в AST другого модуля).
    if (mn.isImport()) {
        // Сайт импорта: объявления модуля регистрируются в ТЕКУЩЕМ скоупе, но их видимость
        // определяется масками сайта и глубиной цепочки импортов (file-local ключи).
        const ModuleIndex importer = m_actx.symbols().currentModule();
        ModuleResolver* resolver = m_actx.ctx().moduleResolver();
        EXPECT(resolver != nullptr && "import binding requires a module resolver");
        // Доступ к protected-набору — факт ОДНОГО сайта на часть: вычисляется один раз и
        // используется и для непопаданий масок, и для привязки.
        std::vector<bool> partProtectedAccessible;
        partProtectedAccessible.reserve(mn.parts().size());
        bool anyProtectedAccessible = false;
        // Непопадание считается по ОБЪЕДИНЁННОМУ набору частей: маска несовпавшая, только если
        // не совпала ни в одной части (пересечение непопаданий частей). Для protected — только
        // по ДОСТУПНЫМ частям.
        std::vector<std::string> unmatched;
        bool hasUnmatchedBaseline = false;
        std::vector<std::string> accessibleProtectedUnmatched;
        bool hasProtectedBaseline = false;
        for (const auto& part : mn.parts()) {
            EXPECT(part.projection && "module part must carry a site projection");
            const bool protectedAccessible = resolver->canAccessProtected(importer, part.index);
            partProtectedAccessible.push_back(protectedAccessible);
            anyProtectedAccessible = anyProtectedAccessible || protectedAccessible;
            if (!hasUnmatchedBaseline) {
                unmatched = part.projection->unmatchedMasks;
                hasUnmatchedBaseline = true;
            } else {
                utils::retainMasksPresentInBoth(unmatched, part.projection->unmatchedMasks);
            }
            if (!protectedAccessible) {
                continue;
            }
            if (!hasProtectedBaseline) {
                accessibleProtectedUnmatched = part.projection->protectedUnmatchedMasks;
                hasProtectedBaseline = true;
            } else {
                utils::retainMasksPresentInBoth(accessibleProtectedUnmatched, part.projection->protectedUnmatchedMasks);
            }
        }
        if (anyProtectedAccessible) {
            // Маска остаётся несовпавшей, только если не нашлась и в protected-наборе.
            utils::retainMasksPresentInBoth(unmatched, accessibleProtectedUnmatched);
        }
        for (const std::string& mask : unmatched) {
            m_actx.ctx().report(mn.range(), semantic::DiagId::UnmatchedMask, "import mask '{}' does not match any exported declaration of module '{}'", mask,
                                mn.importSpelling());
        }
        // Пустой результат импорта: модуль ничего не экспортирует. Namespace-алиас — публичная
        // привязка префикса, поэтому импорт, принёсший только её, не считается пустым.
        bool anyExport = false;
        for (const auto& part : mn.parts()) {
            EXPECT(part.projection && "module part must carry a site projection");
            if (!part.projection->exports->empty() || !part.projection->namespaceAliases->empty() ||
                (anyProtectedAccessible && part.projection->hasProtectedExports)) {
                anyExport = true;
                break;
            }
        }
        if (!anyExport && unmatched.empty()) {
            m_actx.ctx().report(mn.range(), semantic::DiagId::EmptyImport, "import of module '{}' brings no declarations into scope", mn.importSpelling());
        }
        // Сайт хранит список файловых частей. Каждая часть — обычный файловый импорт:
        // свой модуль/контекст/маски; повторная часть (ромб) только связывает видимость.
        bool reportedDuplicate = false;
        const std::string masksKey = mn.importMaskSet().normalizedKey();
        for (std::size_t partPos = 0; partPos < mn.parts().size(); ++partPos) {
            auto& part = mn.parts()[partPos];
            const bool protectedAccessible = partProtectedAccessible[partPos];
            ImportSiteKey importKey{importer, part.index, masksKey};
            if (!m_importKeys[m_actx.symbols().depth()].insert(std::move(importKey)).second) {
                // Один сайт импорта — одно сообщение, даже если группа состоит из нескольких файлов.
                if (!reportedDuplicate) {
                    m_actx.ctx().report(mn.range(), semantic::DiagId::DuplicateImport, "duplicate import of module '{}' with the same masks in this scope",
                                        mn.importSpelling());
                    reportedDuplicate = true;
                }
                continue;
            }
            EXPECT(part.body && "module part must carry a shared body");
            // Экспортируемые namespace-алиасы модуля — факт интерфейса из ЗАГРУЗЧИКА (единый
            // источник); семантика только проецирует их (квалифицированная ссылка, LSP).

            m_actx.symbols().aliases().recordExportedFacts(part.index, *part.projection->namespaceAliases);
            // Экспортированные namespace-алиасы — привязка ПРЕФИКСА в скоупе сайта импорта
            // (одна запись в AliasRegistry: и привязка, и проекция для LSP).
            for (const ModuleNamespaceAlias& alias : *part.projection->namespaceAliases) {
                const NamespaceAliasRecord record{m_actx.symbols().currentModule(), internText(alias.alias), alias.targetModule,
                                                  internText(alias.path), m_actx.symbols().currentScopeRange(), AliasOrigin::Local};
                if (!m_actx.symbols().aliases().recordAlias(m_actx.symbols().current(), record)) {
                    m_actx.ctx().diag().report(Severity::Error, mn.range(), "namespace alias '{}::' is already bound in this scope", alias.alias);
                    continue;
                }
            }
            // Тело анализируется ОДИН раз; поверхность (собственный экспорт + реэкспорт)
            // связывается ОДНИМ путём для первого и повторного сайта. Конфликт плоского имени с
            // другим модулем здесь не диагностируется — он станет `ambiguous name` на месте
            // использования.
            if (!m_actx.state().isModuleBodyAnalyzed(part.index)) {
                m_actx.state().markModuleBodyAnalyzed(part.index);
                m_actx.symbols().pushImportScope(part.index, mn.importMaskSet(), protectedAccessible);
                {
                    ModuleContextGuard moduleContext(m_actx.symbols(), part.index);
                    for (auto& child : *part.body) {
                        if (child) {
                            analyzeNode(child);
                        }
                    }
                }
                m_actx.symbols().popImportScope();
            }
            m_actx.symbols().bindImportedModule(part.index, mn.importMaskSet(), protectedAccessible);
        }
        // Реэкспорт: сайт загрузки ВНУТРИ блока интерфейса публикует свой (отфильтрованный
        // масками) набор имён в экспорт-наборе ЭТОГО файла; владелец имён не меняется.
        if (mn.isReExport()) {
            for (const auto& part : mn.parts()) {
                EXPECT(part.projection && "module part must carry a site projection");
                std::vector<const AstNodeBase*> exported;
                exported.reserve(part.projection->exports->size());
                for (const auto& node : *part.projection->exports) {
                    if (node) {
                        exported.push_back(node.get());
                    }
                }
                m_actx.symbols().surface().recordReExport(m_actx.symbols().currentModule(), part.index, exported);
            }
        }
        return;
    }
    ModuleContextGuard moduleContext(m_actx.symbols(), mn.moduleIndex());
    analyzeScopeContainer(self);
}

// Область видимости: `__` на top-level модуля - private namespace (только этот файл);
// `_` - protected (файлы той же группы/пакета). Блок НЕ открывает лексический скоуп, объявления
// регистрируются в модульном скоупе под module-qualified ключом. Остальные блоки - обычный скоуп.
void NameResolutionPass::doScopeBlock(AstNodePtr& self, bool) {
    auto& sb = static_cast<ScopeBlock&>(*self);
    const bool moduleLevel = m_actx.symbols().depth() == 2 && m_actx.namespaces().currentFunc() == nullptr;
    if (moduleLevel && sb.is_private()) {
        FilePrivateGuard privateScope(m_actx.symbols());
        analyzeChildren(self);
        return;
    }
    if (moduleLevel && sb.is_protected()) {
        // Protected-область не создаёт namespace-сегмент: имена регистрируются на уровне
        // модуля; видимость ограничивает связывание на сайте импорта.
        analyzeChildren(self);
        return;
    }
    analyzeScopeContainer(self);
}

// Оператор with(...){...}else{...} (RAII-менеджер контекста): биндинги, тело, else.
void NameResolutionPass::analyzeWithStmt(AstNodePtr& self) {
    // `with(a=f(), b=g()){...}else{...}` (RAII-менеджер контекста):
    //   - биндинги (VarDecl) регистрируются в скоупе оператора стандартным путём analyzeVarDecl
    //     (кейс VarDecl в doVarDecl): declareOrComplete → duplicate-ошибка при совпадении в том
    //     же скоупе, -Wshadow-предупреждение при затенении внешнего имени, onDeclare-хуки,
    //     sigil-нормализация `$x`, Storage::Local, вывод типа из инициализатора;
    //   - тело (ScopeBlock) анализируется во вложенном скоупе - видит биндинги;
    //   - ветка else - в СВОЁМ скоупе ПОСЛЕ выхода из скоупа оператора: биндинги НЕ видит.
    auto& w = static_cast<WithStmt&>(*self);
    enterScope(*self);
    // m_locks - пары (lock, binding): анализируем и источник захвата (lock, RefTakeExpr),
    // и переменную-значение (binding, VarDecl) - обе должны зарезолвить свои имена.
    for (auto& [lock, binding] : w.m_locks) {
        if (lock) {
            analyzeNode(lock);
        }
        if (binding) {
            analyzeNode(binding);
        }
    }
    if (w.m_body) {
        analyzeNode(w.m_body);
    }
    exitScope();
    if (w.m_else) {
        analyzeNode(w.m_else);
    }
}

// Объявление функции/лямбды/нативного шаблона-типа.
void NameResolutionPass::analyzeFuncDeclNode(AstNodePtr& self) {
    // Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` - регистрирует
    // параметризованный тип (а НЕ функцию): никакого скоупа функции и тела нет.
    auto& f = *self->as<FuncDecl>();
    if (f.m_isNativeTemplateCtor) {
        m_decl.analyzeNativeTemplateDecl(f);
        return;
    }
    // Лямбда-выражение: имя НЕ регистрируется в объёмлющем скоупе. Захваты резолвятся в
    // ОБЪЁМЛЮЩЕМ скоупе (до входа в скоуп лямбды), затем открывается скоуп с параметрами.
    if (f.isLambda()) {
        m_decl.analyzeLambdaCaptures(f);
        enterScope(f);
        m_decl.declareFuncParams(f);
        analyzeChildren(self);
        exitScope();
        // Пост-порядковая типизация значения-лямбды (функциональный тип) - ранний return
        // в этом ветке пропускает общий typeExpr ниже, поэтому вызываем явно.
        m_typer.typeExpr(self.get());
        return;
    }
    // Имя функции регистрируется в ТЕКУЩЕМ (внешнем) скоупе, затем открывается
    // скоуп функции, в котором видны параметры и тело.
    //
    // Generic-функция (`<T> f(...)`) и шаблонный native-import: типовые параметры
    // активируются в AnalysisContext ДО анализа сигнатуры (resolveTypeRef должен видеть T),
    // но НЕ через SymbolTable, чтобы имя функции не попало в собственный шаблонный скоуп.
    const bool generic = f.m_templateParams.has_value() && !f.m_isNativeTemplateCtor;
    if (generic) {
        std::vector<std::pair<std::string, TypeId>> tparams;
        tparams.reserve(f.m_templateParams->size());
        for (const auto& p : *f.m_templateParams) {
            if (!p) {
                continue;
            }
            const std::string pname(requiredText(*p));
            tparams.emplace_back(pname, m_actx.ctx().types().interner().getOrCreateTemplateParamType(pname));
        }
        m_actx.types().pushTemplateParamScope(std::move(tparams));
    }
    m_decl.analyzeFuncDecl(f);
    // Namespace-алиас (`ns:: :- \lib::ns::;`): цель - ПРЕФИКС области модуля, а не ссылка на
    // значение. Резолвить её как имя нельзя (объявлений с таким именем нет), скоупа/тела нет.
    if (f.aliasKind() == AliasKind::Namespace || f.aliasKind() == AliasKind::Module) {
        return;
    }
    enterScope(f);
    m_decl.declareFuncParams(f);
    // Trust-условия (пред/пост): резолв имён в скоупе функции (параметры видны; имя
    // функции = возврат в пост-условии, запрещено в пред-условии - см. lookupOrError).
    m_trust.processTrustConditions(f.m_trust, f);
    analyzeChildren(self);
    m_decl.validateFuncDefaults(f);
    exitScope();
    if (generic) {
        m_actx.types().popTemplateParamScope();
    }
}

// Ветка catch(...) : отдельный вложенный скоуп + связывание переменной.
void NameResolutionPass::analyzeCatchBlockNode(AstNodePtr& self) {
    // Каждая ветка catch - отдельный вложенный скоуп (как в C++). Связанная переменная
    // `catch(e:Type)` (VarDecl в m_binding) регистрируется здесь как ЛОКАЛЬНАЯ и видна
    // только в теле ветки; `catch(:Type)`/`catch(_)`/`catch(...)` не связывают имя.
    auto& cb = *self->as<CatchBlock>();
    enterScope(*self);
    // `catch(:Type)` без связывания имени: узел типа принадлежит ветке напрямую. Резолвим его
    // здесь, чтобы факт был материализован для кодогена (иначе тип не будет разрешён).
    if (cb.m_binding && cb.m_binding->kind() == ParserToken::Kind::TypeName) {
        if (!m_actx.types().resolveTypeRef(*cb.m_binding).has_value()) {
            m_actx.ctx().diag().report(Severity::Error, cb.m_binding->range(), "unknown catch type '{}'", requiredText(*cb.m_binding));
        }
    }
    if (cb.m_binding && cb.m_binding->kind() == ParserToken::Kind::VarDecl) {
        auto& vd = static_cast<VarDecl&>(*cb.m_binding);
        Symbol sym;
        sym.name = stableSymbolName(std::string(vd.text()));
        if (vd.m_type) {
            const auto tid = m_actx.types().resolveTypeRef(*vd.m_type);
            if (tid.has_value()) {
                sym.type = QualType::fromTypeId(*tid);
            } else {
                m_actx.ctx().diag().report(Severity::Error, vd.m_type->range(), "unknown catch type '{}'", requiredText(*vd.m_type));
            }
        }
        sym.decl = &vd;
        sym.storage = Storage::Local;
        m_actx.symbols().declare(sym);
    }
    analyzeChildren(self);
    exitScope();
}

// Термин решателя trust-контракта @( term, args... @); для кванторов - связка.
void NameResolutionPass::analyzeTrustElemNode(AstNodePtr& self) {
    // Термин решателя `@( term, args... @)` внутри контракта: резолв имён аргументов.
    // Для кванторов (forall/exists) первый аргумент - переменная-связка: она обязана быть
    // переменной, ОБЪЯВЛЕННОЙ РАНЕЕ (разрешение имён). Тип связки берётся из её объявления,
    // НЕ выводится; не объявлена или тип выведен автоматически (kInferredFlag) - ошибка.
    // Сам узел-связка не анализируется (это связка, не ссылка).
    auto& te = *self->as<TrustElem>();
    if (te.kind == Z3TermKind::Forall || te.kind == Z3TermKind::Exists) {
        if (!te.m_args.empty() && te.m_args[0]) {
            const std::string bname(requiredText(*te.m_args[0]));
            const Symbol* declared = resolveSimple(nullptr, bname);
            if (!declared) {
                m_actx.ctx().diag().report(Severity::Error, te.m_args[0]->range(), "quantifier bound variable '{}' must be a variable declared earlier", bname);
                return;
            }
            if (declared->type.isInferred()) {
                m_actx.ctx().diag().report(Severity::Error, te.m_args[0]->range(),
                                           "quantifier bound variable '{}' has an inferred type; declare it with an explicit type", bname);
                return;
            }
            const TypeId bt = declared->type.getTypeId();
            te.m_boundVarType = bt; // результат разрешения имён: тип из объявления (переживает таблицу)
            enterScope(*self);
            Symbol sym;
            sym.name = stableSymbolName(bname); // связка как в исходнике (без сигила)
            sym.type = QualType::fromTypeId(bt);
            sym.decl = te.m_args[0].get();
            sym.storage = Storage::Local;
            m_actx.symbols().declareOrComplete(sym);
            // Тело (P) анализируем со связкой в скоупе (индексы с 1).
            for (std::size_t i = 1; i < te.m_args.size(); ++i) {
                if (te.m_args[i]) {
                    analyzeNode(te.m_args[i]);
                }
            }
            exitScope();
            return;
        }
    }
    for (std::size_t i = 0; i < te.m_args.size(); ++i) {
        if (te.m_args[i]) {
            analyzeNode(te.m_args[i]);
        }
    }
}

// Пост-порядковая типизация узла и синтетические временные ($^ / match / return).
void NameResolutionPass::analyzeNodeTail(AstNodePtr& self) {
    // Пост-порядковая типизация выражения/объявления (после того как дети уже
    // проанализированы и типизированы): вычисляет тип результата выражения и
    // расширяет выводимый (inferred) тип целевой переменной по истории присвоений.
    m_typer.typeExpr(self.get());

    // Оператор сравнения типов, статическая проверка которого не удалась (семантика уже
    // сообщила причину): заменяем узел на универсальную ErrorExpr-заглушку, чтобы кодоген и
    // последующие проходы НЕ дублировали диагностику и ничего не эмитили.
    if (self && self->kind() == ParserToken::Kind::CompareOp) {
        if (Binary* b = self->as<Binary>(); b != nullptr && isTypeCheckOp(b->m_op) && !b->m_typeCheckConst.has_value()) {
            self = std::make_shared<ErrorExpr>(std::move(self));
        }
    }

    // `$^` (простой случай): синтетическая временная `__trust_last_N` из источника, который НЕ даёт
    // значения (void-функция/unit) ⇒ у `$^` нечего захватывать. Пре-семантический проход не знает тип
    // вызова, поэтому здесь (тип известен) выдаём ТОЧЕЧНУЮ диагностику вместо общей "unable to generate
    // C++ type 'Any'". Тип источника - Void/None либо не выводится (INVALID).
    if (self->kind() == ParserToken::Kind::VarDecl) {
        VarDecl& vd = static_cast<VarDecl&>(*self);
        if (vd.m_lastResultTemp && vd.m_initializer) {
            const TypeId src = m_actx.facts().exprType(*vd.m_initializer);
            const TypeId voidId = m_actx.ctx().readTypes().getType(type::Void);
            if (src == INVALID_TYPE_ID || (voidId != INVALID_TYPE_ID && src == voidId)) {
                m_actx.ctx().diag().report(Severity::Error, self->range(),
                                           "pseudo-variable '$^' (result of the last operation): the "
                                           "preceding statement produces no value to capture (void)");
            }
        }
    }

    // MatchStmt: scrutinee вычисляется один раз во временную const-переменную. Временную создаёт
    // СЕМАНТИКА (инвариант «временные — уровень анализатора»): синтезируется const VarDecl
    // `_matchN := <m_value>;` (тип из exprType → VarDecl::inferredType), m_value заменяется
    // ссылкой на неё (Ident _matchN). Транспилятор эмитит её как обычный VarDecl и читает тип для
    // выбора switch/enum/if. (Создаёт семантика, а не lowering, т.к. только у неё есть тип значения.)
    if (self->kind() == ParserToken::Kind::MatchingStmt) {
        auto& match = static_cast<MatchStmt&>(*self);
        if (match.m_value) {
            const TypeId vt = m_actx.facts().exprType(*match.m_value);
            const std::string tmpName = "_match" + std::to_string(m_actx.state().nextMatchTempId());
            auto tmp = std::make_shared<VarDecl>(tmpName, nullptr, std::move(match.m_value));
            if (vt != INVALID_TYPE_ID) {
                tmp->inferredType = QualType::fromTypeId(vt).withoutInferred();
            }
            if (const auto ro = m_actx.ctx().attrs().lookup(attr::ReadOnly); ro.has_value()) {
                tmp->add_attr(*ro); // const-временная (как '^' на имени)
            }
            match.m_tempDecl = tmp;
            match.m_value = std::make_shared<IdentName>(tmpName);
        }
        // Атрибут @[matcher("fn")]: переопределение функции сравнения (по значению). Проверяем
        // имя функции-предиката и совместимость оператора match (не type-match).
        analyzeMatchMatcher(match);
    }

    // ReturnStmt: hoist возвращаемого значения в const-временную `__trust_res_N` (для пост-условий:
    // выражение вычисляется один раз, имя функции связывается со значением). Временную создаёт
    // СЕМАНТИКА (инвариант «временные — уровень анализатора»): синтезируется const VarDecl
    // `__trust_res_N := <m_value>;` (тип из exprType → inferredType), m_value заменяется ссылкой
    // на неё (Ident __trust_res_N). Транспилятор эмитит её как обычный VarDecl и читает имя.
    // Создаётся только для ИМЕНОВАННОГО return (m_label) из функции с пост-условиями — точно по
    // логике visit_ReturnStmt (void/неименованный `++ _ ++` не трогаем).
    if (self->kind() == ParserToken::Kind::ReturnStmt) {
        auto& js = static_cast<JumpStmt&>(*self);
        if (js.m_label && js.m_funcDecl && js.m_value) {
            const bool hasPost = js.m_funcDecl->hasTrustProperty(PropertyKind::Post);
            if (hasPost) {
                const TypeId vt = m_actx.facts().exprType(*js.m_value);
                const std::string tmpName = "__trust_res_" + std::to_string(m_actx.state().nextResultTempId());
                auto tmp = std::make_shared<VarDecl>(tmpName, nullptr, std::move(js.m_value));
                if (vt != INVALID_TYPE_ID) {
                    tmp->inferredType = QualType::fromTypeId(vt).withoutInferred();
                }
                if (const auto ro = m_actx.ctx().attrs().lookup(attr::ReadOnly); ro.has_value()) {
                    tmp->add_attr(*ro); // const-временная (как '^' на имени)
                }
                js.m_tempDecl = tmp;
                js.m_value = std::make_shared<IdentName>(tmpName);
            }
        }
    }
}

} // namespace trust
