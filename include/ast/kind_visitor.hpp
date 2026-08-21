// include/ast/kind_visitor.hpp
// Единая ДИСПЕТЧЕРИЗАЦИЯ AST-узлов ПО KIND (не по классу).
//
// Классы AST (Binary, AstNodeAttr, ...) - чистые контейнеры данных (формат хранения),
// семантика узла целиком в ParserToken::Kind. Поэтому visitor строится по kind:
//   - один метод visit_<Kind>(const <класс>&) на каждый kind - ТИПИЗИРОВАННЫЙ (one kind → one class);
//   - абстрактный контракт KindVisitor (все pure virtual): «не переопределил → не компилируется»;
//   - dispatchKind(node, v) - исчерпывающий switch(kind) БЕЗ default, генерируется из
//     PARSER_TOKEN_KINDS (ЕДИНЫЙ источник kind→класс). static_cast по kind всегда однозначен.
//
// Добавление нового kind = одна строка в PARSER_TOKEN_KINDS: enum + интерфейс + диспетчер
// обновляются синхронно. Компилятор заставляет потребителя реализовать нужные visit_<Kind>.

#pragma once

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "ast/token_type.hpp"
#include "utils/error.hpp"

namespace trust {

/// Абстрактный контракт kind-визитора. По одному типизированному методу на kind.
/// Потребитель (например CppTranspiler, проходы семантики) наследует этот класс и
/// реализует все методы: «не переопределил → не компилируется».
///
/// ВАЖНО (архитектурный инвариант): полный набор visit_<Kind> у каждого потребителя -
/// это ОСОЗНАННАЯ compile-time проверка полноты, а НЕ дублирование. Каждый потребитель
/// ОБЯЗАН реализовать все visit_<Kind> (или явный no-op), чтобы добавленный в
/// PARSER_TOKEN_KINDS kind без обработки не компилировался. No-op-методы НЕ выносить
/// в default-базу (KindVisitorDefault) в продуктовый код - сохранять строгий абстрактный
/// контракт. (Зафиксировано в memory: компонент ast.)
struct KindVisitor {
#define KIND_VISITOR_METHOD(name, node_type) virtual void visit_##name(const node_type& node) = 0;
    PARSER_TOKEN_KINDS(KIND_VISITOR_METHOD)
#undef KIND_VISITOR_METHOD

    virtual ~KindVisitor() = default;
};

/// Центральный диспетчер по kind: switch без default, генерируется из PARSER_TOKEN_KINDS.
/// static_cast по kind всегда однозначен (one kind → one class). Для kind=END - FAULT.
inline void dispatchKind(const AstNodeBase& node, KindVisitor& visitor) {
    switch (node.kind()) {
#define KIND_DISPATCH_CASE(name, node_type)                        \
    case ParserToken::Kind::name: {                                \
        visitor.visit_##name(static_cast<const node_type&>(node)); \
        return;                                                    \
    }
        PARSER_TOKEN_KINDS(KIND_DISPATCH_CASE)
#undef KIND_DISPATCH_CASE
    default:
        FAULT("dispatchKind: непредусмотренный kind {}", int(node.kind()));
    }
}

} // namespace trust

// PARSER_TOKEN_KINDS намеренно НЕ #undef'ится здесь: он может понадобиться потребителям
// (например тестам, определяющим KindVisitorDefault). Источник макроса - ast/token.hpp,
// который тоже его не #undef'ит.