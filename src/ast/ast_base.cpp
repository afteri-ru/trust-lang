// src/ast/ast_base.cpp
// Базовые/коллекционные узлы AST: Sequence, ArgNode, ScopeBlock, ModuleNode.
// Выделено из ast_nodes.cpp (модуль ast_base).
#include "ast/ast_nodes.hpp"
#include "ast/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "utils/error.hpp"
#include <string>
namespace trust {

Sequence::Sequence(ParserToken::Kind k, TermPtr term, Context* ctx)
: HasText(k, std::move(term)) {
    EXPECT(m_term && "Sequence term-constructor requires a source Term");
    if (ctx) {
        convertChildren(*ctx, m_term, m_body);
    }
}

ScopeBlock::ScopeBlock(TermPtr term, Context* ctx, int blockCounter)
: Sequence(ParserToken::Kind::ScopeBlock, std::move(term), ctx)
, m_blockCounter(blockCounter) {
}

ModuleNode::ModuleNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: ModuleNode(0, std::move(term)) {
    // Терм-конструктор вызывается только из visit_MODULE для сайтов импорта `\module(...)`.
    m_isImport = true;
    if (ctx) {
        // Тело модуля разворачивается: контейнер (SEQUENCE-терм) не оборачивается в ScopeBlock,
        // пользовательские { ... } (BLOCK-терм) сохраняются как ScopeBlock-узлы.
        for (const auto& childTerm : m_term->m_sequence) {
            convertModuleBody(*ctx, childTerm, m_body);
        }
    }
}

ArgNode::ArgNode(TermPtr term, AstNodePtr type, AstNodePtr value)
: HasText(ParserToken::Kind::ArgNode, std::move(term))
, m_type(std::move(type))
, m_value(std::move(value)) {
    EXPECT(m_term && "ArgNode term-constructor requires a source Term");
    m_text = normalizeTermText(ParserToken::Kind::ArgNode, declNameFromTerm(m_term));
}

// -- ArgNode: uniform терм-конструктор (kind = ArgNode). --
// КАНОНИЧЕСКАЯ раскладка аргумента: имя в m_left, явный тип в m_type (страховка `name:Type`
// без значения - m_right->m_type), значение в m_right. Единая для параметров функций, элементов
// словаря/enum/variant и аргументов вызова (терм_to_ast::appendDictElementsFromArgs использует
// manual-конструктор; здесь - uniform путь visit_ARGUMENT / параметры функции).
ArgNode::ArgNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: ArgNode(std::move(term)) {
    if (ctx && m_term) {
        // Тип: из m_type (канонический слот); страховка - m_right->m_type (`name:Type` без =value).
        if (m_term->m_type) {
            m_type = convertChild(*ctx, m_term->m_type);
        } else if (m_term->m_right && m_term->m_right->m_type) {
            m_type = convertChild(*ctx, m_term->m_right->m_type);
        }
        // Значение: у ARGUMENT m_right - значение (name=value / name:Type=value).
        if (m_term->getTermID() == trust::TermID::ARGUMENT && m_term->m_right) {
            m_value = convertChild(*ctx, m_term->m_right);
        }
    }
}

// -- Sequence::dumpBody - дамп содержимого std::vector<AstNodePtr> --

void Sequence::dumpBody(std::string& result, const std::vector<AstNodePtr>& body, size_t indent, size_t child_indent) {
    if (body.empty()) {
        return;
    }
    result += "\n";
    for (size_t i = 0; i < body.size(); i++) {
        result += std::string(indent, ' ');
        if (body[i]) {
            result += body[i]->dump(child_indent);
        }
        if (i + 1 < body.size()) {
            result += "\n";
        }
    }
}

// -- ArgNode::dump --

std::string ArgNode::dump(size_t indent) const {
    std::string result = detail::dumpQuotedName(kind(), text(), indent);
    dumpLabeled(result, indent, "type", m_type);
    dumpLabeled(result, indent, "value", m_value);
    return result;
}

// -- Sequence::dump --

std::string Sequence::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// -- ScopeBlock::dump --
std::string ScopeBlock::dump(size_t indent) const {
    std::string result = AstNodeAttr::dump(indent);
    if (!text().empty()) {
        result += text();
    } else if (m_blockCounter > 0) {
        result += std::to_string(m_blockCounter);
    }
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// -- ModuleNode::dump --

std::string ModuleNode::dump(size_t indent) const {
    // Паттерн ScopeBlock::dump: заголовок (kind) + имя, затем дети ОДИН раз.
    // НЕ используем Sequence::dump - он уже печатает m_body (иначе было бы дважды).
    std::string result = detail::dumpQuotedName(kind(), text(), indent);
    dumpBody(result, m_body, indent, indent + 2);
    return result;
}
} // namespace trust
