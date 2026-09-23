// src/semantic/name_resolution_nodes.cpp
// Группы узлов обхода NameResolutionPass, вынесенные из analyzeNode как отдельные зоны
// ответственности: скоуп-контейнеры, with, объявления функций/лямбд, catch-ветки,
// термины trust-контрактов и пост-обработка (типизация + синтетические временные).
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

// Скоуп-контейнеры (модуль/блок/ScopeBlock/TryCatchStmt): вложенный скоуп на тело.
void NameResolutionPass::analyzeScopeContainer(AstNodePtr& self) {
        enterScope(*self);
        analyzeChildren(self);
        exitScope();
}

// Оператор with(...){...}else{...} (RAII-менеджер контекста): биндинги, тело, else.
void NameResolutionPass::analyzeWithStmt(AstNodePtr& self) {
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
        m_decl.analyzeFuncDecl(f);
        enterScope(f);
        m_decl.declareFuncParams(f);
        // Trust-условия (пред/пост): резолв имён в скоупе функции (параметры видны; имя
        // функции = возврат в пост-условии, запрещено в пред-условии - см. lookupOrError).
        m_trust.processTrustConditions(f.m_trust, f);
        analyzeChildren(self);
        exitScope();
}

// Ветка catch(...) : отдельный вложенный скоуп + связывание переменной.
void NameResolutionPass::analyzeCatchBlockNode(AstNodePtr& self) {
        // Каждая ветка catch - отдельный вложенный скоуп (как в C++). Связанная переменная
        // `catch(e:Type)` (VarDecl в m_binding) регистрируется здесь как ЛОКАЛЬНАЯ и видна
        // только в теле ветки; `catch(:Type)`/`catch(_)`/`catch(...)` не связывают имя.
        auto& cb = *self->as<CatchBlock>();
        enterScope(*self);
        if (cb.m_binding && cb.m_binding->kind() == ParserToken::Kind::VarDecl) {
            auto& vd = static_cast<VarDecl&>(*cb.m_binding);
            Symbol sym;
            sym.name = std::string(vd.text());
            if (vd.m_type) {
                const auto tid = m_actx.resolveTypeRef(*vd.m_type);
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
            const TypeId src = m_actx.exprType(*vd.m_initializer);
            const TypeId voidId = m_actx.ctx().types().getType(type::Void);
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
            const TypeId vt = m_actx.exprType(*match.m_value);
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
    // `__trust_res_N := <m_value>;` (тип из exprType → inferredType), m_value заменяется ссылкой
    // на неё (Ident __trust_res_N). Транспилятор эмитит её как обычный VarDecl и читает имя.
    // Создаётся только для ИМЕНОВАННОГО return (m_label) из функции с пост-условиями — точно по
    // логике visit_ReturnStmt (void/неименованный `++ _ ++` не трогаем).
    if (self->kind() == ParserToken::Kind::ReturnStmt) {
        auto& js = static_cast<JumpStmt&>(*self);
        if (js.m_label && js.m_funcDecl && js.m_value) {
            const bool hasPost = js.m_funcDecl->hasTrustProperty(PropertyKind::Post);
            if (hasPost) {
                const TypeId vt = m_actx.exprType(*js.m_value);
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

} // namespace trust
