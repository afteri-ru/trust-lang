// Generated: src/semantic/name_resolution.cpp (driver)
#include "semantic/name_resolution.hpp"
#include "diag/flag_values.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/ellipsis.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/stack_check.hpp"
#include "analysis/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "semantic/ref_kind.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"
#include "utils/strings.hpp"
#include "utils/trace.hpp"
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
            opts.set_flag_validator(semantic::FlagKind::SolverMode, [](std::string_view v) { return semantic::parseSolverMode(v).has_value(); });
            opts.set_flag_validator(semantic::FlagKind::SolverLoop, [](std::string_view v) { return semantic::parseSolverLoopMode(v).has_value(); });
            opts.set_flag_validator(semantic::FlagKind::SolverLoopUnroll, [](std::string_view v) { return parseBoolFlagValue(v).has_value(); });
            // Контроль переполнения стека: режим (off|explicit|recursion|auto), резерв (десятичное число)
            // и перечень функций (comma-separated trust-имена).
            opts.set_flag_validator(semantic::FlagKind::StackCheck, [](std::string_view v) { return semantic::parseStackCheckMode(v).has_value(); });
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
    std::string s(fd->text());
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
    TRUST_DEBUG("scope", "exit {} depth={}",
                ParserToken::name(m_actx.symbols().currentCreator() ? m_actx.symbols().currentCreator()->kind() : ParserToken::Kind::Program),
                m_actx.symbols().depth());
    for (auto& hook : m_hooks) {
        hook->exitScope();
    }
    finishUntypedUnderscoreDecls();
    m_actx.symbols().pop();
}

