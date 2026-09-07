// Generated: src/semantic/name_resolution.cpp (driver)
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
#include "semantic/stack_check.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/type_inference.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/token.hpp"
#include "diag/diag.hpp"
#include "diag/options.hpp"
#include "diag/registry.hpp"
#include "semantic/diag.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "utils/operators.hpp"
#include "utils/strings.hpp"
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
    for (auto& hook : m_hooks) {
        hook->enterScope();
    }
}

void NameResolutionPass::exitScope() {
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
    switch (self->kind()) {
    case ParserToken::Kind::ModuleDecl:
    case ParserToken::Kind::sequence:
    case ParserToken::Kind::ScopeBlock:
    case ParserToken::Kind::TryCatchStmt:
        enterScope(*self);
        analyzeChildren(self);
        exitScope();
        return;
    case ParserToken::Kind::WhileStmt:
        analyzeLoopConstruct(self, /*isDoWhile=*/false);
        return;
    case ParserToken::Kind::DoWhileStmt:
        analyzeLoopConstruct(self, /*isDoWhile=*/true);
        return;
    case ParserToken::Kind::WithStmt: {
        // `with(a=f(), b=g()){...}else{...}` (RAII-менеджер контекста):
        //   - биндинги (VarDecl) регистрируются в скоупе оператора стандартным путём analyzeVarDecl
        //     (кейс VarDecl в handleNode): declareOrComplete → duplicate-ошибка при совпадении в том
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
        return;
    }
    case ParserToken::Kind::FuncDecl: {
        // Объявление нативного шаблона-ТИПА `<T> %std::vector() := ...;` - регистрирует
        // параметризованный тип (а НЕ функцию): никакого скоупа функции и тела нет.
        auto& f = static_cast<FuncDecl&>(*self);
        if (f.m_isNativeTemplateCtor) {
            m_decl.analyzeNativeTemplateDecl(f);
            return;
        }
        // Имя функции регистрируется в ТЕКУЩЕМ (внешнем) скоупе, затем открывается
        // скоуп функции, в котором видны параметры и тело.
        m_decl.analyzeFuncDecl(f);
        enterScope(f);
        m_decl.declareFuncParams(f);
        // Trust-условия (пред/пост): резолв имён в скоупе функции (параметры видны; имя
        // функции = возврат в пост-условии, запрещено в пред-условии - см. lookupOrError).
        m_trust.processTrustConditions(f.m_trust, f);
        analyzeChildren(self);
        exitScope();
        return;
    }
    case ParserToken::Kind::ClassDecl: {
        // Forward-объявление (нативного) класса - обрабатывается целиком в analyzeTypeDecl
        // (регистрирует тип + члены-интерфейс). Здесь как дочерний узел TypeDecl - no-op:
        // детей (членов) НЕ обходим, иначе они стали бы отдельными функциями/переменными
        // верхнего уровня, а не членами класса.
        return;
    }
    case ParserToken::Kind::CatchBlock: {
        // Каждая ветка catch - отдельный вложенный скоуп (как в C++). Связанная переменная
        // `catch(e:Type)` (VarDecl в m_binding) регистрируется здесь как ЛОКАЛЬНАЯ и видна
        // только в теле ветки; `catch(:Type)`/`catch(_)`/`catch(...)` не связывают имя.
        auto& cb = static_cast<CatchBlock&>(*self);
        enterScope(*self);
        if (cb.m_binding && cb.m_binding->kind() == ParserToken::Kind::VarDecl) {
            auto& vd = static_cast<VarDecl&>(*cb.m_binding);
            Symbol sym;
            sym.name = std::string(vd.text());
            if (vd.m_type) {
                const auto tid = m_actx.resolveType(*vd.m_type);
                if (tid.has_value()) {
                    sym.type = *tid;
                } else {
                    m_actx.ctx().diag().report(Severity::Error, vd.m_type->range(), "unknown catch type '{}'", vd.m_type->text());
                }
            }
            sym.decl = &vd;
            sym.storage = Storage::Local;
            m_actx.symbols().declare(sym);
        }
        analyzeChildren(self);
        exitScope();
        return;
    }
    case ParserToken::Kind::DestructureDecl:
        // Деструктуризация `item, dict := ... source;`: первый target объявляется локальной
        // std::any-переменной (первый элемент), источник мутируется pop_front. Цели не обходим
        // общим механизмом (это объявления, не ссылки).
        m_decl.analyzeDestructure(static_cast<DestructureDecl&>(*self));
        return;
    case ParserToken::Kind::DictLiteral:
        // Литерал словаря: анализируем значения элементов (имена-метки не резолвим).
        m_typer.analyzeDictLiteral(static_cast<Sequence&>(*self));
        return;
    case ParserToken::Kind::ArrayInit:
        // Литерал массива `[1,2,3,]` / `[1,2,3,]:Int32`: анализ элементов + вывод типа
        // элемента + интернирование структурного Array<Elem> (см. analyzeArrayInit).
        m_typer.analyzeArrayInit(static_cast<DictLiteralNode&>(*self));
        return;
    case ParserToken::Kind::RangeExpr:
        // Литерал диапазона: резолв/типизация операндов + элементный тип (join).
        m_typer.analyzeRangeExpr(static_cast<RangeExpr&>(*self));
        return;
    case ParserToken::Kind::CheckAreaStmt:
        // Встроенный маркер `@__CHECK_AREA__(area[, behavior][, attrs...])` из тела макроса.
        // Проверяет текущую область (из единого скоуп-стека) и УДАЛЯЕТ маркер (кода не даёт).
        analyzeCheckAreaStmt(self);
        return;
    case ParserToken::Kind::TrustContract:
        // Автономный trust-контракт `@{ [kind:] expr @};` в последовательности (не привязан
        // к объявлению). Обработка по -Wsolver/--solver-mode (см. processTrustConditions).
        m_trust.processTrustConditions({self}, *self);
        return;
    case ParserToken::Kind::TrustElem: {
        // Термин решателя `@( term, args... @)` внутри контракта: резолв имён аргументов.
        // Для кванторов (forall/exists) первый аргумент - переменная-связка: она обязана быть
        // переменной, ОБЪЯВЛЕННОЙ РАНЕЕ (разрешение имён). Тип связки берётся из её объявления,
        // НЕ выводится; не объявлена или тип выведен автоматически (kInferredFlag) - ошибка.
        // Сам узел-связка не анализируется (это связка, не ссылка).
        auto& te = static_cast<TrustElem&>(*self);
        if (te.kind == Z3TermKind::Forall || te.kind == Z3TermKind::Exists) {
            if (!te.m_args.empty() && te.m_args[0]) {
                const std::string bname(te.m_args[0]->text());
                const Symbol* declared = resolveSimple(nullptr, bname);
                if (!declared) {
                    m_actx.ctx().diag().report(Severity::Error, te.m_args[0]->range(), "quantifier bound variable '{}' must be a variable declared earlier",
                                               bname);
                    return;
                }
                if (testFlag(declared->type, SymbolFlag::Inferred)) {
                    m_actx.ctx().diag().report(Severity::Error, te.m_args[0]->range(),
                                               "quantifier bound variable '{}' has an inferred type; declare it with an explicit type", bname);
                    return;
                }
                const TypeId bt = clearFlag(declared->type, SymbolFlag::Inferred);
                te.m_boundVarType = bt; // результат разрешения имён: тип из объявления (переживает таблицу)
                enterScope(*self);
                Symbol sym;
                sym.name = bname; // связка как в исходнике (без сигила)
                sym.type = bt;
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
        return;
    }
    case ParserToken::Kind::MemberAccess:
    case ParserToken::Kind::ArrayAccess:
        // Доступ к элементу словаря: объект анализируется, поле-имя не резолвится,
        // статический индекс проверяется по размерности объекта.
        m_access.analyzeAccess(static_cast<Binary&>(*self));
        return;
    case ParserToken::Kind::AssignOp:
        // Оператор `... = X` (using): регистрирует область/области имён RHS для поиска
        // имён, RHS НЕ резолвится как значение. Заменяется пустым узлом (compile-time
        // директива, кода не генерирует). Иначе - обычное присваивание.
        if (handleUsingImport(*self)) {
            self = std::make_shared<Sequence>();
            return;
        }
        break;
    default:
        break;
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

    // Пост-порядковая типизация выражения/объявления (после того как дети уже
    // проанализированы и типизированы): вычисляет тип результата выражения и
    // расширяет выводимый (inferred) тип целевой переменной по истории присвоений.
    m_typer.typeExpr(self.get());

    // `$^` (простой случай): синтетическая временная `__trust_last_N` из источника, который НЕ даёт
    // значения (void-функция/unit) ⇒ у `$^` нечего захватывать. Пре-семантический проход не знает тип
    // вызова, поэтому здесь (тип известен) выдаём ТОЧЕЧНУЮ диагностику вместо общей "unable to generate
    // C++ type 'Any'". Тип источника - Void/None либо не выводится (INVALID).
    if (self->kind() == ParserToken::Kind::VarDecl) {
        VarDecl& vd = static_cast<VarDecl&>(*self);
        if (vd.m_lastResultTemp && vd.m_initializer) {
            const TypeId src = m_actx.resolvedType(*vd.m_initializer);
            const TypeId voidId = m_actx.ctx().types().getType("Void");
            if (src == INVALID_TYPE_ID || (voidId != INVALID_TYPE_ID && src == voidId)) {
                m_actx.ctx().diag().report(Severity::Error, self->range(),
                                           "pseudo-variable '$^' (result of the last operation): the "
                                           "preceding statement produces no value to capture (void)");
            }
        }
    }

    // MatchStmt: scrutinee вычисляется один раз во временную const-переменную. Временную создаёт
    // СЕМАНТИКА (инвариант «временные — уровень анализатора»): синтезируется const VarDecl
    // `_matchN := <m_value>;` (тип из resolvedType → VarDecl::inferredType), m_value заменяется
    // ссылкой на неё (Ident _matchN). Транспилятор эмитит её как обычный VarDecl и читает тип для
    // выбора switch/enum/if. (Создаёт семантика, а не lowering, т.к. только у неё есть тип значения.)
    if (self->kind() == ParserToken::Kind::MatchingStmt) {
        auto& match = static_cast<MatchStmt&>(*self);
        if (match.m_value) {
            const TypeId vt = m_actx.resolvedType(*match.m_value);
            const std::string tmpName = "_match" + std::to_string(m_actx.nextMatchTempId());
            auto tmp = std::make_shared<VarDecl>(tmpName, nullptr, std::move(match.m_value));
            if (vt != INVALID_TYPE_ID) {
                tmp->inferredType = clearFlag(vt, SymbolFlag::Inferred);
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
    // `__trust_res_N := <m_value>;` (тип из resolvedType → inferredType), m_value заменяется ссылкой
    // на неё (Ident __trust_res_N). Транспилятор эмитит её как обычный VarDecl и читает имя.
    // Создаётся только для ИМЕНОВАННОГО return (m_label) из функции с пост-условиями — точно по
    // логике visit_ReturnStmt (void/неименованный `++ _ ++` не трогаем).
    if (self->kind() == ParserToken::Kind::ReturnStmt) {
        auto& js = static_cast<JumpStmt&>(*self);
        if (js.m_label && js.m_funcDecl && js.m_value) {
            bool hasPost = false;
            for (const auto& t : js.m_funcDecl->m_trust) {
                const auto* tc = dynamic_cast<const TrustContract*>(t.get());
                if (t && tc && tc->kind == PropertyKind::Post) {
                    hasPost = true;
                    break;
                }
            }
            if (hasPost) {
                const TypeId vt = m_actx.resolvedType(*js.m_value);
                const std::string tmpName = "__trust_res_" + std::to_string(m_actx.nextResultTempId());
                auto tmp = std::make_shared<VarDecl>(tmpName, nullptr, std::move(js.m_value));
                if (vt != INVALID_TYPE_ID) {
                    tmp->inferredType = clearFlag(vt, SymbolFlag::Inferred);
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
    switch (self->kind()) {
    case ParserToken::Kind::VarDecl:
        m_decl.analyzeVarDecl(static_cast<VarDecl&>(*self));
        break;
    case ParserToken::Kind::TypeDecl:
        m_decl.analyzeTypeDecl(static_cast<Binary&>(*self));
        break;
    case ParserToken::Kind::ReturnStmt: {
        // Помечаем return ссылкой на определение функции (для пост-условий в кодогенерации:
        // узел самодостаточен, транспилятору не нужен текущий контекст функции).
        auto& js = static_cast<JumpStmt&>(*self);
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
        break;
    }
    case ParserToken::Kind::Ident:
        // Квалификатор @:: foo уже раскрыт хук-ом ContextMacroExpander (в analyzeNode);
        // здесь только резолвим имя.
        lookupOrError(*self);
        break;
    case ParserToken::Kind::EmbedExpr: {
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
        break;
    }
    case ParserToken::Kind::AppendStmt: {
        // Учёт `[]=` в статическом размере словаря: `d []= v` увеличивает известный размер
        // (dims) целевого словаря - чтобы статическая проверка `d.N` далее по тексту видела
        // выросший размер (после двух append размер 3 → 5). LHS - простой Ident (вложенный
        // отклонён в typeExpr); dims >= 0 означает «словарь с известным размером».
        //
        // Spread-merge `d []= ... dict` (RHS - Ellipsis): добавляются ВСЕ элементы словаря-
        // операнда, поэтому dims растёт на число элементов операнда, а типы полей переносятся
        // в dictFieldTypes цели. Без `...` - одиночный элемент (прежнее поведение: dims += 1,
        // типы полей не регистрируются - сохранение «Any» для добавленных позиционных).
        auto& append = static_cast<Binary&>(*self);
        if (append.m_left && append.m_left->kind() == ParserToken::Kind::Ident) {
            if (Symbol* s = resolveSimple(append.m_left.get(), append.m_left->text())) {
                if (s->dims < 0) {
                    break; // статический размер цели неизвестен - отслеживать нечего
                }
                const AstNodeBase* rhs = append.m_right.get();
                if (rhs && rhs->kind() == ParserToken::Kind::Ellipsis) {
                    const auto& ell = static_cast<const Sequence&>(*rhs);
                    const AstNodeBase* operand = ell.m_body.empty() ? nullptr : ell.m_body[0].get();
                    if (operand && operand->kind() == ParserToken::Kind::DictLiteral) {
                        // Компиляционно известный словарь-литерал: каждый элемент - новый элемент.
                        const auto& dl = static_cast<const Sequence&>(*operand);
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
                    break;
                }
                // Одиночный элемент (не spread): размер +1 и регистрация типа поля по позиции,
                // чтобы dictFieldTypes оставался выровнен по dims (инвариант: число записей
                // dictFieldTypes == известный размер). Для литерала тип выводится, для
                // переменной/выражения (до анализа) - Any (INVALID).
                s->dims += 1;
                s->dictFieldTypes.emplace_back("", rhs ? m_typer.dictElementType(rhs) : INVALID_TYPE_ID);
            }
        }
        break;
    }
    default:
        break; // прочие kinds обрабатываются только обходом детей
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
// Проверка области применения макроса: маркер встречается в теле макроса на сайте раскрытия.
// Текущая область выводится из ЕДИНОГО скоуп-стека (создатели скоупов) - без отдельного
// параллельного стека областей. Маркер не генерирует код и УДАЛЯЕТСЯ после проверки.
void NameResolutionPass::analyzeCheckAreaStmt(AstNodePtr& self) {
    if (!self || self->kind() != ParserToken::Kind::CheckAreaStmt) {
        self = nullptr;
        return;
    }
    auto st = std::static_pointer_cast<CheckAreaStmt>(self);
    const MapperRange rng = st->range();
    if (!st->area.has_value()) { // область не задана - нечего проверять
        self = nullptr;
        return;
    }
    const AreaKind required = *st->area;

    // Текущие области из ЕДИНОГО скоуп-стека (creator-узлы, от внутреннего к глобальному).
    std::vector<const AstNodeBase*> creators;
    m_actx.symbols().forEachScope([&](const SymbolTable::Scope& s) {
        if (s.creator) {
            creators.push_back(s.creator);
        }
    });
    bool hasClass = false;
    for (const AstNodeBase* n : creators) {
        if (n->kind() == ParserToken::Kind::ClassDecl) {
            hasClass = true;
        }
    }
    std::vector<AreaKind> cur;
    for (const AstNodeBase* n : creators) {
        switch (n->kind()) {
        case ParserToken::Kind::ModuleDecl:
            cur.push_back(AreaKind::Module);
            break;
        case ParserToken::Kind::ScopeBlock:
        case ParserToken::Kind::sequence:
            cur.push_back(AreaKind::Block);
            break;
        case ParserToken::Kind::FuncDecl:
            cur.push_back(AreaKind::Function);
            if (hasClass) {
                cur.push_back(AreaKind::Method);
            }
            break;
        case ParserToken::Kind::ClassDecl:
            cur.push_back(AreaKind::Class);
            break;
        case ParserToken::Kind::WhileStmt:
            cur.push_back(AreaKind::While);
            cur.push_back(AreaKind::Loop);
            break;
        case ParserToken::Kind::DoWhileStmt:
            cur.push_back(AreaKind::DoWhile);
            cur.push_back(AreaKind::Loop);
            break;
        case ParserToken::Kind::WithStmt:
            cur.push_back(AreaKind::With);
            break;
        case ParserToken::Kind::TryCatchStmt:
            cur.push_back(AreaKind::Try);
            break;
        case ParserToken::Kind::CatchBlock:
            cur.push_back(AreaKind::Catch);
            break;
        case ParserToken::Kind::IfStmt:
            // Whole-if: создатель IfStmt-скоупа означает «внутри if» (then/elseif/else - не
            // различаются, ветки отдельных скоупов не открывают). Отдельных elseif/else НЕ
            // заявляем (см. TRUST_CHECK_AREAS), пока не реализована per-branch-детекция.
            cur.push_back(AreaKind::If);
            break;
        case ParserToken::Kind::MatchingStmt:
            // Whole-match: создатель MatchingStmt-скоупа = «внутри match» (любая ветка/default).
            cur.push_back(AreaKind::Match);
            break;
        default:
            break;
        }
    }

    bool inside = false;
    for (const auto& c : cur) {
        if (c == required) {
            inside = true;
            break;
        }
    }

    // Атрибуты (AttrId) на ближайшем creator, несущем атрибуты.
    bool attrsOk = true;
    const AstNodeBase* areaNode = nullptr;
    for (const AstNodeBase* n : creators) {
        if (n->as_attr()) {
            areaNode = n;
            break;
        }
    }
    if (!st->attrs().empty()) {
        if (!areaNode) {
            attrsOk = false;
        } else {
            const AstNodeAttr* an = areaNode->as_attr();
            for (const AttrId id : st->attrs()) { // требуемые атрибуты лежат в attrs() самого маркера
                if (!an->has_attr(id)) {
                    attrsOk = false;
                    break;
                }
            }
        }
    }

    if (inside && attrsOk) {
        self = nullptr; // ограничение выполнено - маркер удаляем (кода не даёт)
        return;
    }

    // Severity: явный override (behavior) -> default -> -W-опция (nullopt = ignore).
    std::optional<Severity> sevOpt = st->behavior.has_value() ? st->behavior : std::optional<Severity>(m_actx.ctx().opts().get(semantic::DiagId::CheckArea));
    self = nullptr; // маркер всегда удаляется (не генератор кода)

    if (!sevOpt.has_value() || *sevOpt == Severity::Ignore || *sevOpt == Severity::Remark || *sevOpt == Severity::Note) {
        return; // ignore/низкий severity - без диагностики
    }
    const Severity sev = *sevOpt;

    std::string curList;
    for (const auto& c : cur) {
        if (!curList.empty()) {
            curList += ", ";
        }
        curList.append(areaKindName(c));
    }
    if (curList.empty()) {
        curList = "module(top-level)";
    }
    if (!attrsOk) {
        m_actx.ctx().diag().report(sev, rng, "@__CHECK_AREA__: current area '{}' lacks required attribute(s)", curList);
    } else {
        m_actx.ctx().diag().report(sev, rng, "@__CHECK_AREA__: macro allowed only inside area '{}', but current area is '{}'", areaKindName(required), curList);
    }
}

// -- Атрибут @[matcher("fn")] на операторе match -------------------------------
// Переопределяет сравнение по значению (==/===>): вместо (tmp == pattern) в каждой ветке
// кодогенерация эмитит вызов fn(tmp, pattern). Семантика резолвит имя функции и проверяет,
// что это объявленная функция-предикат с сигнатурой `bool fn(T_value, T_pattern)`.
void NameResolutionPass::analyzeMatchMatcher(MatchStmt& match) {
    const AttrPool& pool = m_actx.ctx().attrs();
    const auto matcher_id = pool.lookup(attr::Matcher);
    if (!matcher_id.has_value() || !match.has_attr(*matcher_id)) {
        return; // атрибута нет - ничего не делаем
    }
    const MapperRange rng = match.range();
    const std::vector<std::string>* args = match.attr_args(*matcher_id);
    if (!args || args->empty() || (*args)[0].empty()) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(...)] expects one argument - the name of a predicate function 'bool fn(T_value, T_pattern)'");
        return;
    }
    const std::string name = (*args)[0];

    // Matcher переопределяет сравнение ПО ЗНАЧЕНИЮ; сопоставление по типу несовместимо.
    const bool typeMatch = (match.m_op == "~>" || match.m_op == "~~>" || match.m_op == "~~~>");
    if (typeMatch) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(\"{}\")] is not applicable to type-matching operator '{}'; matcher overrides value comparison", name, match.m_op);
        return;
    }
    // Нативный C++-символ (%...) - резолвить нельзя, сигнатуру не проверяем (кодоген эмитит как есть).
    if (!name.empty() && name.front() == '%') {
        return;
    }
    Symbol* s = resolveSimple(nullptr, name);
    if (!s || !s->decl || s->decl->kind() != ParserToken::Kind::FuncDecl) {
        m_actx.ctx().diag().report(Severity::Error, rng,
                                   "@[matcher(\"{}\")]: '{}' is not a declared function; a matcher predicate must be declared before this match", name, name);
        return;
    }
    TypeRegistry& reg = m_actx.ctx().types();
    const auto* fd = reg.getTypeDataAs<FunctionTypeData>(s->type);
    if (!fd) {
        return; // сигнатура неизвестна (обобщённая/шаблон) - кодоген эмитит вызов как есть
    }
    if (fd->paramTypes.size() != 2) {
        m_actx.ctx().diag().report(Severity::Error, rng, "@[matcher(\"{}\")]: predicate must take exactly 2 arguments (T_value, T_pattern), got {}", name,
                                   fd->paramTypes.size());
        return;
    }
    const TypeId rt = fd->returnType == INVALID_TYPE_ID ? INVALID_TYPE_ID : reg.getCanonicalTypeId(fd->returnType);
    const bool returnsBool = rt != INVALID_TYPE_ID && getGroup(getKindFromId(rt)) == Group::kLogical;
    if (!returnsBool) {
        m_actx.ctx().diag().report(Severity::Error, rng, "@[matcher(\"{}\")]: predicate must return bool (got '{}')", name,
                                   fd->returnType == INVALID_TYPE_ID ? "void" : std::string(reg.getFullTypeName(fd->returnType)));
    }
}

// -- Применение ортогональных квалификаторов типа (const + вид ссылки) --
// Единый источник для переменных (analyzeVarDecl) и параметров (declareFuncParams).
TypeId NameResolutionPass::applyRefAttrs(TypeId base, const AstNodeAttr& node, MapperRange range) {
    if (base == INVALID_TYPE_ID) {
        return base;
    }
    const AttrPool& attrs = m_actx.ctx().attrs();
    // Константность ('^' → attr::ReadOnly): бит kConstFlag → `const T` в C++ (getCppTypeName).
    if (node.has_attr(attrs, attr::ReadOnly)) {
        base = setFlag(base, SymbolFlag::Const);
    }
    // Вид ссылки (@[reftype("ptr")]) - плоский enum RefType. Первая ссылка - fast-path бит,
    // вложенность - составной узел (единый источник: TypeRegistry::applyRefType).
    // Вид может быть задан ЛИБО у типа (`x : &Int32` → тип уже несёт признак), ЛИБО у переменной
    // (`& x : Int32` → атрибут reftype на узле переменной). Если задан у обоих - они ОБЯЗАНЫ
    // совпадать (иначе - ошибка); если только у переменной - применяем к pointee.
    auto reftype_id = attrs.lookup(attr::Reftype);
    if (reftype_id.has_value() && node.has_attr(*reftype_id)) {
        const std::vector<std::string>* rargs = node.attr_args(*reftype_id);
        if (!rargs || rargs->empty()) {
            m_actx.ctx().diag().report(Severity::Error, range, "attribute 'reftype' requires a reference-kind parameter, e.g. @[reftype(\"ptr\")]");
        } else {
            auto refkind = refTypeFromString(rargs->front());
            if (!refkind) {
                m_actx.ctx().diag().report(Severity::Error, range, "unknown reference kind '{}'", rargs->front());
            } else {
                // Расширенная форма: @[reftype("shared"/"weak"[, <sync_policy>][, <timeout>])].
                // Второй аргумент - имя класса синхронизации доступа, зарегистрированного в реестре
                // (встроенная политика Group::kSyncPolicy или класс, помеченный атрибутом `sync`),
                // допустим ТОЛЬКО для видов shared/weak. Третий (опциональный) - пер-объектный
                // таймаут детектора взаимной блокировки (<ms|s|nano>), перекрывает глобальный.
                if (rargs->size() > 3) {
                    m_actx.ctx().diag().report(Severity::Error, range, "attribute 'reftype' accepts at most 3 arguments: kind[, sync_policy[, timeout]]");
                } else if (rargs->size() > 1 && *refkind != RefType::kShared && *refkind != RefType::kWeak) {
                    m_actx.ctx().diag().report(Severity::Error, range, "sync access policy is only valid for reference kinds 'shared'/'weak', got '{}'",
                                               refTypeName(*refkind));
                } else if (rargs->size() > 1) {
                    // Политика - имя типа в реестре (реальная регистрация, без строкового маппинга).
                    const TypeRegistry& reg = m_actx.ctx().types();
                    const auto pid = reg.findType(rargs->at(1));
                    if (!pid.has_value() || !reg.isSyncPolicyType(*pid)) {
                        m_actx.ctx().diag().report(
                            Severity::Error, range,
                            "unknown sync access policy type '{}' (expected a type registered in the TypeRegistry, e.g. SyncMutexPolicy)", rargs->at(1));
                    }
                }
                const RefType baseKind = getRefType(getKindFromId(base));
                if (baseKind != RefType::kValue && baseKind != *refkind) {
                    // У переменной и у типа указаны РАЗНЫЕ виды ссылок - диагностика.
                    m_actx.ctx().diag().report(Severity::Error, range, "reference kind mismatch: variable is '{}' but its type is '{}'", refTypeName(*refkind),
                                               refTypeName(baseKind));
                } else if (baseKind == RefType::kValue) {
                    // Тип не несёт признака (pointee-значение) - применяем вид переменной.
                    base = m_actx.ctx().types().applyRefType(base, *refkind);
                }
                // baseKind == *refkind: тип уже несёт тот же признак - не применяем повторно.
            }
        }
    }
    return base;
}

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
