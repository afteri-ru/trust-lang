// ЕДИНЫЙ источник kind-диспетчеризации: перечень kinds (`PARSER_TOKEN_KINDS`), индекс-слот,
// контекстная категория, полнота обработчиков и диспетчер exhaustive-потребителя.
// Индустриальный аналог: Clang `RecursiveASTVisitor` (типизированные обработчики + сгенерированный
// диспетчер) и `StmtDumper` (исчерпывающий switch + `llvm_unreachable`); Roslyn сгенерированный
// `Accept()`/`DefaultVisit`; Rust `visit_*`/`walk_*`. Добавление kind в `PARSER_TOKEN_KINDS` меняет
// перечень и требует категории; компилятор требует решения там, где полнота обязательна.
// ДВА вида потребителей (смешивать их - ошибка):
// 1. Exhaustive consumer (кодоген): обработчик на КАЖДЫЙ kind, полнота доказывается компилятором
//        static_assert(hasAllKindHandlers<CppTranspiler>());
//    Обработчики типизированы (`const <node_type>&`), приведение делает диспетчер `dispatchEmit`.
//    `dispatchEmit(..., expected)` проверяет `kindCategory` (statement/expression-контекст).
//    «Недостижимо здесь» - явный `faultUnexpectedKind`, «осознанно пропустить» - `skipNode`.
// 2. Partial pass (семантика, линты): фаза реагирует на часть kinds, остальные обходит.
//    Решение - ДАННЫЕ: ОДНА таблица обработчиков размером `kParserKindSlotCount`, заполненная
//    default'ом обхода и переопределённая только реальными действиями (эталон -
//    `NameResolutionPass::semaActions()`). Каждый слот таблицы заполнен; переопределения
//    адресуются через `kindIndex(kind)`, `END` - reject. Default-обход - осознанная политика
//    partial pass (как в Clang/Rust/Swift): компиляторная полнота обязательна только для
//    exhaustive-потребителей.

#pragma once

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "utils/error.hpp"

#include <cstddef>

namespace trust {

/// Контекстная категория kind - из ЕДИНСТВЕННОГО источника `PARSER_TOKEN_KINDS`.
/// Неизвестное значение enum - нарушение контракта, а не повод для default-категории.
[[nodiscard]] inline KindCategory kindCategory(ParserToken::Kind k) {
    switch (k) {
#define KIND_CATEGORY_ROW(name, node_type, category) \
    case ParserToken::Kind::name:                    \
        return KindCategory::category;
        PARSER_TOKEN_KINDS(KIND_CATEGORY_ROW)
#undef KIND_CATEGORY_ROW
    case ParserToken::Kind::END:
        return KindCategory::Any;
    }
    FAULT("kindCategory: invalid kind value {}", static_cast<int>(k));
}

/// Полнота контракта exhaustive consumer: для каждого kind у `C` есть вызываемый
/// `visit_<Kind>(const <node_type>&)`. Возвращает false (а не ошибку компиляции) -
/// поэтому проверяется через `static_assert` и тестируется негативно.
template <class C>
consteval bool hasAllKindHandlers() {
#define KIND_REQUIRE(name, node_type, category)               \
    requires(C& c, const node_type& n) { c.visit_##name(n); } \
    &&
    return true && PARSER_TOKEN_KINDS(KIND_REQUIRE) true;
#undef KIND_REQUIRE
}

/// Диспетчеризация exhaustive consumer: switch по kind БЕЗ default (полнота switch
/// гарантируется `-Wswitch-enum`, полнота обработчиков - `hasAllKindHandlers`).
/// `expected` - контекст кодогена: Any (оба), StmtOnly, ExprOnly. Категория `Any`
/// допустима в любом контексте; несовпадение - FAULT с именем kind.
/// `END` - не узел: FAULT.
template <class C>
void dispatchEmit(const AstNodeBase& node, C& self, KindCategory expected = KindCategory::Any) {
    if (expected != KindCategory::Any) {
        const KindCategory actual = kindCategory(node.kind());
        if (actual != KindCategory::Any && actual != expected) {
            FAULT("dispatchEmit: kind '{}' is not allowed in {} context", ParserToken::name(node.kind()),
                  expected == KindCategory::StmtOnly ? "statement" : "expression");
        }
    }
    switch (node.kind()) {
#define KIND_DISPATCH_CASE(name, node_type, category)           \
    case ParserToken::Kind::name:                               \
        self.visit_##name(static_cast<const node_type&>(node)); \
        return;
        PARSER_TOKEN_KINDS(KIND_DISPATCH_CASE)
#undef KIND_DISPATCH_CASE
    case ParserToken::Kind::END:
        break;
    }
    FAULT("dispatchEmit: '{}' is not a node", ParserToken::name(node.kind()));
}

/// Единая диагностика «узел недопустим в этой фазе» - без молчаливого no-op.
[[noreturn]] inline void faultUnexpectedKind(const char* consumer, const AstNodeBase& node) {
    FAULT("{}: kind '{}' must not reach this phase", consumer, ParserToken::name(node.kind()));
}

/// Явный Skip (аналог Roslyn `DefaultVisit`): узел уже обработан владельцем и не требует
/// emission. Каждый вызов - осознанное решение с обоснованием в комментарии обработчика;
/// недостижимые узлы обязаны идти через `faultUnexpectedKind`, а не сюда.
inline void skipNode(const AstNodeBase& /*node*/) noexcept {
}

} // namespace trust