// Нетипизированная локальная `x := _;` так и не получила конкретный тип ни из одной записи
// (structuralType == INVALID) при выходе из своего скоупа → Error «cannot infer type». Это
// НЕ -Wunused-variable: типа нет, вывод из записей невозможен (или переменная просто не
// использовалась и не записывалась). Срабатывает только для реального untyped-`_` объявления;
// типизированные `x:T := _` / `x:Any := _` (тип известен) и обычные объявления не затрагиваются.
// Если переменная УЖЕ прочитана до записи (=> Error «read before it is initialized»), отдельную
// «cannot infer» не дублируем (uninitVarReported) - первичная причина уже зафиксирована.
void NameResolutionPass::finishUntypedUnderscoreDecls() {
    const auto& cur = m_actx.symbols().current();
    for (const auto& kv : cur.symbols) {
        const Symbol& sym = kv.second;
        if (sym.storage != Storage::Local) {
            continue; // только локальные; нелокальные untyped `:= _` уже дали Error «non-local»
        }
        if (!sym.decl || sym.decl->kind() != ParserToken::Kind::VarDecl) {
            continue;
        }
        const auto* vd = static_cast<const VarDecl*>(sym.decl);
        if (vd->m_type || !vd->m_initializer || !isNoneMarker(vd->m_initializer.get())) {
            continue; // не untyped-`_`
        }
        if (structuralType(sym.type) != INVALID_TYPE_ID) {
            continue; // тип выведен из записей (widenInferredTarget) - всё в порядке
        }
        if (m_actx.uninitVarReported(sym.decl)) {
            continue; // уже Error «read before it is initialized»
        }
        std::string name = sym.name;
        if (!name.empty() && name.front() == '$') {
            name.erase(0, 1); // DSL-сигил: показываем имя без служебного '$'
        }
        m_actx.ctx().diag().report(Severity::Error, vd->nameRange(),
                                   "cannot infer the type of untyped '{} := _' from any assignment; "
                                   "annotate it ('{} :Any := _') or remove the declaration",
                                   name, name);
    }
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

    // Скоуп-контейнеры (модуль/блок) открывают вложенный скоуп на время обхода тела.
    // ЛЮБОЙ блок (в т.ч. цикл while/do-while) создаёт скоуп - это локальность переменных:
    // объявленные в теле цикла видны только внутри него. Детекция цикла (для диагностик
    // деструктуризации) - по creator-скоупа в стеке (isInLoop).
    const ParserToken::Kind self_kind = self->kind();
    if (self_kind == ParserToken::Kind::ModuleDecl || self_kind == ParserToken::Kind::sequence || self_kind == ParserToken::Kind::ScopeBlock ||
        self_kind == ParserToken::Kind::TryCatchStmt) {
        analyzeScopeContainer(self);
        return;
    }
    if (self_kind == ParserToken::Kind::WhileStmt) {
        analyzeLoopConstruct(self, /*isDoWhile=*/false);
        return;
    }
    if (self_kind == ParserToken::Kind::DoWhileStmt) {
        analyzeLoopConstruct(self, /*isDoWhile=*/true);
        return;
    }
    if (self_kind == ParserToken::Kind::WithStmt) {
        analyzeWithStmt(self);
        return;
    }
    if (self_kind == ParserToken::Kind::FuncDecl) {
        analyzeFuncDeclNode(self);
        return;
    }
    if (self_kind == ParserToken::Kind::ClassDecl) {
        // Forward-объявление (нативного) класса - обрабатывается целиком в analyzeTypeDecl
        // (регистрирует тип + члены-интерфейс). Здесь как дочерний узел TypeDecl - no-op:
        // детей (членов) НЕ обходим, иначе они стали бы отдельными функциями/переменными
        // верхнего уровня, а не членами класса.
        return;
    }
    if (self_kind == ParserToken::Kind::StructDecl) {
        // Объявление пользовательского Struct/Class - обрабатывается целиком в analyzeTypeDecl
        // (analyzeRecordDecl: регистрирует тип + поля/методы). Здесь как дочерний узел TypeDecl -
        // no-op: членов НЕ обходим повторно (они уже проанализированы в класc-скоупе).
        return;
    }
    if (self_kind == ParserToken::Kind::CatchBlock) {
        analyzeCatchBlockNode(self);
        return;
    }
    if (self_kind == ParserToken::Kind::DestructureDecl) {
        // Деструктуризация `item, dict := ... source;`: первый target объявляется локальной
        // std::any-переменной (первый элемент), источник мутируется pop_front. Цели не обходим
        // общим механизмом (это объявления, не ссылки).
        m_decl.analyzeDestructure(*self->as<DestructureDecl>());
        return;
    }
    if (self_kind == ParserToken::Kind::DictLiteral) {
        // Литерал словаря: анализируем значения элементов (имена-метки не резолвим).
        m_typer.analyzeDictLiteral(*self->as<Sequence>());
        return;
    }
    if (self_kind == ParserToken::Kind::ArrayInit) {
        // Литерал массива `[1,2,3,]` / `[1,2,3,]:Int32`: анализ элементов + вывод типа
        // элемента + интернирование структурного Array<Elem> (см. analyzeArrayInit).
        m_typer.analyzeArrayInit(*self->as<DictLiteralNode>());
        return;
    }
    if (self_kind == ParserToken::Kind::RangeExpr) {
        // Литерал диапазона: резолв/типизация операндов + элементный тип (join).
        m_typer.analyzeRangeExpr(*self->as<RangeExpr>());
        return;
    }
    if (self_kind == ParserToken::Kind::CheckAreaStmt) {
        // Встроенный маркер `@__CHECK_AREA__(area[, behavior][, attrs...])` из тела макроса.
        // Проверяет текущую область (из единого скоуп-стека) и УДАЛЯЕТ маркер (кода не даёт).
        analyzeCheckAreaStmt(self);
        return;
    }
    if (self_kind == ParserToken::Kind::DebugStmt) {
        // Встроенные маркеры отладочного вывода `@__DEBUG__(...)`/`@__DEBUG_SCOPE__(...)`:
        // применяет эффект (фильтр вывода / дамп скоупа) и УДАЛЯЕТ маркер (кода не даёт).
        analyzeDebugStmt(self);
        return;
    }
    if (self_kind == ParserToken::Kind::TrustContract) {
        // Автономный trust-контракт `@{ [kind:] expr @};` в последовательности (не привязан
        // к объявлению). Обработка по -Wsolver/--solver-mode (см. processTrustConditions).
        m_trust.processTrustConditions({self}, *self);
        return;
    }
    if (self_kind == ParserToken::Kind::TrustElem) {
        analyzeTrustElemNode(self);
        return;
    }
    if (self_kind == ParserToken::Kind::MemberAccess || self_kind == ParserToken::Kind::ArrayAccess) {
        // Доступ к элементу словаря: объект анализируется, поле-имя не резолвится,
        // статический индекс проверяется по размерности объекта.
        m_access.analyzeAccess(*self->as<Binary>());
        return;
    }
    if (self_kind == ParserToken::Kind::AssignOp) {
        // Оператор `... = X` (using): регистрирует область/области имён RHS для поиска
        // имён, RHS НЕ резолвится как значение. Заменяется пустым узлом (compile-time
        // директива, кода не генерирует). Иначе - обычное присваивание.
        if (handleUsingImport(*self)) {
            self = std::make_shared<Sequence>();
            return;
        }
        // иначе - обычное присваивание: продолжаем общую пост-обработку ниже
    }

    // Ветвящиеся statement'ы (if / match) открывают вложенный скоуп НА ВРЕМЯ обхода детей:
    // маркер @__CHECK_AREA__(if/match) и локальные объявления в теле видят область конструкта.
    // ВАЖНО: НЕ включаем их в раннюю enterScope-группу (см. switch выше) - она возвращает ДО
    // typeExpr и пост-обработки (scrutinee-временная для match), что сломало бы хвост. Здесь
    // скоуп оборачивает только handleNode+analyzeChildren; typeExpr/пост-шаги идут ПОСЛЕ выхода.
    const bool branchScope = (self->kind() == ParserToken::Kind::IfStmt || self->kind() == ParserToken::Kind::MatchingStmt);
    if (branchScope) {
        enterScope(*self);
    }
    // Обработка узла по kind (если он не был заменён хук-ом) + полный обход детей.
    if (!consumed) {
        handleNode(self);
    }
    if (self->kind() == ParserToken::Kind::IfStmt || self->kind() == ParserToken::Kind::MatchingStmt) {
        // Definite-assignment для if/else-if/else и match: единый снапшот+merge флагов по веткам.
        analyzeBranchConstruct(self);
    } else {
        analyzeChildren(self);
    }
    if (branchScope) {
        exitScope();
    }

    // Пост-порядковая типизация и синтетические временные ($^ / match / return).
    analyzeNodeTail(self);
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

// -- Definite-assignment: снапшот+merge ветвей (if/else-if/else), все пер-Symbol флаги --

std::vector<std::pair<Symbol*, TypeId>> NameResolutionPass::snapshotSymbolFlags() {
    // Маска ВСЕХ пер-Symbol флагов (kSymbolFlagsMask) каждого символа текущего скоуп-стека
    // (внутренний → глобальный). Порядок детерминирован (скоуп-стек + std::map имён).
    std::vector<std::pair<Symbol*, TypeId>> out;
    m_actx.symbols().forEachScopeMutable([&](SymbolTable::Scope& sc) {
        for (auto& kv : sc.symbols) {
            (void)kv.first;
            out.emplace_back(&kv.second, kv.second.type & kSymbolFlagsMask);
        }
    });
    return out;
}

void NameResolutionPass::restoreSymbolFlags(const std::vector<std::pair<Symbol*, TypeId>>& snap) {
    // Восстановление маски = «переопределение флагов в отдельном скоупе с сохранением исходных»:
    // каждая ветка стартует из состояния entry (для ВСЕХ флагов - Inferred/Const/Uninit),
    // эффекты предыдущей ветки гасятся (тип/registry_index не затрагиваются).
    for (const auto& [sym, mask] : snap) {
        sym->type = clearSymbolFlags(sym->type) | mask;
    }
}

void NameResolutionPass::applyMergeFlags(const std::vector<std::pair<Symbol*, TypeId>>& snap, const std::vector<std::vector<TypeId>>& pathExitMasks,
                                         bool hasElse) {
    // Покрытие путей по каждому пер-Symbol флагу отдельно (согласованность на всех путях):
    //   * Uninit («неинициализирована») - OR выходов веток: неинициализирована после, если хоть
    //     один путь оставил неинициализированной; `if` без else учитывает путь entry.
    //   * Const/Inferred (свойство «на всех путях») - AND выходов веток (+ путь entry при отсутствии else).
    for (size_t i = 0; i < snap.size(); ++i) {
        const TypeId entryMask = snap[i].second;
        bool anyUninit = false, allConst = true, allInferred = true;
        for (const auto& pm : pathExitMasks) {
            const TypeId m = pm[i];
            if ((m & kUninitFlag) != 0) {
                anyUninit = true;
            }
            if ((m & kConstFlag) == 0) {
                allConst = false;
            }
            if ((m & kInferredFlag) == 0) {
                allInferred = false;
            }
        }
        bool mergedUninit = anyUninit || (!hasElse && (entryMask & kUninitFlag) != 0);
        bool mergedConst = allConst && (hasElse || (entryMask & kConstFlag) != 0);
        bool mergedInferred = allInferred && (hasElse || (entryMask & kInferredFlag) != 0);
        TypeId& t = snap[i].first->type;
        t = clearSymbolFlags(t);
        if (mergedUninit) {
            t = setFlag(t, SymbolFlag::Uninit);
        }
        if (mergedConst) {
            t = setFlag(t, SymbolFlag::Const);
        }
        if (mergedInferred) {
            t = setFlag(t, SymbolFlag::Inferred);
        }
    }
}

// ЕДИНЫЙ definite-assignment для ВСЕХ ветвящихся конструктов (if/else-if/else и match): снапшот
// маски пер-Symbol флагов на входе creator-скоупа; каждый «arm» (условие/scrutinee/паттерн - из
// состояния entry) и тело ветки пересматриваются ОТ снапшота (переопределение флагов в отдельном
// скоупе с сохранением исходных - изменения одной ветки не текут в другие); на выходе каждого тела
// снимается маска флагов; после завершения всех вложенных скоупов - merge покрытия флагов по путям
// (applyMergeFlags). Вызывается из analyzeNode после enterScope (creator-скоуп конструкта на стеке);
// typeExpr/пост-шаги - в analyzeNode.
void NameResolutionPass::analyzeBranchConstruct(AstNodePtr& self) {
    std::vector<std::pair<Symbol*, TypeId>> entry = snapshotSymbolFlags();
    std::vector<std::vector<TypeId>> pathExitMasks;
    bool hasFallback = false;
    if (self->kind() == ParserToken::Kind::IfStmt) {
        IfStmt& n = static_cast<IfStmt&>(*self);
        if (n.m_cond) { // условие if читается из состояния entry
            restoreSymbolFlags(entry);
            analyzeNode(n.m_cond);
        }
        if (n.m_body) {
            analyzeBranchBody(n.m_body, entry, pathExitMasks); // then
        }
        for (auto& eif : n.m_elseifs) {
            if (eif.first) { // условие else-if (из entry: выполняется только при false предыдущих)
                restoreSymbolFlags(entry);
                analyzeNode(eif.first);
            }
            if (eif.second) {
                analyzeBranchBody(eif.second, entry, pathExitMasks);
            }
        }
        hasFallback = (n.m_else != nullptr);
        if (n.m_else) {
            analyzeBranchBody(n.m_else, entry, pathExitMasks);
        }
    } else { // MatchingStmt
        MatchStmt& m = static_cast<MatchStmt&>(*self);
        if (m.m_value) { // scrutinee читается из состояния entry
            restoreSymbolFlags(entry);
            analyzeNode(m.m_value);
        }
        for (auto& c : m.m_cases) {
            for (auto& p : c.patterns) {
                if (p) {
                    restoreSymbolFlags(entry);
                    analyzeNode(p);
                }
            }
            if (c.body) {
                analyzeBranchBody(c.body, entry, pathExitMasks);
            }
        }
        hasFallback = (m.m_default != nullptr);
        if (m.m_default) {
            analyzeBranchBody(m.m_default, entry, pathExitMasks);
        }
    }
    applyMergeFlags(entry, pathExitMasks, hasFallback);
}

// Обработка тела одной ветки изолированно: восстановить состояние entry, проанализировать тело,
// снять маску флагов на выходе и добавить её (выровненную по entry) в pathExitMasks для merge.
void NameResolutionPass::analyzeBranchBody(AstNodePtr& body, const std::vector<std::pair<Symbol*, TypeId>>& entry,
                                           std::vector<std::vector<TypeId>>& pathExitMasks) {
    restoreSymbolFlags(entry);
    if (body) {
        analyzeNode(body);
    }
    std::vector<std::pair<Symbol*, TypeId>> ex = snapshotSymbolFlags();
    std::vector<TypeId> row(entry.size());
    for (size_t i = 0; i < entry.size(); ++i) {
        row[i] = ex[i].second; // маска флагов на выходе по этой ветке
    }
    pathExitMasks.push_back(std::move(row));
}

// Циклы while/do-while - недоказуемый для definite-assignment путь: тело может выполниться 0..n раз.
// Консервативный merge (никаких silent-fallback): переменная после цикла считается инициализированной
// только если это доказуемо на всех путях. while: тело может не выполниться вовсе → обязателен и путь
// «0 итераций» = entry (entry-неинициализированная остаётся неинициализированной). do-while: тело
// выполняется минимум один раз → состояние = выход тела. Сброс (x = _) в теле в любом цикле оставляет
// переменную после цикла возможно-неинициализированной. Итог: чтение после цикла такой переменной
// даёт Error (чтение до инициализации), а не тихий пропуск.
void NameResolutionPass::analyzeLoopConstruct(AstNodePtr& self, bool isDoWhile) {
    std::vector<std::pair<Symbol*, TypeId>> entry = snapshotSymbolFlags();
    enterScope(*self);
    analyzeChildren(self);
    exitScope();
    // exitSnapshot после выхода из скоупа цикла (локальные тела удалены) - тот же набор символов.
    std::vector<std::pair<Symbol*, TypeId>> ex = snapshotSymbolFlags();
    for (size_t i = 0; i < entry.size(); ++i) {
        const bool exUninit = (ex[i].second & kUninitFlag) != 0; // тело могло оставить неинициализированной (сброс x = _)
        bool uninit = exUninit;
        if (!isDoWhile && (entry[i].second & kUninitFlag) != 0) {
            uninit = true; // while может выполниться 0 раз → entry-неинициализированная остаётся такой
        }
        if (uninit) {
            entry[i].first->type = setFlag(entry[i].first->type, SymbolFlag::Uninit);
        } else {
            entry[i].first->type = clearFlag(entry[i].first->type, SymbolFlag::Uninit);
        }
    }
}
// Обработка по kind (объявления, типы, Ident, ContextMacro); полный обход детей - в
// analyzeNode через analyzeChildren, поэтому здесь рекурсия в детей не нужна.
void NameResolutionPass::handleNode(AstNodePtr& self) {
    // `$^` - псевдопеременная «результат последней операции» (read-only). Простой случай (после
    // предыдущего оператора-выражения или декларации) переписан ПРЕ-семантически (captureLastResult),
    // а неподдержанные обращения проход сообщил и заменил на ErrorExpr-заглушку - сюда они не доходят.
    // Этот guard - ТОЛЬКО страховка для листа `$^`, который по какой-то причине проход не покрыл
    // (напр. `$^` в составе составного sink) - чтобы не было тихого битого кодогена. Запись в `$^`
    // исключена грамматикой. Ограничено листами Ident/ArgNode: только у них text() в HasText.
    if ((self->kind() == ParserToken::Kind::Ident || self->kind() == ParserToken::Kind::ArgNode) && self->text() == "$^") {
        m_actx.ctx().diag().report(Severity::Error, self->range(), "pseudo-variable '$^' (result of the last operation) is not implemented yet");
        return;
    }
    const ParserToken::Kind self_kind = self->kind();
    if (self_kind == ParserToken::Kind::VarDecl) {
        m_decl.analyzeVarDecl(*self->as<VarDecl>());
        return;
    }
    if (self_kind == ParserToken::Kind::TypeDecl) {
        m_decl.analyzeTypeDecl(*self->as<Binary>());
        return;
    }
    if (self_kind == ParserToken::Kind::ReturnStmt) {
        // Помечаем return ссылкой на определение функции (для пост-условий в кодогенерации:
        // узел самодостаточен, транспилятору не нужен текущий контекст функции).
        auto& js = *self->as<JumpStmt>();
        js.m_funcDecl = currentFuncDecl();
        // Именованное положительное прерывание `name ++ value ++` = return по имени функции:
        // метка обязана быть текущей функцией либо глобальной `::` (exit/проброс). Иначе ошибка.
        // Неименованное `++ value ++` - «положительное прерывание» (throw IntPlus), валидно всегда.
        if (js.m_label) {
            const std::string labelText = std::string(js.m_label->text());
            if (labelText != "::") {
                const std::string fn = interruptFuncName(js.m_funcDecl);
                if (fn.empty() || interruptLabelName(labelText) != fn) {
                    m_actx.ctx().diag().report(Severity::Error, js.m_label->range(), "named interrupt '{}' is not a return label of the current function '{}'",
                                               labelText, fn);
                }
            }
        }
        return;
    }
    if (self_kind == ParserToken::Kind::Ident) {
        // Квалификатор @:: foo уже раскрыт хук-ом ContextMacroExpander (в analyzeNode);
        // здесь только резолвим имя.
        lookupOrError(*self);
        return;
    }
    if (self_kind == ParserToken::Kind::EmbedExpr) {
        // Опция -Wembed (default Warning): предупреждение за сам факт использования C++-вставки
        // {% ... %} независимо от имён внутри. `-Wembed=ignore` подавляет вывод. Вызывается
        // ровно один раз на узел (обход семантики), в отличие от кодогенерации (рекурсия через emitExpr).
        const auto& embed = static_cast<const AstNodeAttr&>(*self);
        m_actx.ctx().report(embed.range(), semantic::DiagId::Embed, "C++ code embedding {{% ... %}} is used");
        // C++-вставка ({% ... %}): trust-имена, на которые ссылается вставка ($name/@name),
        // проверяются на доступность в таблице символов; отсутствующие - предупреждение.
        for (const auto& nm : utils::extract_embed_names(embed.text())) {
            const Symbol* found = m_actx.symbols().resolve(nm);
            // Квалифицированное имя (ns::x): таблица - плоский стек скоупов, поэтому полное
            // имя не находится; проверяем по последнему сегменту (грубая проверка доступности).
            if (!found) {
                const auto pos = nm.rfind("::");
                if (pos != std::string::npos) {
                    found = m_actx.symbols().resolve(nm.substr(pos + 2));
                }
            }
            if (!found) {
                m_actx.ctx().diag().report(Severity::Warning, embed.range(), "embed references name '{}' not declared in trust code", nm);
            }
        }
        return;
    }
    if (self_kind == ParserToken::Kind::AppendStmt) {
        // Учёт `[]=` в статическом размере словаря: `d []= v` увеличивает известный размер
        // (dims) целевого словаря - чтобы статическая проверка `d.N` далее по тексту видела
        // выросший размер (после двух append размер 3 → 5). LHS - простой Ident (вложенный
        // отклонён в typeExpr); dims >= 0 означает «словарь с известным размером».
        //
        // Spread-merge `d []= ... dict` (RHS - Ellipsis): добавляются ВСЕ элементы словаря-
        // операнда, поэтому dims растёт на число элементов операнда, а типы полей переносятся
        // в dictFieldTypes цели. Без `...` - одиночный элемент (прежнее поведение: dims += 1,
        // типы полей не регистрируются - сохранение «Any» для добавленных позиционных).
        auto& append = *self->as<Binary>();
        if (append.m_left && append.m_left->kind() == ParserToken::Kind::Ident) {
            if (Symbol* s = resolveSimple(append.m_left.get(), append.m_left->text())) {
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
                            s->dictFieldTypes.emplace_back(fname, val ? m_typer.dictElementType(val) : INVALID_TYPE_ID);
                        }
                    } else if (operand && operand->kind() == ParserToken::Kind::Ident) {
                        // Словарь-переменная: переносим её известный размер и типы полей.
                        // Простое имя могло быть объявлено как локальная `$x` (опция -Wsigil).
                        if (Symbol* src = resolveSimple(nullptr, operand->text())) {
                            if (isDictTypeId(m_actx.ctx().types(), src->type)) {
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
                s->dictFieldTypes.emplace_back("", rhs ? m_typer.dictElementType(rhs) : INVALID_TYPE_ID);
            }
        }
        return;
    }
}

// True, если текущий скоуп - локальный (в стеке скоупов есть FuncDecl). Единый предикат для
// sigil-нормализации имён (analyzeVarDecl / declareDestructureTarget).
bool NameResolutionPass::isInLocalScope() const {
    bool isLocal = false;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            isLocal = true;
        }
    });
    return isLocal;
}

