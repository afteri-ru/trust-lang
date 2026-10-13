#include "semantic/core/name_resolution.hpp"
#include "semantic/core/ast_view.hpp"
#include "semantic/core/local_name.hpp"
#include "semantic/core/type_predicates.hpp"
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
#include "utils/strings.hpp"
#include "utils/utils.hpp"
#include <algorithm>
#include <format>
#include <string>

namespace trust {

// -- Пер-компонентная регистрация диагностик семантики (см. diag/registry.hpp) --
// Регистрирует на static-init severity-диагностики и feature-флаги, которыми владеет
// компонент semantic (сообщаются в name_resolution.cpp / lint.cpp / pass_runner.cpp).
namespace {
struct SemanticDiagnosticsRegistrar {
    SemanticDiagnosticsRegistrar() {
        registerDiagnostics([](Options& opts) {
            // Авто-регистрация ВСЕХ severity-диагностик из SEMANTIC_DIAG_LIST (без ручного
            // списка opts.add(...)): каждая строка списка → `opts.add(semantic::DiagId::X)`.
            SEMANTIC_DIAG_LIST(SEMANTIC_DG_ADD_OPT)
            // `-Wclass-member-dot` (поле/метод класса зарегистрированы без ведущей '.') по умолчанию
            // ВЫКЛЮЧЕН (Severity::Ignore): включается явно `-Wclass-member-dot=warning|error` (или
            // -Wextra, т.к. член группы WG_Wextra). Остальные severity-диагностики - по умолчанию Warning.
            opts.set(semantic::DiagId::ClassMemberDot, Severity::Ignore);
            // Авто-регистрация ВСЕХ флагов из SEMANTIC_FLAG_LIST: `opts.add_flag(FlagKind::X)` +
            // (если задано значение по умолчанию в списке) `opts.set_flag_value(...)`.
            // `-Wsolver=...` (severity) и `--solver-mode`/`-Wsolver-loop`/`-fsolver-loop-unroll`/
            // `--keywords` (поведенческие флаги) регистрируются здесь автоматически.
            SEMANTIC_FLAG_LIST(SEMANTIC_FL_ADD_OPT)
            // Валидаторы допустимых значений value-флагов (функции, не данные - остаются ручными;
            // гарантируют «no silent fallback»: потребитель видит только валидные значения).
            opts.set_flag_validator(semantic::FlagKind::SolverMode, [](std::string_view v) { return analysis::parseSolverMode(v).has_value(); });
            opts.set_flag_validator(semantic::FlagKind::SolverLoop, [](std::string_view v) { return analysis::parseSolverLoopMode(v).has_value(); });
            opts.set_flag_validator(semantic::FlagKind::SolverLoopUnroll, [](std::string_view v) { return parseBoolFlagValue(v).has_value(); });
            // Контроль переполнения стека: режим (off|explicit|recursion|auto), резерв (десятичное число)
            // и перечень функций (comma-separated trust-имена).
            opts.set_flag_validator(semantic::FlagKind::StackCheck, [](std::string_view v) { return analysis::parseStackCheckMode(v).has_value(); });
            opts.set_flag_validator(semantic::FlagKind::StackCheckReserve, [](std::string_view v) {
                if (v.empty()) {
                    return false;
                }
                for (const char c : v) {
                    if (c < '0' || c > '9') {
                        return false;
                    }
                }
                return true;
            });
            opts.set_flag_validator(semantic::FlagKind::StackCheckFunctions, [](std::string_view v) {
                // Допускаем непустой список имён, разделённых запятыми/пробелами (имена - идентификаторы).
                return !v.empty();
            });
            // Режим по умолчанию - explicit (безопасность по умолчанию): флаг включён, значение "explicit".
            opts.set_enabled(semantic::FlagKind::StackCheck, true);
        });
    }
};
const SemanticDiagnosticsRegistrar kSemanticDiagnostics;
} // namespace

namespace {
// Метка именованного прерывания без '%' и ':' (для сравнения с именем функции).
std::string interruptLabelName(std::string_view raw) {
    std::string s(raw);
    if (!s.empty() && s[0] == '%') {
        s.erase(0, 1);
    }
    s.erase(std::remove(s.begin(), s.end(), ':'), s.end());
    return s;
}
// Имя функции без '%' и ':' (как funcNameOf в lowering) - для валидации меток прерываний.
std::string interruptFuncName(const FuncDecl* fd) {
    if (!fd) {
        return {};
    }
    std::string s(requiredText(*fd));
    if (!s.empty() && s[0] == '%') {
        s.erase(0, 1);
    }
    s.erase(std::remove(s.begin(), s.end(), ':'), s.end());
    return s;
}
} // namespace

// namespace

NameResolutionPass::NameResolutionPass(AnalysisContext& actx)
: m_actx(actx)
, m_decl(actx, *this)
, m_typer(actx, *this)
, m_access(actx, *this)
, m_trust(actx, *this) {
}

void NameResolutionPass::addHook(std::unique_ptr<InlineAnalysisHook> hook) {
    if (hook) {
        m_hooks.push_back(std::move(hook));
    }
}

void NameResolutionPass::finalize() {
    for (auto& hook : m_hooks) {
        hook->finalize();
    }
}

// -- Скоупы с уведомлением хуков --

void NameResolutionPass::enterScope(const AstNodeBase& node) {
    m_actx.symbols().push(&node);
    TRUST_DEBUG("scope", "enter {} depth={}", ParserToken::name(node.kind()), m_actx.symbols().depth());
    for (auto& hook : m_hooks) {
        hook->enterScope();
    }
}

void NameResolutionPass::exitScope() {
    // Namespace-контейнер (`::`/`name::`) на уровне модуля: объявления должны пережить
    // выход из лексического блока и стать доступными квалифицированно (`name::x`) из любого
    // места модуля/импортёра. Переносим их в КОРНЕВОЙ скоуп под полным namespace-путём.
    const AstNodeBase* creator = m_actx.symbols().currentCreator();
    const ScopeBlock* namespaceBlock = creator != nullptr && creator->is<ScopeBlock>() ? creator->as<ScopeBlock>() : nullptr;
    const bool exportedNamespace =
        namespaceBlock != nullptr && !namespaceBlock->is_hidden() &&
        (namespaceBlock->isModuleInterface() ||
         (!namespaceBlock->is_anonymous() && (namespaceBlock->text() == "::" || namespaceBlock->text().find("::") != std::string_view::npos)));
    if (exportedNamespace && m_actx.namespaces().currentFunc() == nullptr && !m_actx.namespaces().insideTypeBody()) {
        const std::string prefix = m_actx.namespaces().namespacePath();
        // Перенос читает ТЕКУЩИЙ скоуп и пишет в КОРНЕВОЙ (другой map): копия скоупа не нужна.
        for (const auto& [scopeKey, sym] : m_actx.symbols().current().symbols) {
            Symbol qualified = sym;
            // Ключ блока внутри области имён - плоский (path пуст): квалифицируем короткое имя.
            const std::string name = utils::qualifyTrustName(prefix, scopeKey.name);
            qualified.name = stableSymbolName(name);
            const DeclResult result = m_actx.symbols().declareInRoot(qualified);
            if (result == DeclResult::Duplicate || result == DeclResult::SignatureMismatch) {
                m_actx.ctx().diag().report(Severity::Error, creator->range(), "duplicate declaration '{}'", name);
            }
        }
        // Namespace-алиас — тоже объявление области: квалифицированный (`outer::ns`) он переживает
        // выход из блока и участвует в longest-prefix-резолве.
        const Scope& leaving = m_actx.symbols().current();
        for (const std::size_t ref : leaving.aliasRefs) {
            const NamespaceAliasRecord& alias = m_actx.symbols().aliases().aliasAt(ref);
            NamespaceAliasRecord promoted = alias;
            promoted.alias = internText(utils::qualifyTrustName(prefix, alias.alias));
            if (!m_actx.symbols().aliases().promoteAliasToRoot(m_actx.symbols().moduleRootScope(), promoted)) {
                m_actx.ctx().diag().report(Severity::Error, creator->range(), "namespace alias '{}::' is already bound in this scope", promoted.alias);
            }
        }
    }
    // `... = X` на выходе из скоупа: область обязана существовать и быть NAMESPACE-областью
    // (защита от опечаток); все объявления блока к этому моменту известны.
    {
        const Scope& leaving = m_actx.symbols().current();
        for (const std::size_t ref : leaving.usingRefs) {
            checkSearchScope(m_actx.symbols().aliases().usingAt(ref));
        }
    }
    TRUST_DEBUG("scope", "exit {} depth={}",
                ParserToken::name(m_actx.symbols().currentCreator() ? m_actx.symbols().currentCreator()->kind() : ParserToken::Kind::Program),
                m_actx.symbols().depth());
    for (auto& hook : m_hooks) {
        hook->exitScope();
    }
    m_importKeys.erase(m_actx.symbols().depth());
    m_actx.symbols().pop();
}

void NameResolutionPass::checkSearchScope(const UsingScopeRecord& search) {
    // Область поиска — NAMESPACE: имена под префиксом `ns::name`. Тип владеет ЧЛЕНСКИМ скоупом
    // (`T::member`) и namespace-областью не является; значение/функция области не создают.
    const Symbol* owner =
        search.targetModule.has_value() ? m_actx.symbols().findVisibleFromModule(*search.targetModule, search.path, /*maskBypass=*/false,
                                                                                 /*viaAlias=*/true)
                                        : m_actx.symbols().resolve(search.path);
    if (owner != nullptr && m_actx.types().typeNameOfSymbol(*owner) != INVALID_TYPE_ID) {
        m_actx.ctx().diag().report(Severity::Error, search.operatorRange,
                                   "target '{}' is a type, not a namespace; 'using namespace' accepts namespaces only (use '{}::member' for static members)",
                                   search.path, search.path);
        return;
    }
    const bool exists = search.targetModule.has_value() ? m_actx.symbols().hasNamespaceInModule(*search.targetModule, search.path)
                                                        : m_actx.symbols().hasNamespaceInScopes(search.path);
    if (exists) {
        return;
    }
    if (owner != nullptr && owner->decl != nullptr && owner->decl->is<FuncDecl>()) {
        m_actx.ctx().diag().report(Severity::Error, search.operatorRange, "target '{}' is a function, not a namespace; extended search has no effect", search.path);
        return;
    }
    if (owner != nullptr) {
        m_actx.ctx().diag().report(Severity::Error, search.operatorRange, "target '{}' is a value, not a namespace; extended search has no effect", search.path);
        return;
    }
    m_actx.ctx().diag().report(Severity::Error, search.operatorRange, "namespace '{}' has no names; extended search has no effect (check the spelling)",
                               search.path);
}

// -- Обход --

void NameResolutionPass::run(std::vector<AstNodePtr>& ast_nodes) {
    for (auto& node : ast_nodes) {
        if (node) {
            analyzeNode(node);
        }
    }
    // Пост-проход: нераскрытые многоточия в СПИСКОВЫХ позициях (аргументы вызова/элементы
    // коллекций) - явные диагностики. Разворот выполняется по ходу анализа (где известно число
    // позиций); оставшиеся случаи (нет размера/сигнатуры, `... <источник>`) диагностируются здесь.
    reportUnresolvedEllipsis(m_actx, ast_nodes);
}

// Однопроходный обход: имя должно быть объявлено до использования. Модуль, блоки
// и ScopeBlock открывают вложенный скоуп; объявления регистрируются в текущем скоупе;
// Ident разрешается поиском вверх по стеку. Раскрытие контекст-макросов выполняет
// всегда-подключённый хук ContextMacroExpander (в начале обработки каждого узла).
void NameResolutionPass::analyzeNode(AstNodePtr& self) {
    // KIND_DISPATCH_GUARD_BEGIN - внутри блока запрещены if/switch по kind() (см. cmake guard).
    if (!self) {
        return;
    }

    // Раскрытие контекст-макросов (ContextMacro → Literal/IdentName, квалификатор @::
    // в именах) выполняет всегда-подключённый хук ContextMacroExpander. Он вызывается
    // ДО обработки ядра, чтобы имя объявления было раскрыто до регистрации, а ContextMacro
    // - заменён до резолва. Возврат true означает, что узел заменён хук-ом (ядро его
    // не обрабатывает, но продолжает обход детей).
    bool consumed = false;
    for (auto& hook : m_hooks) {
        if (hook->onNode(self)) {
            consumed = true;
        }
    }

    // Kind → действие: partial pass, решение - ДАННЫЕ (таблица, ast/kind_dispatch.hpp).
    // Default - doTraverse; переопределены только реальные действия. Таблица заполнена целиком.
    (this->*(semaActions()[kindIndex(self->kind())]))(self, consumed);
    // KIND_DISPATCH_GUARD_END
}

// Обход реальных детей через единый источник AstNodeBase::collectChildren (ссылки на
// слоты, чтобы хук мог заменять узлы). Не открывает скоупы - это делает analyzeNode.
void NameResolutionPass::analyzeChildren(AstNodePtr& self) {
    if (!self) {
        return;
    }
    std::vector<AstNodePtr*> slots;
    self->collectChildren(slots);
    for (auto* child : slots) {
        if (child) {
            analyzeNode(*child);
        }
    }
}

// -- Kind → действие (partial pass, ast/kind_dispatch.hpp) --------------------------------
// ОДНА таблица: fill(default) + точечные переопределения. Полнота по построению,
// «незаполненного» kind не существует; поведение на новом kind задано default'ом явно.

const NameResolutionPass::NodeHandlerTable& NameResolutionPass::semaActions() {
    using K = ParserToken::Kind;
    static constexpr NodeHandlerTable kTable = [] {
        NodeHandlerTable t;
        t.fill(&NameResolutionPass::doTraverse);
        t[kindIndex(K::END)] = &NameResolutionPass::doReject;
        t[kindIndex(K::ModuleDecl)] = &NameResolutionPass::doModuleDecl;
        t[kindIndex(K::ScopeBlock)] = &NameResolutionPass::doScopeBlock;
        t[kindIndex(K::sequence)] = &NameResolutionPass::doScopeContainer;
        t[kindIndex(K::TryCatchStmt)] = &NameResolutionPass::doScopeContainer;
        t[kindIndex(K::WhileStmt)] = &NameResolutionPass::doLoop;
        t[kindIndex(K::DoWhileStmt)] = &NameResolutionPass::doDoWhile;
        t[kindIndex(K::WithStmt)] = &NameResolutionPass::doWith;
        t[kindIndex(K::FuncDecl)] = &NameResolutionPass::doFuncDecl;
        t[kindIndex(K::ClassDecl)] = &NameResolutionPass::doSkip;
        t[kindIndex(K::StructDecl)] = &NameResolutionPass::doSkip;
        t[kindIndex(K::CatchBlock)] = &NameResolutionPass::doCatch;
        t[kindIndex(K::DestructureDecl)] = &NameResolutionPass::doDestructure;
        t[kindIndex(K::DictLiteral)] = &NameResolutionPass::doDict;
        t[kindIndex(K::ArrayInit)] = &NameResolutionPass::doArray;
        t[kindIndex(K::RangeExpr)] = &NameResolutionPass::doRange;
        t[kindIndex(K::CheckAreaStmt)] = &NameResolutionPass::doCheckArea;
        t[kindIndex(K::DebugStmt)] = &NameResolutionPass::doDebug;
        t[kindIndex(K::TrustContract)] = &NameResolutionPass::doTrustContract;
        t[kindIndex(K::TrustElem)] = &NameResolutionPass::doTrustElem;
        t[kindIndex(K::MemberAccess)] = &NameResolutionPass::doAccess;
        t[kindIndex(K::ArrayAccess)] = &NameResolutionPass::doAccess;
        t[kindIndex(K::AssignOp)] = &NameResolutionPass::doAssign;
        t[kindIndex(K::IfStmt)] = &NameResolutionPass::doBranch;
        t[kindIndex(K::MatchingStmt)] = &NameResolutionPass::doBranch;
        t[kindIndex(K::VarDecl)] = &NameResolutionPass::doVarDecl;
        t[kindIndex(K::TypeDecl)] = &NameResolutionPass::doTypeDecl;
        t[kindIndex(K::ReturnStmt)] = &NameResolutionPass::doReturnStmt;
        t[kindIndex(K::Ident)] = &NameResolutionPass::doIdentOrArg;
        t[kindIndex(K::ArgNode)] = &NameResolutionPass::doIdentOrArg;
        t[kindIndex(K::EmbedExpr)] = &NameResolutionPass::doEmbedExpr;
        t[kindIndex(K::AppendStmt)] = &NameResolutionPass::doAppendStmt;
        return t;
    }();
    return kTable;
}

// -- Действие фазы для группы kinds --

void NameResolutionPass::doReject(AstNodePtr& self, bool) {
    // END - не узел (default-конструктор AstNodeBase); молчаливый обход недопустим.
    faultUnexpectedKind("NameResolutionPass", *self);
}

void NameResolutionPass::doScopeContainer(AstNodePtr& self, bool) {
    // Скоуп-контейнеры (модуль/блок) открывают вложенный скоуп на время обхода тела.
    // ЛЮБОЙ блок (в т.ч. цикл while/do-while) создаёт скоуп - это локальность переменных.
    analyzeScopeContainer(self);
}

void NameResolutionPass::doLoop(AstNodePtr& self, bool) {
    // CFG/dataflow-пост-проход вычисляет DA; здесь - обход тела в отдельном скоупе.
    enterScope(*self);
    analyzeChildren(self);
    exitScope();
}

void NameResolutionPass::doDoWhile(AstNodePtr& self, bool) {
    enterScope(*self);
    analyzeChildren(self);
    exitScope();
}

void NameResolutionPass::doWith(AstNodePtr& self, bool) {
    analyzeWithStmt(self);
}

void NameResolutionPass::doFuncDecl(AstNodePtr& self, bool) {
    analyzeFuncDeclNode(self);
}

void NameResolutionPass::doSkip(AstNodePtr& self, bool) {
    // Forward-объявление (нативного) класса / Struct-Class обрабатывается целиком в analyzeTypeDecl
    // (регистрирует тип + члены). Здесь как дочерний узел TypeDecl - осознанный Skip: детей (членов)
    // НЕ обходим, иначе они стали бы отдельными объявлениями верхнего уровня, а не членами.
    skipNode(*self);
}

void NameResolutionPass::doCatch(AstNodePtr& self, bool) {
    analyzeCatchBlockNode(self);
}

void NameResolutionPass::doDestructure(AstNodePtr& self, bool) {
    // Деструктуризация `item, dict := ... source;`: цели не обходим общим механизмом.
    m_decl.analyzeDestructure(*self->as<DestructureDecl>());
}

void NameResolutionPass::doDict(AstNodePtr& self, bool) {
    // Литерал словаря: анализируем значения элементов (имена-метки не резолвим).
    m_typer.analyzeDictLiteral(*self->as<Sequence>());
}

void NameResolutionPass::doArray(AstNodePtr& self, bool) {
    // Литерал массива `[1,2,3,]` / `[1,2,3,]:Int32`: анализ элементов + вывод элемента.
    m_typer.analyzeArrayInit(*self->as<DictLiteralNode>());
}

void NameResolutionPass::doRange(AstNodePtr& self, bool) {
    // Литерал диапазона: резолв/типизация операндов + элементный тип (join).
    m_typer.analyzeRangeExpr(*self->as<RangeExpr>());
}

void NameResolutionPass::doCheckArea(AstNodePtr& self, bool) {
    // `@__CHECK_AREA__(area[, behavior][, attrs...])`: проверяет область и УДАЛЯЕТ маркер.
    analyzeCheckAreaStmt(self);
}

void NameResolutionPass::doDebug(AstNodePtr& self, bool) {
    // `@__DEBUG__`/`@__DEBUG_SCOPE__`: применяет эффект и УДАЛЯЕТ маркер.
    analyzeDebugStmt(self);
}

void NameResolutionPass::doTrustContract(AstNodePtr& self, bool) {
    // Автономный trust-контракт `@{ [kind:] expr @};` (не привязан к объявлению).
    m_trust.processTrustConditions({self}, *self);
}

void NameResolutionPass::doTrustElem(AstNodePtr& self, bool) {
    analyzeTrustElemNode(self);
}

void NameResolutionPass::doAccess(AstNodePtr& self, bool) {
    // Доступ к элементу словаря: объект анализируется, поле-имя не резолвится.
    m_access.analyzeAccess(*self->as<Binary>());
}

void NameResolutionPass::doAssign(AstNodePtr& self, bool consumed) {
    // `... = X` (using): регистрирует области имён RHS для поиска, RHS не резолвится как значение;
    // узел заменяется пустой sequence-нодой (kind != END) - compile-time директива, кода не даёт.
    // Иначе - обычное присваивание: общий обход.
    if (handleUsingImport(*self)) {
        self = std::make_shared<Sequence>(ParserToken::Kind::sequence);
        return;
    }
    doTraverse(self, consumed);
}

void NameResolutionPass::doBranch(AstNodePtr& self, bool) {
    // Ветвящиеся statement'ы (if / match) открывают вложенный скоуп НА ВРЕМЯ обхода детей:
    // маркер @__CHECK_AREA__(if/match) и локальные объявления видят область конструкта.
    // Flow-факты (DA/became-const) вычисляет CFG/dataflow-пост-проход; здесь — только
    // последовательный обход условий/паттернов и тел в собственных скоупах.
    enterScope(*self);
    if (self->kind() == ParserToken::Kind::IfStmt) {
        auto& n = static_cast<IfStmt&>(*self);
        if (n.m_cond) {
            analyzeNode(n.m_cond);
        }
        if (n.m_body) {
            analyzeNode(n.m_body);
        }
        for (auto& eif : n.m_elseifs) {
            if (eif.first) {
                analyzeNode(eif.first);
            }
            if (eif.second) {
                analyzeNode(eif.second);
            }
        }
        if (n.m_else) {
            analyzeNode(n.m_else);
        }
    } else { // MatchingStmt
        auto& m = static_cast<MatchStmt&>(*self);
        if (m.m_value) {
            analyzeNode(m.m_value);
        }
        for (auto& c : m.m_cases) {
            for (auto& p : c.patterns) {
                if (!p) {
                    continue;
                }
                if (p->kind() == ParserToken::Kind::TypeName) {
                    // Тип-паттерн (`match(x){ :Int32 => ... }`): материализуем разрешённый тип
                    // для кодогена (тот рендерит паттерн без повторного резолва имени).
                    if (!m_actx.types().resolveTypeRef(*p).has_value()) {
                        m_actx.ctx().diag().report(Severity::Error, p->range(), "unknown type '{}' in match pattern", requiredText(*p));
                    }
                } else {
                    analyzeNode(p);
                }
            }
            if (c.body) {
                analyzeNode(c.body);
            }
        }
        if (m.m_default) {
            analyzeNode(m.m_default);
        }
    }
    exitScope();
    analyzeNodeTail(self);
}

void NameResolutionPass::doTraverse(AstNodePtr& self, bool) {
    // Реакции на сам узел нет (default partial pass) - только дети и пост-обработка.
    finishTraverse(self);
}

void NameResolutionPass::finishTraverse(AstNodePtr& self) {
    analyzeChildren(self);
    // Пост-порядковая типизация и синтетические временные ($^ / match / return).
    analyzeNodeTail(self);
}

// -- Групповые обработчики: leaf-действие + стандартный обход --

void NameResolutionPass::doVarDecl(AstNodePtr& self, bool consumed) {
    if (!consumed) {
        handleVarDecl(self);
    }
    finishTraverse(self);
}

void NameResolutionPass::doTypeDecl(AstNodePtr& self, bool consumed) {
    if (!consumed) {
        handleTypeDecl(self);
    }
    finishTraverse(self);
}

void NameResolutionPass::doReturnStmt(AstNodePtr& self, bool consumed) {
    if (!consumed) {
        handleReturnStmt(self);
    }
    finishTraverse(self);
}

void NameResolutionPass::doIdentOrArg(AstNodePtr& self, bool consumed) {
    if (!consumed) {
        // `$^` - псевдопеременная «результат последней операции» (read-only). Простой случай
        // переписан ПРЕ-семантически (captureLastResult); этот guard - страховка для листа,
        // который проход не покрыл (напр. `$^` в составе составного sink).
        if (requiredText(*self) == "$^") {
            reportResultCaptureError(self);
        } else if (self->is<IdentName>()) {
            handleIdent(self);
        }
    }
    finishTraverse(self);
}

void NameResolutionPass::reportResultCaptureError(const AstNodePtr& self) {
    m_actx.ctx().diag().report(Severity::Error, self->range(), "pseudo-variable '$^' (result of the last operation) is not implemented yet");
}

void NameResolutionPass::doEmbedExpr(AstNodePtr& self, bool consumed) {
    if (!consumed) {
        handleEmbedExpr(self);
    }
    finishTraverse(self);
}

void NameResolutionPass::doAppendStmt(AstNodePtr& self, bool consumed) {
    if (!consumed) {
        handleAppendNode(self);
    }
    finishTraverse(self);
}

// -- Leaf-действия (без рекурсии) --

void NameResolutionPass::handleVarDecl(AstNodePtr& self) {
    m_decl.analyzeVarDecl(*self->as<VarDecl>());
}

void NameResolutionPass::handleTypeDecl(AstNodePtr& self) {
    m_decl.analyzeTypeDecl(*self->as<Binary>());
}

void NameResolutionPass::handleReturnStmt(AstNodePtr& self) {
    // Помечаем return ссылкой на определение функции (для пост-условий в кодогенерации:
    // узел самодостаточен, транспилятору не нужен текущий контекст функции).
    auto& js = *self->as<JumpStmt>();
    js.m_funcDecl = m_actx.namespaces().currentFunc();
    // Именованное положительное прерывание `name ++ value ++` = return по имени функции:
    // метка обязана быть текущей функцией либо глобальной `::` (exit/проброс). Иначе ошибка.
    // Неименованное `++ value ++` - «положительное прерывание» (throw IntPlus), валидно всегда.
    if (js.m_label) {
        const std::string labelText = std::string(requiredText(*js.m_label));
        if (labelText != "::") {
            const std::string fn = interruptFuncName(js.m_funcDecl);
            if (fn.empty() || interruptLabelName(labelText) != fn) {
                m_actx.ctx().diag().report(Severity::Error, js.m_label->range(), "named interrupt '{}' is not a return label of the current function '{}'",
                                           labelText, fn);
            }
        }
    }
}

void NameResolutionPass::handleIdent(AstNodePtr& self) {
    // Квалификатор @:: foo уже раскрыт хук-ом ContextMacroExpander (в analyzeNode);
    // здесь только резолвим имя.
    lookupOrError(*self);
}

void NameResolutionPass::handleEmbedExpr(AstNodePtr& self) {
    // Опция -Wembed (default Warning): предупреждение за сам факт использования C++-вставки
    // {% ... %} независимо от имён внутри. `-Wembed=ignore` подавляет вывод. Вызывается
    // ровно один раз на узел (обход семантики), в отличие от кодогенерации (рекурсия через emitExpr).
    auto& embed = static_cast<RawText&>(*self);
    embed.embedNames.clear(); // идемпотентность повторного прогона семантики
    m_actx.ctx().report(embed.range(), semantic::DiagId::Embed, "C++ code embedding {{% ... %}} is used");
    // C++-вставка ({% ... %}): trust-имена, на которые ссылается вставка ($name/@name),
    // проверяются на доступность в таблице символов; отсутствующие - предупреждение.
    // Найденный модуль объявления СОХРАНЯЕТСЯ: кодоген конвертирует маркер в C++-имя с
    // модульной изоляцией (иначе `@x` импортированного файла дал бы короткое `c_x`).
    for (const auto& nm : utils::extractEmbedNames(embed.text())) {
        const Symbol* found = m_actx.symbols().resolve(nm);
        // Квалифицированное имя (ns::x): таблица - плоский стек скоупов, поэтому полное
        // имя не находится; проверяем по последнему сегменту (грубая проверка доступности).
        if (!found) {
            if (utils::isQualifiedName(nm)) {
                found = m_actx.symbols().resolve(utils::shortNameOf(nm));
            }
        }
        if (!found) {
            m_actx.ctx().diag().report(Severity::Warning, embed.range(), "embed references name '{}' not declared in trust code", nm);
        }
        EmbedNameRef ref;
        ref.sigil = (!nm.empty() && nm.front() == '$') ? '$' : '@';
        const std::string_view bare = ref.sigil == '$' ? std::string_view(nm).substr(1) : std::string_view(nm);
        ref.name = stableSymbolName(bare);
        ref.module = found != nullptr && found->decl != nullptr ? found->decl->module : ModuleIndex::invalid();
        embed.embedNames.push_back(ref);
    }
}

void NameResolutionPass::handleAppendNode(AstNodePtr& self) {
    handleAppendStmt(*self->as<Binary>());
}

void NameResolutionPass::handleAppendStmt(const Binary& append) {
    // Учёт `[]=` в статическом размере словаря: `d []= v` увеличивает известный размер
    // (dims) целевого словаря - чтобы статическая проверка `d.N` далее по тексту видела
    // выросший размер (после двух append размер 3 → 5). LHS - простой Ident (вложенный
    // отклонён в typeExpr); dims >= 0 означает «словарь с известным размером».
    //
    // Spread-merge `d []= ... dict` (RHS - Ellipsis): добавляются ВСЕ элементы словаря-
    // операнда, поэтому dims растёт на число элементов операнда, а типы полей переносятся
    // в dictFieldTypes цели. Без `...` - одиночный элемент: dims += 1, типы полей
    // не регистрируются - «Any» для добавленных позиционных.
    if (append.m_left && append.m_left->kind() == ParserToken::Kind::Ident) {
        if (Symbol* s = resolveSimple(append.m_left.get(), requiredText(*append.m_left))) {
            if (s->dims < 0) {
                return; // статический размер цели неизвестен - отслеживать нечего
            }
            const AstNodeBase* rhs = append.m_right.get();
            if (rhs && rhs->kind() == ParserToken::Kind::Ellipsis) {
                const auto& ell = *rhs->as<Sequence>();
                const AstNodeBase* operand = ell.m_body.empty() ? nullptr : ell.m_body[0].get();
                if (operand && operand->kind() == ParserToken::Kind::DictLiteral) {
                    // Компиляционно известный словарь-литерал: каждый элемент - новый элемент.
                    const auto& dl = *operand->as<Sequence>();
                    for (const auto& el : dl.m_body) {
                        if (!el) {
                            continue;
                        }
                        std::string fname;
                        const AstNodeBase* val = nullptr;
                        collectionElementNameValue(el.get(), fname, val);
                        s->dims += 1;
                        s->dictFieldTypes.emplace_back(fname, val ? m_typer.dictElementType(val) : QualType{});
                    }
                } else if (operand && operand->kind() == ParserToken::Kind::Ident) {
                    // Словарь-переменная: переносим её известный размер и типы полей.
                    // Простое имя могло быть объявлено как локальная `$x` (опция -Wsigil).
                    if (Symbol* src = resolveSimple(nullptr, requiredText(*operand))) {
                        if (isDictTypeId(m_actx.ctx().types(), src->type.getTypeId())) {
                            if (src->dims >= 0) {
                                s->dims += src->dims;
                            }
                            s->dictFieldTypes.insert(s->dictFieldTypes.end(), src->dictFieldTypes.begin(), src->dictFieldTypes.end());
                        }
                    }
                }
                // Прочий dict-операнд (выражение): статический размер неизвестен - не меняем.
                return;
            }
            // Одиночный элемент (не spread): размер +1 и регистрация типа поля по позиции,
            // чтобы dictFieldTypes оставался выровнен по dims (инвариант: число записей
            // dictFieldTypes == известный размер). Для литерала тип выводится, для
            // переменной/выражения (до анализа) - Any (INVALID).
            s->dims += 1;
            s->dictFieldTypes.emplace_back("", rhs ? m_typer.dictElementType(rhs) : QualType{});
        }
    }
    return;
}

// -- Встроенный маркер `@__CHECK_AREA__` -- -----------------------------------------

// -- Trust-условия (пред/пост/утверждение) -------------------------

// -- Разрешение имён --

const Symbol* NameResolutionPass::lookupOrError(AstNodeBase& node) {
    const std::string name(requiredText(node));
    // `_` (None/underscore) - специальное значение, а не имя переменной: используется как
    // none-значение и как discard в swap-move `var :=: _` → std::move(var). Не резолвим.
    if (name == "_") {
        return nullptr;
    }
    const Symbol* sym = resolveSimple(&node, name);
    // Параметр-пакет нельзя использовать как значение: он существует только для раскрытия
    // в C++-сигнатуре (`T... xs`) и передаётся целиком, а не поэлементно.
    if (sym && sym->type.getTypeId() != INVALID_TYPE_ID && m_actx.ctx().readTypes().store().isPackExpansionType(sym->type.getTypeId())) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "type parameter pack '{}' cannot be used as a value in an expression", name);
    }
    // Trust-контракты: в пред-условии (kind=Pre) имя самой функции (возвращаемое значение)
    // ЗАПРЕЩЕНО; в пост-условии (kind=Post) - легально (это возврат). Контекст и текущая функция
    // выводятся из стека скоупов (creator), а не из отдельного состояния (см. currentTrustKind).
    if (sym && m_actx.namespaces().currentTrustKind() == PropertyKind::Pre) {
        if (const FuncDecl* f = m_actx.namespaces().currentFunc(); f && sym->decl == f) {
            m_actx.ctx().diag().report(Severity::Error, node.range(),
                                       "cannot use function '{}' (return value) in a precondition; use it only in a postcondition", name);
        }
    }
    // Зарегистрированные runtime-символы (например %trust::trust__abort__) и интринсики языка
    // (например trust::intrinsic_assert, без native-префикса '%') - известные имена (публичный
    // runtime-заголовок / распознаваемые компилятором); «undefined name» для них не выдаём
    // (транспилер эмитит их разворачиванием на этапе генерации, см. CppTranspiler::emitIntrinsic).
    const bool isRuntime = !sym && (m_actx.types().isRegisteredRuntimeSymbol(name) || m_actx.types().isRegisteredIntrinsic(name));
    TRUST_DEBUG("resolve", "name '{}' -> {}", name, sym ? "resolved" : (isRuntime ? "runtime" : "undefined"));
    // Стабильная ссылка на объявление для пост-проходных flow-анализов (DA): SymbolTable
    // после обхода недоступна, а resolvedDecl переживает pop скоупов (узел AST).
    if (node.kind() == ParserToken::Kind::Ident) {
        if (auto* ident = node.as<IdentName>()) {
            ident->resolvedDecl = sym ? sym->decl : nullptr;
            ident->valueType = QualType{};
            // Идентификатор, обозначающий ТИП (ctor/static/enum/alias-base/имя объявления):
            // материализуем тип для кодогена (он не резолвит имя типа повторно).
            if (sym != nullptr) {
                const TypeId typeNameId = m_actx.types().typeNameOfSymbol(*sym);
                if (typeNameId != INVALID_TYPE_ID) {
                    m_actx.types().recordTypeName(node, typeNameId);
                } else {
                    // Значение: фиксируем type-state В ТОЧКЕ использования (valueType).
                    // Дальнейшее изменение inferred-типа символа ссылку не переписывает.
                    ident->valueType = sym->type;
                }
            }
        }
    }
    for (auto& hook : m_hooks) {
        hook->onResolve(node, sym);
    }
    if (!sym && !isRuntime) {
        // Модульная ссылка: единая точка текстов (M1) — «модуль такого имени не экспортирует»
        // либо «имя скрыто маской сайта импорта».
        if (node.is<IdentName>()) {
            const auto& ident = static_cast<const IdentName&>(node);
            if (ident.moduleRef.has_value()) {
                m_actx.types().reportModuleRefError(ident);
                return sym;
            }
        }
        // Неоднозначность голого имени: одноимённые экспорты/локальное объявление и импорт.
        // Разрешается квалификацией `\модуль::имя` (диагностика — на сайте использования).
        if (const std::vector<ModuleIndex>* candidates = m_actx.symbols().ambiguousModules(name); candidates != nullptr) {
            m_actx.types().reportAmbiguousName(node, name, *candidates, "name");
            return sym;
        }
        if (!m_usingAmbiguityReported) {
            m_actx.ctx().diag().report(Severity::Error, node.range(), "undefined name '{}'", name);
        }
        m_usingAmbiguityReported = false;
    }
    return sym;
}

