// src/ast/nodes/ast_base.cpp
// Out-of-line часть иерархии AstNodeBase: базовые классы узла (AstNodeBase/AstNodeAttr/HasText),
// коллекционные узлы (Sequence/ArgNode/ScopeBlock/ModuleNode), trust-контракты
// (TrustContract/TrustElem) и разбор атрибута @[...]. Один TU: "база + базовые узлы".
#include "ast/ast_nodes.hpp"
#include "ast/nodes/ast_helpers.hpp"
#include "ast/nodes/attr_parser.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "ast/trust_prop.hpp"
#include "ast/nodes/z3_term.hpp"
#include "attrs/attr_pool.hpp"
#include "diag/base_diags.hpp"
#include "diag/diag.hpp"
#include "syntax/term/term.h"
#include "utils/error.hpp"
#include <string>
namespace trust {

Sequence::Sequence(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeAttr(k, term) {
    EXPECT(term && "Sequence term-constructor requires a source Term");
    if (ctx) {
        convertChildren(*ctx, term, m_body);
    }
}

ScopeBlock::ScopeBlock(TermPtr term, Context* ctx, int blockCounter)
: WithText<Sequence>(ParserToken::Kind::ScopeBlock, term, ctx)
, m_blockCounter(blockCounter) {
}

ArgNode::ArgNode(TermPtr term, AstNodePtr type, AstNodePtr value, AttrPool* pool)
: WithText<AstNodeAttr>(ParserToken::Kind::ArgNode, term, NoTextTag{})
, m_type(std::move(type))
, m_value(std::move(value)) {
    EXPECT(term && "ArgNode term-constructor requires a source Term");
    assignNameFromRaw(*this, *this, ParserToken::Kind::ArgNode, declNameFromTerm(term), pool);
    isTypePack = term->m_isPack;
}

// -- ArgNode: uniform терм-конструктор (kind = ArgNode). --
// КАНОНИЧЕСКАЯ раскладка аргумента: имя в m_left, явный тип в m_type (страховка `name:Type`
// без значения - m_right->m_type), значение в m_right. Единая для параметров функций, элементов
// словаря/enum/variant и аргументов вызова (терм_to_ast::appendDictElementsFromArgs использует
// manual-конструктор; здесь - uniform путь visit_ARGUMENT / параметры функции).
ArgNode::ArgNode(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: ArgNode(term, nullptr, nullptr, ctx ? &ctx->attrs() : nullptr) {
    if (ctx && term) {
        // Тип: из m_type (канонический слот); страховка - m_right->m_type (`name:Type` без =value).
        if (term->m_type) {
            m_type = convertChild(*ctx, term->m_type);
        } else if (term->m_right && term->m_right->m_type) {
            m_type = convertChild(*ctx, term->m_right->m_type);
        }
        // Значение: у ARGUMENT m_right - значение (name=value / name:Type=value).
        if (term->getTermID() == trust::TermID::ARGUMENT && term->m_right) {
            m_value = convertChild(*ctx, term->m_right);
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
    if (isTypePack) {
        result += "...";
    }
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

// Поля узла: из Term извлекается ТОЛЬКО range (текст читает WithText/HasText).
AstNodeBase::AstNodeBase(ParserToken::Kind k, TermPtr term)
: m_kind(k)
, m_range(term ? term->m_mapperRange : MapperRange{}) {
}

// HasText: единственная точка чтения текста Term (как есть, без нормализации).
// Терм обязателен: узел-носитель без источника текста - нарушение инварианта kind->класс
// (написание задаётся либо Term, либо явным текстом/manual-конструктором, см. WithText).
HasText::HasText(TermPtr term) {
    EXPECT(term && "text carrier requires a source Term (use an explicit-text ctor instead)");
    set_text_interned(term->getText());
}

// ----------------------------------------------------------------------------
// AstNodeBase::dump - default dump for base token
// ----------------------------------------------------------------------------

std::string AstNodeBase::dump(size_t indent) const {
    std::string result(indent, ' ');
    result += ParserToken::name(kind());
    if (!m_trust.empty()) {
        for (const auto& t : m_trust) {
            if (t) {
                result += "\n";
                result += std::string(indent, ' ');
                result += "trust: ";
                result += t->dump(indent + 2);
            }
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// AstNodeAttr::has_attr (by name)
// ----------------------------------------------------------------------------

bool AstNodeAttr::has_attr(const AttrPool& pool, std::string_view name) const {
    auto id = pool.lookup(name);
    if (!id.has_value()) {
        return false;
    }
    return has_attr(id.value());
}

// ----------------------------------------------------------------------------
// AstNodeAttr::set_attr_args / attr_args - аргументы атрибута (напр. @[link("m")] → ["m"]).
// Хранятся по индексу атрибута в пуле (id & kAttrIndexMask).
// ----------------------------------------------------------------------------

void AstNodeAttr::set_attr_args(AttrId id, std::vector<std::string> args) {
    const AttrId key = id & detail::kAttrIndexMask;
    m_attrArgs[key] = std::move(args);
}

const std::vector<std::string>* AstNodeAttr::attr_args(AttrId id) const noexcept {
    const AttrId key = id & detail::kAttrIndexMask;
    auto it = m_attrArgs.find(key);
    if (it == m_attrArgs.end()) {
        return nullptr;
    }
    return &it->second;
}

// ----------------------------------------------------------------------------
// AstNodeAttr::dump - default dump for attribute-aware token
// ----------------------------------------------------------------------------

std::string AstNodeAttr::dump(size_t indent) const {
    return AstNodeBase::dump(indent);
}

// ----------------------------------------------------------------------------
// AstNodeBase::trustContracts / hasTrustProperty - типизированный доступ к m_trust (R1).
// Фильтрация только TrustContract-узлов (без RTTI) - единый источник для потребителей.
// ----------------------------------------------------------------------------

std::vector<const TrustContract*> AstNodeBase::trustContracts() const {
    std::vector<const TrustContract*> result;
    result.reserve(m_trust.size());
    for (const auto& t : m_trust) {
        EXPECT(t && "AstNodeBase::m_trust must not contain null entries");
        EXPECT(t->is<TrustContract>() && "AstNodeBase::m_trust must contain only TrustContract nodes");
        result.push_back(t->as<TrustContract>());
    }
    return result;
}

std::vector<TrustContract*> AstNodeBase::trustContracts() {
    std::vector<TrustContract*> result;
    result.reserve(m_trust.size());
    for (const auto& t : m_trust) {
        EXPECT(t && "AstNodeBase::m_trust must not contain null entries");
        EXPECT(t->is<TrustContract>() && "AstNodeBase::m_trust must contain only TrustContract nodes");
        result.push_back(t->as<TrustContract>());
    }
    return result;
}

bool AstNodeBase::hasTrustProperty(PropertyKind kind) const {
    for (const auto& t : m_trust) {
        EXPECT(t && "AstNodeBase::m_trust must not contain null entries");
        EXPECT(t->is<TrustContract>() && "AstNodeBase::m_trust must contain only TrustContract nodes");
        if (t->as<TrustContract>()->kind == kind) {
            return true;
        }
    }
    return false;
}

// ----------------------------------------------------------------------------
// parse_attr
// ----------------------------------------------------------------------------

std::optional<AttrId> parse_attr(Context& ctx, MapperRange range, std::string_view name, std::optional<std::vector<std::string_view>> params) {

    if (name.empty()) {
        ctx.diag().report(Severity::Error, range, "expected attribute name");
        return std::nullopt;
    }

    // Look up existing attribute
    auto existing_id = ctx.attrs().lookup(name);
    if (existing_id.has_value()) {
        // If parameters are supplied, validate them against the registered defaults.
        if (params.has_value()) {
            const Attr& attr = ctx.attrs().get(*existing_id);
            if (!attr.matches_params(*params)) {
                ctx.diag().report(Severity::Error, range, "attribute '{}' has mismatched parameters", name);
                return std::nullopt;
            }
        }
        return existing_id;
    }

    // Attribute not found - do NOT register a new one. Severity-controlled diagnostic:
    // -Wunknown-attributes (default Error; silence with =ignore). Если опция не зарегистрирована
    // (например parse-only путь без регистрации базовых диагностик) - жёсткий Error.
    if (ctx.opts().isRegisteredByName(diagName(diag::DiagId::UnknownAttribute))) {
        ctx.report(range, diag::DiagId::UnknownAttribute, "unknown attribute '{}'", name);
    } else {
        ctx.diag().report(Severity::Error, range, "unknown attribute '{}'", name);
    }
    return std::nullopt;
}

// -- TrustContract: единый узел trust-контракта (pre/post/assert/invariant/type). --
TrustContract::TrustContract(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeBase(k, term) {
    if (term && term->m_left && !term->m_left->getText().empty()) {
        // `@{ kind --> expr @}`: m_left = идентификатор kind (резолв из фиксированного набора).
        kind = parsePropertyKind(term->m_left->getText()).value_or(PropertyKind::kUnknown);
    }
    if (ctx && term && term->m_right) {
        m_expr = convertChild(*ctx, term->m_right);
    }
}

std::string TrustContract::dump(size_t indent) const {
    std::string result = AstNodeBase::dump(indent);
    if (kind != PropertyKind::kUnknown) {
        result += " kind=";
        result += propertyKindName(kind);
    }
    dumpLabeled(result, indent, "expr", m_expr);
    return result;
}

// -- TrustElem: термин решателя `@( term, args... @)` внутри выражения контракта. --
TrustElem::TrustElem(ParserToken::Kind k, TermPtr term, Context* ctx)
: AstNodeBase(k, term) {
    if (term && term->m_left && !term->m_left->getText().empty()) {
        kind = parseZ3Term(term->m_left->getText()).value_or(Z3TermKind::kUnknown);
    }
    if (ctx && term) {
        // Аргументы `@( term, args... @)` хранятся в m_args (ArgsList, из args-терма грамматики).
        if (term->m_args) {
            for (const auto& a : *term->m_args) {
                if (a.second) {
                    m_args.push_back(convertChild(*ctx, a.second));
                }
            }
        }
    }
}

std::string TrustElem::dump(size_t indent) const {
    std::string result = AstNodeBase::dump(indent);
    result += " term=";
    result += z3TermName(kind);
    for (const auto& a : m_args) {
        result += "\n";
        result += std::string(indent + 1, ' ');
        result += "arg: ";
        result += a ? a->dump(indent + 1) : "<null>";
    }
    return result;
}
} // namespace trust