// True, если текущий узел находится ВНУТРИ тела цикла (в стеке скоупов есть скоуп, созданный
// WhileStmt/DoWhileStmt). Циклы создают скоуп на время обхода тела (см. analyzeNode).
bool NameResolutionPass::isInLoop() const {
    bool inLoop = false;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (s.creator && (s.creator->kind() == ParserToken::Kind::WhileStmt || s.creator->kind() == ParserToken::Kind::DoWhileStmt)) {
            inLoop = true;
        }
    });
    return inLoop;
}

// -- Встроенный маркер `@__CHECK_AREA__` -- -----------------------------------------

// -- Trust-условия (пред/пост/утверждение) -------------------------

const FuncDecl* NameResolutionPass::currentFuncDecl() const {
    const FuncDecl* result = nullptr;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (!result && s.creator && s.creator->kind() == ParserToken::Kind::FuncDecl) {
            result = static_cast<const FuncDecl*>(s.creator);
        }
    });
    return result; // forEachScope идёт от внутреннего скоупа - первый найденный и есть ближайший
}

PropertyKind NameResolutionPass::currentTrustKind() const {
    PropertyKind result = PropertyKind::kUnknown;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (result == PropertyKind::kUnknown && s.creator && s.creator->kind() == ParserToken::Kind::TrustContract) {
            result = static_cast<const TrustContract&>(*s.creator).kind;
        }
    });
    return result;
}