// Единый алгоритм разрешения простого имени с правилами вывода сигилов.
// Порядок для bare-имени `x`: `$x` (локальная) → `x` (глобал/параметр) → `%x` (нативная функция).
// Для `$`-имени `$x`: сначала `$x`; если нет - bare `x` (параметр/локальная без сигила): `n` и
// `$n` - одно локальное имя. При попадании на `$x`/`%x` текст узла-ссылки (node) нормализуется
// на эту форму, чтобы манглинг (nameToCpp срезает `$`/`%` → c_x / x) совпал с объявлением.
// Квалифицированные/сигилные/нативные имена, найденные напрямую, резолвятся как есть.
// node может быть nullptr (тогда текст не меняется).
void NameResolutionPass::normalizeResolvedRef(AstNodeBase* node, const Symbol& sym) {
    if (node == nullptr || node->kind() != ParserToken::Kind::Ident) {
        return;
    }
    if (sym.decl == nullptr || !sym.decl->module.isValid() || sym.decl->module == m_actx.symbols().currentModule()) {
        return; // объявление своего модуля (в т.ч. локальное) — текст уже верен
    }
    HasText* text = node->as_text();
    if (text == nullptr || text->text() == sym.name) {
        return;
    }
    text->set_text_interned(sym.name);
    if (auto* ident = node->as<IdentName>(); ident != nullptr) {
        ident->moduleRef = sym.decl->module;
    }
}

