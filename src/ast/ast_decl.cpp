// src/ast/ast_decl.cpp
// Объявления AST: Decl, VarDecl, FuncDecl, DestructureDecl.
// Выделено из ast_nodes.cpp (модуль ast_decl).
#include "ast/ast_nodes.hpp"
#include "ast/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "utils/error.hpp"
#include <string>
#include <vector>
namespace trust {

// True, если терм - «чистое» многоточие `<name> := ...;` (forward-объявление): ELLIPSIS без
// детей. Для извлечения из коллекции `... dict` (ELLIPSIS c rval в m_right) - false, такой
// терм не является признаком forward-объявления.
static bool isForwardEllipsisTerm(const TermPtr& term) {
    if (!term || term->getTermID() != trust::TermID::ELLIPSIS) {
        return false;
    }
    return !term->m_left && !term->m_right && term->m_sequence.empty() && !term->m_args.has_value();
}

// True, если терм - нативный импорт `<name>(...) := %sym...;` (m_right - native-терм `%sym`).
static bool isNativeImportTerm(const TermPtr& term) {
    return term && term->getTermID() == trust::TermID::NATIVE;
}

// C++-имя нативного импорта: убираем ведущий '%' (`%abs`→"abs", `%std::sqrt`→"std::sqrt").
static std::string nativeImportName(const TermPtr& term) {
    std::string_view t = term ? term->getText() : std::string_view{};
    if (t.size() > 1 && t[0] == '%') {
        t.remove_prefix(1);
    }
    return std::string(t);
}

// -- VarDecl/FuncDecl/ArgNode: терм-конструкторы - имя из m_left (fallback term->getText()).
VarDecl::VarDecl(TermPtr term, AstNodePtr type, AstNodePtr initializer)
: Decl(std::move(term))
, m_initializer(std::move(initializer)) {
    EXPECT(m_term && "VarDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::VarDecl;
    m_type = std::move(type);
    m_text = normalizeTermText(ParserToken::Kind::VarDecl, declNameFromTerm(m_term));
}

// -- VarDecl: uniform терм-конструктор (kind = VarDecl). --
VarDecl::VarDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: VarDecl(std::move(term)) {
    if (ctx && m_term) {
        if (m_term->m_left && m_term->m_left->m_type) {
            m_type = convertChild(*ctx, m_term->m_left->m_type);
        }
        // Предварительное (forward) объявление `x:Type := ...;`: m_right - чистое многоточие
        // вместо инициализатора. m_initializer остаётся nullptr (forward declaration).
        if (isForwardEllipsisTerm(m_term->m_right)) {
            return;
        }
        if (m_term->m_right) {
            std::vector<AstNodePtr> body;
            convertChildren(*ctx, m_term, body);
            if (body.size() >= 2 && body[1]) {
                m_initializer = std::move(body[1]);
            } else if (body.size() >= 1 && body[0]) {
                m_initializer = std::move(body[0]);
            }
        }
    }
}

// -- DestructureDecl: `t1, ..., tN := [... ]source;` --
DestructureDecl::DestructureDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: AstNodeAttr(ParserToken::Kind::DestructureDecl, std::move(term)) {
    EXPECT(m_term && "DestructureDecl term-constructor requires a source Term");
    if (!ctx || !m_term) {
        return;
    }
    m_isAssign = m_term->getTermID() == trust::TermID::ASSIGN;
    m_isSpread = m_term->m_right && m_term->m_right->getTermID() == trust::TermID::ELLIPSIS;
    if (m_term->m_left) {
        for (const TermPtr* t = &m_term->m_left; *t; t = &(*t)->m_left) {
            // Цель - имя переменной из терма lval; IdentName конструируем напрямую (convertChild
            // для lval-терма может дать не тот kind). rest-цель (`rest...`) - lval с суффиксом
            // ELLIPSIS в m_right (грамматика assign_item: `lval ELLIPSIS`). Явная аннотация типа
            // цели (`a:Int32`) хранится в lval->m_type (грамматика `lval_var: name type_item`
            // → m_type = type_item); конвертируем её в узел типа для семантики/кодгена.
            const bool isRest = (*t)->m_right && (*t)->m_right->getTermID() == trust::TermID::ELLIPSIS;
            m_targets.push_back(std::make_shared<IdentName>(std::string((*t)->getText())));
            m_targetIsRest.push_back(isRest);
            m_targetTypeNodes.push_back((*t)->m_type ? convertChild(*ctx, (*t)->m_type) : AstNodePtr{});
        }
    }
    TermPtr src;
    if (m_isSpread) {
        if (m_term->m_right && m_term->m_right->m_right) {
            src = m_term->m_right->m_right;
        }
    } else {
        src = m_term->m_right;
    }
    if (src) {
        m_source = convertChild(*ctx, src);
    }
}

std::string DestructureDecl::dump(size_t indent) const {
    std::string result(indent, ' ');
    result += "DestructureDecl";
    if (m_isAssign) {
        result += " (assign)";
    }
    result += "\n";
    for (size_t i = 0; i < m_targets.size(); ++i) {
        const auto& t = m_targets[i];
        const bool isRest = i < m_targetIsRest.size() && m_targetIsRest[i];
        result += std::string(indent + 2, ' ') + "target: " + (t ? std::string(t->text()) : std::string{}) + (isRest ? "..." : "") + "\n";
    }
    if (m_source) {
        result += m_source->dump(indent + 2);
    }
    return result;
}

MapperRange DestructureDecl::range() const {
    if (!m_term) {
        return {};
    }
    // Полный охват `t1, ..., tN := [... ]source;`: начало - первый lval-терм цепочки имён
    // (m_term->m_left), конец - источник (m_source->range(); для `... source` источник - операнд
    // ELLIPSIS). Базовый m_term (CREATE_NAME `:=`) имеет m_mapperRange на оператор `:=`; без
    // расширения маппинг деструктуризации сужался бы до одного оператора (как VarDecl::range()).
    const bool beginOk = m_term->m_left && !m_term->m_left->m_mapperRange.isInvalid();
    const bool endOk = m_source && !m_source->range().isInvalid();
    if (beginOk && endOk && m_term->m_left->m_mapperRange.begin.fileIdx() == m_source->range().end.fileIdx() &&
        m_term->m_left->m_mapperRange.begin.offset() <= m_source->range().end.offset()) {
        return MapperRange{m_term->m_left->m_mapperRange.begin, m_source->range().end};
    }
    return AstNodeAttr::range();
}

FuncDecl::FuncDecl(TermPtr term)
: Decl(std::move(term)) {
    EXPECT(m_term && "FuncDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::FuncDecl;
    m_text = normalizeTermText(ParserToken::Kind::FuncDecl, declNameFromTerm(m_term));
}

// -- FuncDecl: uniform терм-конструктор (kind = FuncDecl). --
FuncDecl::FuncDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: FuncDecl(std::move(term)) {
    if (!ctx || !m_term) {
        return;
    }
    // CREATE_NAME (`:=`) с сигнатурой функции в m_left (m_left->isCall()) - это функция.
    const bool sigInLeft = m_term->getTermID() == trust::TermID::CREATE_NAME && m_term->m_left && m_term->m_left->isCall();
    if (sigInLeft) {
        if (m_term->m_left->m_type) {
            m_type = convertChild(*ctx, m_term->m_left->m_type);
        }
        m_params = std::vector<AstNodePtr>{};
        // Параметры: m_args на сигнатуре (ARGUMENT: имя в m_left, тип в m_right).
        // Раскладка m_type - в ArgNode-конструкторе.
        if (m_term->m_left->m_args) {
            for (const auto& [name, argTerm] : *m_term->m_left->m_args) {
                (void)name;
                if (!argTerm) {
                    continue;
                }
                m_params->push_back(std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, argTerm, ctx));
            }
        }
        // Нативный импорт `<name>(...) := %sym...;` - АЛИАС на нативную функцию: C++-функция
        // НЕ эмитится, вызовы name(...) переписываются в прямой вызов %sym(...). m_body пуст.
        if (isNativeImportTerm(m_term->m_right)) {
            m_isNativeImport = true;
            m_nativeName = nativeImportName(m_term->m_right);
            return;
        }
        // Тело: m_right - block. Предварительное объявление `%func():Type := ...;` -
        // m_right - чистое многоточие вместо тела → m_body = nullopt (forward declaration).
        if (isForwardEllipsisTerm(m_term->m_right)) {
            return;
        }
        if (m_term->m_right) {
            std::vector<AstNodePtr> fnBody;
            convertChildren(*ctx, m_term->m_right, fnBody);
            m_body = std::move(fnBody);
        }
    } else {
        // Lambda/iterator: split params (ArgNode) / body из convertChildren.
        std::vector<AstNodePtr> body;
        convertChildren(*ctx, m_term, body);
        std::vector<AstNodePtr> params;
        std::vector<AstNodePtr> fnBody;
        bool inParams = true;
        for (auto& tok : body) {
            if (inParams && tok && tok->kind() == ParserToken::Kind::ArgNode) {
                params.push_back(std::move(tok));
            } else {
                inParams = false;
                fnBody.push_back(std::move(tok));
            }
        }
        if (!params.empty()) {
            m_params = std::move(params);
        }
        if (!fnBody.empty()) {
            m_body = std::move(fnBody);
        }
    }
}

// -- Decl::dump --

std::string Decl::dump(size_t indent) const {
    // Имя выводится один раз (ident_name/dump даёт "Kind 'text'"); ранее здесь
    // ошибочно добавлялся второй "'text'" → "Kind 'x' 'x'". Исправлено.
    std::string result = detail::dumpQuotedName(kind(), text(), indent);
    if (m_type) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "type: ";
        result += m_type->dump(indent + 2);
    }
    // Trust-конструкции (pre/post/assert), привязанные к объявлению.
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

// -- FuncDecl::dump --

std::string FuncDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_params) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "params:";
        for (size_t i = 0; i < m_params->size(); ++i) {
            result += "\n";
            const auto& param = (*m_params)[i];
            if (param) {
                result += param->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    }
    if (m_body) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "body:";
        for (size_t i = 0; i < m_body->size(); ++i) {
            result += "\n";
            const auto& stmt = (*m_body)[i];
            if (stmt) {
                result += stmt->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    } else {
        result += " (forward)";
    }
    return result;
}

// -- FuncDecl::signature - строка сигнатуры для контекст-макроса @__FUNCSIG__ --

std::string FuncDecl::signature(std::string_view namespace_path) const {
    std::string name(text());
    if (!name.empty() && name.front() == '%') {
        name.erase(0, 1);
    }
    std::string sig;
    if (!namespace_path.empty()) {
        sig.assign(namespace_path);
        sig += "::";
    }
    sig += name;
    sig += "(";
    if (m_params) {
        bool first = true;
        for (const auto& p : *m_params) {
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            const auto& pd = static_cast<const ArgNode&>(*p);
            if (!first) {
                sig += ", ";
            }
            first = false;
            sig += pd.text();
            if (pd.m_type) {
                sig += ":";
                sig += pd.m_type->text();
            }
        }
    }
    sig += ")";
    if (m_type) {
        sig += ":";
        sig += m_type->text();
    }
    return sig;
}

MapperRange FuncDecl::blockRange() const noexcept {
    // Тело функции - блок { ... } лежит в m_term->m_right (терм CREATE_TYPE ::=).
    // Его range используется для определения строк { и }, чтобы сгенерированный код
    // повторял раскладку исходника. Для test-only узлов без терма - invalid range.
    if (m_term && m_term->m_right) {
        return m_term->m_right->m_mapperRange;
    }
    return {};
}

// -- VarDecl::dump --

std::string VarDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_initializer) {
        dumpLabeled(result, indent, "init", m_initializer);
    } else {
        result += " (forward)";
    }
    return result;
}