const AstNodeBase* NameResolutionPass::trustTypeDeclOf(TypeId typeId) const {
    if (typeId == INVALID_TYPE_ID || !typeIsTrusted(typeId)) {
        return nullptr;
    }
    const TypeRegistry& reg = m_actx.ctx().types();
    const TypeDescriptor* d = reg.lookup(clearSymbolFlags(typeId));
    if (!d) {
        return nullptr;
    }
    const Symbol* s = m_actx.symbols().resolve(d->name);
    if (!s || !s->decl || s->decl->kind() != ParserToken::Kind::TypeDecl) {
        return nullptr;
    }
    return s->decl;
}

// -- Разрешение имён --

const Symbol* NameResolutionPass::lookupOrError(AstNodeBase& node) {
    const std::string name(node.text());
    // `_` (None/underscore) - специальное значение, а не имя переменной: используется как
    // none-значение и как discard в swap-move `var :=: _` → std::move(var). Не резолвим.
    if (name == "_") {
        return nullptr;
    }
    const Symbol* sym = resolveSimple(&node, name);
    // Trust-контракты: в пред-условии (kind=Pre) имя самой функции (возвращаемое значение)
    // ЗАПРЕЩЕНО; в пост-условии (kind=Post) - легально (это возврат). Контекст и текущая функция
    // выводятся из стека скоупов (creator), а не из отдельного состояния (см. currentTrustKind).
    if (sym && currentTrustKind() == PropertyKind::Pre) {
        if (const FuncDecl* f = currentFuncDecl(); f && sym->decl == f) {
            m_actx.ctx().diag().report(Severity::Error, node.range(),
                                       "cannot use function '{}' (return value) in a precondition; use it only in a postcondition", name);
        }
    }
    // Зарегистрированные runtime-символы (например %trust::trust__abort__) и интринсики языка
    // (например trust::intrinsic_assert, без native-префикса '%') - известные имена (публичный
    // runtime-заголовок / распознаваемые компилятором); «undefined name» для них не выдаём
    // (транспилер эмитит их разворачиванием на этапе генерации, см. CppTranspiler::emitIntrinsic).
    const bool isRuntime = !sym && (m_actx.isRegisteredRuntimeSymbol(name) || m_actx.isRegisteredIntrinsic(name));
    TRUST_DEBUG("resolve", "name '{}' -> {}", name, sym ? "resolved" : (isRuntime ? "runtime" : "undefined"));
    for (auto& hook : m_hooks) {
        hook->onResolve(node, sym);
    }
    if (!sym && !isRuntime) {
        m_actx.ctx().diag().report(Severity::Error, node.range(), "undefined name '{}'", name);
    }
    return sym;
}