Symbol* NameResolutionPass::resolveSimple(AstNodeBase* node, std::string_view name) {
    // Модульная ссылка `\path::name`: имя обязано быть экспортом ЭТОГО модуля и видимым на
    // текущем сайте (M1). Обычный lookup не используется — иначе ссылка на чужой модуль
    // молча связалась бы с одноимённым именем плоского пространства.
    if (node != nullptr && node->is<IdentName>()) {
        auto* ident = node->as<IdentName>();
        if (ident->moduleRef.has_value()) {
            const Symbol* resolved = m_actx.types().resolveModuleRef(*ident);
            if (resolved != nullptr) {
                normalizeResolvedRef(node, *resolved);
            }
            return const_cast<Symbol*>(resolved);
        }
    }
    const bool simple = isSimpleVarName(name);
    // bare x: сначала локальная $x (при попадании - нормализуем текст узла на $x).
    if (simple) {
        const std::string sigil_name = "$" + std::string(name);
        if (Symbol* s = m_actx.symbols().resolveMutable(sigil_name)) {
            if (node && node->kind() == ParserToken::Kind::Ident) {
                HasText* text = node->as_text();
                EXPECT(text && "Ident node must carry text");
                text->set_text_interned(sigil_name);
            }
            return s;
        }
    }
    // как есть (глобальная/параметр/квалифицированная/сигилная).
    if (Symbol* s = m_actx.symbols().resolveMutable(name)) {
        normalizeResolvedRef(node, *s);
        return s;
    }
    // Правила вывода сигилов:
    //  - bare x → нативная функция %x (напр. %fib вызывается как fib);
    //  - $x → локальная/параметр без сигила (n и $n - одно локальное имя).
    if (simple) {
        const std::string native_name = "%" + std::string(name);
        if (Symbol* s = m_actx.symbols().resolveMutable(native_name)) {
            if (node && node->kind() == ParserToken::Kind::Ident) {
                HasText* text = node->as_text();
                EXPECT(text && "Ident node must carry text");
                text->set_text_interned(native_name);
            }
            return s;
        }
    } else if (!name.empty() && name.front() == '$') {
        if (Symbol* s = m_actx.symbols().resolveMutable(name.substr(1))) {
            return s;
        }
    }
    // Fallback оператора `... = X` (using): голое имя, не найденное напрямую, ищем в
    // зарегистрированных областях имён. Для каждого префикса (от внутреннего скоупа к
    // внешнему) пробуем квалифицированное `prefix::name`; символ должен быть зарегистрирован
    // под ключом `prefix::name`. Действует только для trust-имён (simple).
    if (simple) {
        // Цели `using` резолвятся ОДНИМ проходом: от внутреннего скоупа к внешнему, внутри
        // скоупа — все цели сразу (слияние наборов перегрузок живёт в зоне резолва).
        const UsingResolveResult resolved = m_actx.symbols().names().resolveUsing(name);
        if (resolved.symbol != nullptr) {
            // Текст ссылки НОРМАЛИЗУЕТСЯ на квалифицированное имя (строковая цель): иначе кодоген
            // эмитит неквалифицированный C++-символ (`c_count` вместо `c_MyStr::c_count`).
            if (!resolved.qualifiedText.empty() && node != nullptr && node->kind() == ParserToken::Kind::Ident) {
                HasText* text = node->as_text();
                EXPECT(text != nullptr && "Ident node must carry text");
                if (text != nullptr) {
                    text->set_text_interned(resolved.qualifiedText);
                }
                if (resolved.module.has_value()) {
                    if (auto* ident = node->as<IdentName>(); ident != nullptr) {
                        ident->moduleRef = resolved.module;
                    }
                }
            }
            return resolved.symbol;
        }
        if (resolved.ambiguous) {
            m_usingAmbiguityReported = true;
            m_actx.ctx().diag().report(Severity::Error, node != nullptr ? node->range() : MapperRange{},
                                       "ambiguous name '{}': found in several 'using' namespaces of one scope; qualify it explicitly", name);
        }
    }
    return nullptr;
}

