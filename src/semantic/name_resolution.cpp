// Generated: src/semantic/name_resolution.cpp (driver)
#include "semantic/name_resolution.hpp"
#include "semantic/analysis_common.hpp"
#include "semantic/format_check.hpp"
#include "semantic/solver.hpp"
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
            opts.add(semantic::DiagId::UnusedVariable);
            opts.add(semantic::DiagId::UnusedParameter);
            opts.add(semantic::DiagId::Embed);
            opts.add(semantic::DiagId::NoSigil);
            opts.add(semantic::DiagId::Format);
            opts.add(semantic::DiagId::WidenAny);
            opts.add_flag(semantic::FlagKind::Lint);
            opts.add_flag(semantic::FlagKind::Effect);
            opts.add_flag(semantic::FlagKind::Trust);
            opts.add_flag(semantic::FlagKind::Extended);
            opts.add_flag(semantic::FlagKind::Symbols);
            // `-Wsolver=ignore|warning|error` - severity-диагностика «присутствуют trust-условия»
            // (default warning; применяется в processTrustConditions; глушится -Wsolver=ignore).
            opts.add(semantic::DiagId::Solver);
            // `--solver-mode=assert|export|calculate` - ПОВЕДЕНЧЕСКИЙ флаг (рантайм-проверки /
            // генерация SMT-LIB 2 / генерация+запуск Z3). Значение по умолчанию НЕ задано.
            opts.add_flag(semantic::FlagKind::SolverMode);
            // Валидатор допустимых значений `--solver-mode` (no silent fallback).
            opts.set_flag_validator(semantic::FlagKind::SolverMode, [](std::string_view v) { return semantic::parseSolverMode(v).has_value(); });
            // `-Wsolver-loop` - диагностика циклов без инварианта (значение по умолчанию warning).
            opts.add_flag(semantic::FlagKind::SolverLoop);
            opts.set_flag_value(semantic::FlagKind::SolverLoop, "warning");
            opts.set_flag_validator(semantic::FlagKind::SolverLoop, [](std::string_view v) { return semantic::parseSolverLoopMode(v).has_value(); });
            // `-fsolver-loop-unroll` / `-fno-solver-loop-unroll` - поведенческий флаг
            // (bounded-unrolling, по умолчанию выкл; конвенция gcc/clang -f, не -W).
            opts.add_flag(semantic::FlagKind::SolverLoopUnroll);
            // `--keywords` / `Keywords:` в .trust-format - список макросов, допустимых без '@'
            // (суппресс -Wsigil; читается и syntax через opts().flagValueByName("keywords")).
            opts.add_flag(semantic::FlagKind::Keywords);
        });
    }
};
const SemanticDiagnosticsRegistrar kSemanticDiagnostics;
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
    m_actx.symbols().pop();
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
    case ParserToken::Kind::WhileStmt:
    case ParserToken::Kind::DoWhileStmt:
        enterScope(*self);
        analyzeChildren(self);
        exitScope();
        return;
    case ParserToken::Kind::FuncDecl: {
        // Имя функции регистрируется в ТЕКУЩЕМ (внешнем) скоупе, затем открывается
        // скоуп функции, в котором видны параметры и тело.
        auto& f = static_cast<FuncDecl&>(*self);
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
                const Symbol* declared = resolveSimpleRead(bname);
                if (!declared) {
                    m_actx.ctx().diag().report(Severity::Error, te.m_args[0]->range(), "quantifier bound variable '{}' must be a variable declared earlier",
                                               bname);
                    return;
                }
                if (typeIsInferred(declared->type)) {
                    m_actx.ctx().diag().report(Severity::Error, te.m_args[0]->range(),
                                               "quantifier bound variable '{}' has an inferred type; declare it with an explicit type", bname);
                    return;
                }
                const TypeId bt = clearInferred(declared->type);
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
    default:
        break;
    }

    // Обработка узла по kind (если он не был заменён хук-ом) + полный обход детей.
    if (!consumed) {
        handleNode(self);
    }
    analyzeChildren(self);

    // Пост-порядковая типизация выражения/объявления (после того как дети уже
    // проанализированы и типизированы): вычисляет тип результата выражения и
    // расширяет выводимый (inferred) тип целевой переменной по истории присвоений.
    m_typer.typeExpr(self.get());
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

// Обработка по kind (объявления, типы, Ident, ContextMacro); полный обход детей - в
// analyzeNode через analyzeChildren, поэтому здесь рекурсия в детей не нужна.
void NameResolutionPass::handleNode(AstNodePtr& self) {
    switch (self->kind()) {
    case ParserToken::Kind::VarDecl:
        m_decl.analyzeVarDecl(static_cast<VarDecl&>(*self));
        break;
    case ParserToken::Kind::TypeDecl:
        m_decl.analyzeTypeDecl(static_cast<Binary&>(*self));
        break;
    case ParserToken::Kind::ReturnStmt:
        // Помечаем return ссылкой на определение функции (для пост-условий в кодогенерации:
        // узел самодостаточен, транспилятору не нужен текущий контекст функции).
        static_cast<JumpStmt&>(*self).m_funcDecl = currentFuncDecl();
        break;
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
                        if (const Symbol* src = resolveSimpleRead(operand->text())) {
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

// -- Применение ортогональных квалификаторов типа (const + вид ссылки) --
// Единый источник для переменных (analyzeVarDecl) и параметров (declareFuncParams).
TypeId NameResolutionPass::applyRefAttrs(TypeId base, const AstNodeAttr& node, MapperRange range) {
    if (base == INVALID_TYPE_ID) {
        return base;
    }
    const AttrPool& attrs = m_actx.ctx().attrs();
    // Константность ('^' → attr::ReadOnly): бит kConstFlag → `const T` в C++ (getCppTypeName).
    if (node.has_attr(attrs, attr::ReadOnly)) {
        base = withConst(base);
    }
    // Вид ссылки (@[reftype("ptr")]) - плоский enum RefType. Первая ссылка - fast-path бит,
    // вложенность - составной узел (единый источник: TypeRegistry::applyRefType).
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
                base = m_actx.ctx().types().applyRefType(base, *refkind);
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
    const TypeDescriptor* d = reg.lookup(clearInferred(clearConst(typeId)));
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
    return nullptr;
}

const Symbol* NameResolutionPass::resolveSimpleRead(std::string_view name) const {
    const bool simple = isSimpleVarName(name);
    if (simple) {
        const std::string sigil_name = "$" + std::string(name);
        if (const Symbol* s = m_actx.symbols().resolve(sigil_name)) {
            return s;
        }
    }
    if (const Symbol* s = m_actx.symbols().resolve(name)) {
        return s;
    }
    if (simple) {
        const std::string native_name = "%" + std::string(name);
        if (const Symbol* s = m_actx.symbols().resolve(native_name)) {
            return s;
        }
    } else if (!name.empty() && name.front() == '$') {
        if (const Symbol* s = m_actx.symbols().resolve(name.substr(1))) {
            return s;
        }
    }
    return nullptr;
}
} // namespace trust