// Единый алгоритм разрешения простого имени с правилами вывода сигилов.
// Порядок для bare-имени `x`: `$x` (локальная) → `x` (глобал/параметр) → `%x` (нативная функция).
// Для `$`-имени `$x`: сначала `$x`; если нет - bare `x` (параметр/локальная без сигила): `n` и
// `$n` - одно локальное имя. При попадании на `$x`/`%x` текст узла-ссылки (node) нормализуется
// на эту форму, чтобы манглинг (name_to_cpp срезает `$`/`%` → c_x / x) совпал с объявлением.
// Квалифицированные/сигилные/нативные имена, найденные напрямую, резолвятся как есть.
// node может быть nullptr (тогда текст не меняется).
Symbol* NameResolutionPass::resolveSimple(AstNodeBase* node, std::string_view name) {
    const bool simple = isSimpleVarName(name);
    // bare x: сначала локальная $x (при попадании - нормализуем текст узла на $x).
    if (simple) {
        const std::string sigil_name = "$" + std::string(name);
        if (Symbol* s = m_actx.symbols().resolveMutable(sigil_name)) {
            if (node && node->kind() == ParserToken::Kind::Ident) {
                static_cast<HasText&>(*node).set_text(sigil_name);
            }
            return s;
        }
    }
    // как есть (глобальная/параметр/квалифицированная/сигилная).
    if (Symbol* s = m_actx.symbols().resolveMutable(name)) {
        return s;
    }
    // Правила вывода сигилов:
    //  - bare x → нативная функция %x (напр. %fib вызывается как fib);
    //  - $x → локальная/параметр без сигила (n и $n - одно локальное имя).
    if (simple) {
        const std::string native_name = "%" + std::string(name);
        if (Symbol* s = m_actx.symbols().resolveMutable(native_name)) {
            if (node && node->kind() == ParserToken::Kind::Ident) {
                static_cast<HasText&>(*node).set_text(native_name);
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
        SymbolTable& syms = m_actx.symbols();
        std::vector<std::string> prefixes;
        syms.forEachScope([&](const SymbolTable::Scope& s) { prefixes.insert(prefixes.end(), s.importedNamespaces.begin(), s.importedNamespaces.end()); });
        for (const auto& prefix : prefixes) {
            const std::string q = prefix + "::" + std::string(name);
            if (Symbol* found = syms.resolveMutable(q)) {
                return found;
            }
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
    auto& scope = m_actx.symbols().current();
    const auto addPath = [&](const std::string& path) {
        if (!path.empty()) {
            scope.importNamespace(path);
        }
    };
    const AstNodeBase& rhs = *b.m_right;
    if (rhs.kind() == ParserToken::Kind::CallExpr) {
        const auto& call = static_cast<const CallExpr&>(rhs);
        if (call.m_callee) {
            addPath(std::string(call.m_callee->text()));
        }
        if (call.m_args) {
            for (const auto& arg : *call.m_args) {
                if (arg) {
                    addPath(std::string(arg->text()));
                }
            }
        }
    } else {
        addPath(std::string(rhs.text()));
    }
    return true;
}
} // namespace trust
