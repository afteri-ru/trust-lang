// token_type.cpp - реализация IdentType (терм-конструктор + dump)

#include "ast/token_type.hpp"
#include "ast/term_to_ast.hpp"
#include "syntax/term/term.h"

namespace trust {

// ----------------------------------------------------------------------------
// IdentType: терм-конструктор (kind всегда TypeName)
// Единственный владелец раскладки детей TYPE-терма:
//   m_dims   - из term->m_type (ARGS-терм размерностей `[...]`)
//   m_params - из term->m_args (call-аргументы `(...)`)
// Рекурсивная конвертация детей - через convertChild (при ctx != nullptr).
// ----------------------------------------------------------------------------

IdentType::IdentType(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: IdentType(term, std::nullopt, std::nullopt, ctx ? &ctx->attrs() : nullptr) {
    if (!ctx || !term) {
        return;
    }
    // m_dims: ARGS-терм размерностей [...] (каждый элемент - через at(i).second)
    if (term->m_type && term->m_type->size() > 0) {
        std::vector<AstNodePtr> dims;
        for (int64_t i = 0; i < term->m_type->size(); ++i) {
            if (AstNodePtr d = convertChild(*ctx, term->m_type->at(i).second); d) {
                dims.push_back(std::move(d));
            }
        }
        m_dims = std::move(dims);
    }
    // m_params: call-аргументы (...) - именованные ARGUMENT-обёртки (имя в m_left)
    if (term->m_args && !term->m_args->empty()) {
        std::vector<AstNodePtr> params;
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name;
            if (!argTerm || argTerm->getTermID() == trust::TermID::END) {
                continue;
            }
            if (AstNodePtr p = convertChild(*ctx, argTerm); p) {
                params.push_back(std::move(p));
            }
        }
        m_params = std::move(params);
    }
}

// ----------------------------------------------------------------------------
// IdentType::dump
// ----------------------------------------------------------------------------

std::string IdentType::dump(size_t indent) const {
    std::string result = detail::dumpQuotedName(kind(), text(), indent, ":");

    if (m_dims.has_value() && !m_dims->empty()) {
        result += " [";
        for (size_t i = 0; i < m_dims->size(); ++i) {
            if (i > 0) {
                result += ", ";
            }
            const auto& node = (*m_dims)[i];
            if (node) {
                result += node->dump(indent + 2);
            }
        }
        result += "]";
    }

    if (m_params.has_value() && !m_params->empty()) {
        result += " (";
        for (size_t i = 0; i < m_params->size(); ++i) {
            const auto& node = (*m_params)[i];
            if (node) {
                result += node->dump(indent + 2);
            }
        }
        result += ")";
    }

    return result;
}

} // namespace trust