// -- VarDecl::nameRange --
MapperRange VarDecl::nameRange() const noexcept {
    if (m_term && m_term->m_left && !m_term->m_left->m_mapperRange.isInvalid()) {
        return m_term->m_left->m_mapperRange;
    }
    return {};
}

MapperRange VarDecl::range() const {
    if (!m_term) {
        return {};
    }
    // [имя, инициализатор] - обе стороны обязательны (прежний expandTermRangeToChildren).
    // Имя из nameRange() (m_term->m_left), инициализатор - узел-ребёнок.
    if (m_initializer) {
        const MapperRange n = nameRange();
        const MapperRange init = m_initializer->range();
        if (!n.isInvalid() && !init.isInvalid() && n.begin.fileIdx() == init.end.fileIdx() && n.begin.offset() <= init.end.offset()) {
            return MapperRange{n.begin, init.end};
        }
    }
    return AstNodeAttr::range();
}

MapperRange FuncDecl::range() const {
    if (!m_term) {
        return {};
    }
    // CREATE_NAME (`:=`) с сигнатурой функции в m_left: [имя, оператор] (без тела).
    // Совпадает с прежней мутацией term->m_mapperRange в конструкторе (см. FuncDecl-конструктор).
    if (m_term->getTermID() == trust::TermID::CREATE_NAME && m_term->m_left && m_term->m_left->isCall() && !m_term->m_left->m_mapperRange.isInvalid() &&
        !m_term->m_mapperRange.isInvalid() && m_term->m_left->m_mapperRange.begin.fileIdx() == m_term->m_mapperRange.end.fileIdx() &&
        m_term->m_left->m_mapperRange.begin.offset() <= m_term->m_mapperRange.end.offset()) {
        return MapperRange{m_term->m_left->m_mapperRange.begin, m_term->m_mapperRange.end};
    }
    return AstNodeAttr::range();
}
} // namespace trust