// Оператор `... = X` (using): RHS - область(и) имён, регистрируются как префиксы поиска
// в текущем скоупе. Возвращает true, если узел - AssignOp с левым Ellipsis (RHS как значение
// не резолвится). Формы RHS: одиночная `... = ns::name` (right = Ident), несколько
// `... = a, b` (right = CallExpr: callee=первый, args=остальные).
bool NameResolutionPass::handleUsingImport(const AstNodeBase& self) {
    if (self.kind() != ParserToken::Kind::AssignOp) {
        return false;
    }
    const auto& b = static_cast<const Binary&>(self);
    if (!b.m_left || b.m_left->kind() != ParserToken::Kind::Ellipsis || !b.m_right) {
        return false;
    }
    std::vector<const AstNodeBase*> path_nodes;
    const AstNodeBase& rhs = *b.m_right;
    if (rhs.kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(rhs);
        if (call.m_callee) {
            path_nodes.push_back(call.m_callee.get());
        }
        if (call.m_args) {
            for (const auto& arg : *call.m_args) {
                if (arg) {
                    path_nodes.push_back(arg.get());
                }
            }
        }
    } else {
        path_nodes.push_back(&rhs);
    }
    // Области поиска живут до конца текущей области видимости; `_` — не область имён.
    for (const AstNodeBase* path_node : path_nodes) {
        // Модульно-квалифицированная цель `... = \lib::ns;`: путь резолвится в экспорт-наборе
        // указанного модуля (одноимённые области разных модулей больше не сливаются).
        std::optional<ModuleIndex> targetModule;
        if (path_node->is<IdentName>()) {
            targetModule = path_node->as<IdentName>()->moduleRef;
        }
        const std::string_view path = requiredText(*path_node);
        if (path.empty()) {
            m_actx.ctx().diag().report(Severity::Error, path_node->range(), "empty namespace path in extended name search");
            continue;
        }
        if (path == "_") {
            m_actx.ctx().diag().report(Severity::Error, path_node->range(),
                                       "extended name search cannot be cancelled: it ends with the current scope; use a nested block to limit it");
            continue;
        }
        m_actx.symbols().aliases().recordUsing(
            m_actx.symbols().current(),
            UsingScopeRecord{m_actx.symbols().currentModule(), internText(path), targetModule, path_node->range(), m_actx.symbols().currentScopeRange()});
    }
    return true;
}
} // namespace trust
