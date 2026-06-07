#include "pipeline/term_to_ast.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/attr_builtin.hpp"
#include "ast/attr_parser.hpp"
#include "ast/token_type.hpp"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "utils/error.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace trust {

// ── Helper: map TermID → ParserToken::Kind ──
// This is approximate — legacy and new ASTs have different grammars.
// For downstream passes (SemanticAnalyzer, CppTranspiler) the critical
// thing is that the tree structure is correct and nodes have meaningful Kind.

static ParserToken::Kind mapTermID(trust::TermID id) {
    using TK = trust::TermID;
    using PK = ParserToken::Kind;

    switch (id) {
    // Blocks / sequences
    case TK::SEQUENCE:
    case TK::BLOCK:
    case TK::BLOCK_TRY:
    case TK::BLOCK_PLUS:
    case TK::BLOCK_MINUS:
        return PK::ScopeBlock;

    // Names and identifiers
    case TK::NAME:
    case TK::LOCAL:
    case TK::STATIC:
    case TK::MACRO:
    case TK::NATIVE:
    case TK::MANGLED:
    case TK::MODULE:
        return PK::Ident;

    // Types
    case TK::TYPE:
    case TK::TYPEDUCK:
    case TK::TYPECAST:
        return PK::TypeName;

    // Literals
    case TK::INTEGER:
        return PK::IntLiteral;
    case TK::NUMBER:
    case TK::COMPLEX:
        return PK::FloatLiteral;
    case TK::RATIONAL:
        return PK::IntLiteral;
    case TK::STRWIDE:
    case TK::STRCHAR:
    case TK::TEMPLATE:
        return PK::StringLiteral;
    case TK::REFLECTION:
    case TK::EMBED:
        return PK::EmbedExpr;

    // Declarations
    case TK::FUNCTION:
    case TK::COROUTINE:
    case TK::ITERATOR:
        return PK::FuncDecl;
    case TK::CLASS:
        return PK::StructDecl;

    // Parameters
    case TK::ARGS:
    case TK::ARGUMENT:
        return PK::ParamDecl;

    // Assignment / creation
    case TK::CREATE_NAME:
        return PK::NameDecl;
    case TK::APPEND:
    case TK::SWAP:
        return PK::AssignmentStmt;

    // Operators + type declarations (::=) + assignment (=)
    case TK::ASSIGN:
        return PK::AssignOp;
    case TK::OP_MATH:
        return PK::MathOp;
    case TK::OP_BITWISE:
        return PK::BitwiseOp;
    case TK::OP_COMPARE:
        return PK::CompareOp;
    case TK::OP_LOGICAL:
        return PK::LogicalOp;
    case TK::CREATE_TYPE:
        return PK::TypeDecl;

    // Control flow
    case TK::INT_PLUS:
        return PK::ReturnStmt;
    case TK::INT_MINUS:
        return PK::ThrowStmt;
    case TK::WHILE:
        return PK::WhileStmt;
    case TK::DOWHILE:
        return PK::DoWhileStmt;
    case TK::FOLLOW:
        return PK::MatchingStmt;
    case TK::WITH:
        return PK::MatchingStmt;
    case TK::OPERATOR_PTR:
        return PK::RefMakeExpr;
    case TK::TAKE:
        return PK::RefTakeExpr;

    // Ranges and collections
    case TK::RANGE:
        return PK::ArrayInit;
    case TK::TENSOR:
        return PK::ArrayInit;
    case TK::DICT:
    case TK::SET:
        return PK::EnumLiteral;
    case TK::INDEX:
        return PK::ArrayAccess;
    case TK::FIELD:
        return PK::MemberAccess;

    // Attributes
    case TK::ATTRIBUTE:
        return PK::Attr;

    // Macros
    case TK::MACRO_SEQ:
    case TK::MACRO_DEL:
    case TK::MACRO_STR:
    case TK::MACRO_TOSTR:
    case TK::MACRO_CONCAT:
    case TK::MACRO_ARGUMENT:
    case TK::MACRO_ARGNAME:
    case TK::MACRO_ARGPOS:
    case TK::MACRO_ARGCOUNT:
        return PK::EmbedExpr;

    // Symbols / punctuation
    case TK::SYMBOL:
    case TK::NAMESPACE:
    case TK::PARENT:
    case TK::ESCAPE:
        return PK::ExprStmt;

    // END / no-value
    case TK::END:
    case TK::NONE:
        return PK::END;

    default:
        return PK::ExprStmt;
    }
}

// ── Recursive converter ──

static void convertTermToSeq(const trust::TermPtr& term, std::vector<AstNodePtr>& out, Context& ctx);

/// Может ли узел данного kind нести признак иммутабельности ('^' → attr::Const)?
static bool canHaveImmutableQualifier(const trust::TermPtr& term, ParserToken::Kind kind) {
    if (term->getTermID() == trust::TermID::OPERATOR_PTR)
        return true; // операторы ptr: &1^, &^, &&^, &*^, &?^
    switch (kind) {
    case ParserToken::Kind::Ident:       // имена: arg^, @name^, $name^, value^
    case ParserToken::Kind::TypeName:    // :Type^
    case ParserToken::Kind::ParamDecl:   // параметры: arg^, $1^
    case ParserToken::Kind::EnumLiteral: // <,>:Error^
    case ParserToken::Kind::VarDecl:     // x^
    case ParserToken::Kind::NameDecl:    // x^ := ...
    case ParserToken::Kind::FuncDecl:    // func^
    case ParserToken::Kind::StructDecl:  // class^
    case ParserToken::Kind::RefTakeExpr: // *^ — take с иммутабельностью
    case ParserToken::Kind::RefMakeExpr: // &^ — ptr с иммутабельностью
        return true;
    default:
        return false;
    }
}

static void convertAttrsToNode(const trust::TermPtr& term, AstNodePtr& node, Context& ctx) {
    if (!node)
        return;
    if (auto* attrNode = node->as_attr()) {
        // Пользовательские атрибуты @[ ... ]@, собранные парсером в term->m_attr.
        for (const auto& attrTerm : term->m_attr) {
            if (!attrTerm)
                continue;
            std::vector<std::string_view> params;
            if (attrTerm->m_args) {
                for (const auto& [name, argTerm] : *attrTerm->m_args) {
                    (void)name;
                    if (argTerm)
                        params.push_back(argTerm->getText());
                }
            }
            std::optional<std::vector<std::string_view>> optParams;
            if (!params.empty())
                optParams = std::move(params);
            if (auto id = parse_attr(ctx, attrTerm->m_mapperRange, attrTerm->getText(), optParams))
                attrNode->add_attr(*id);
        }
        // Иммутабельность ('^' в имени) → attr::Const с ручным признаком.
        std::string_view text = term->getText();
        if (!text.empty() && text.back() == '^' && canHaveImmutableQualifier(term, node->kind())) {
            auto const_id = ctx.attrs().lookup(attr::Const);
            EXPECT(const_id.has_value() && "Predefined attr::Const not registered!");
            attrNode->add_attr(*const_id);
        }
    }
}

/// Расширяет диапазон составного (бинарного) терма до [left.begin, right.end] — полного
/// охвата строки/выражения. Терм оператора (`:=`, `=`, `+=`, `-=`, `::=` и т.п.) несёт
/// range только оператора; для корректного source-map (mapStart всей строки в транспиляторе)
/// диапазон расширяется до границ left/right.
/// Guard: только когда left/right валидны, лежат в одном файле и begin <= end
/// (после макро-раскрытия left/right могут оказаться в разных псевдо-файлах).
static void expandTermRangeToChildren(const trust::TermPtr& term) {
    if (term->m_left && term->m_right && !term->m_left->m_mapperRange.isInvalid() && !term->m_right->m_mapperRange.isInvalid() &&
        term->m_left->m_mapperRange.begin.fileIdx() == term->m_right->m_mapperRange.end.fileIdx() &&
        term->m_left->m_mapperRange.begin.offset() <= term->m_right->m_mapperRange.end.offset()) {
        term->m_mapperRange = MapperRange{term->m_left->m_mapperRange.begin, term->m_right->m_mapperRange.end};
    }
}

static AstNodePtr makeNodeForKind(const trust::TermPtr& term, ParserToken::Kind kind, const std::string& text, std::vector<AstNodePtr> body) {
    AstNodePtr node;

    switch (kind) {
    case ParserToken::Kind::Ident: {
        // Имена с детьми (например, вызов term(...)) — Sequence; простые имена — IdentName.
        if (!body.empty()) {
            auto seq = std::make_shared<Sequence>(kind, text, term);
            seq->m_body = std::move(body);
            node = std::move(seq);
        } else {
            node = std::make_shared<IdentName>(text, term);
        }
        break;
    }
    case ParserToken::Kind::TypeName: {
        if (!body.empty()) {
            auto seq = std::make_shared<Sequence>(kind, text, term);
            seq->m_body = std::move(body);
            node = std::move(seq);
        } else {
            node = std::make_shared<IdentType>(text, term);
        }
        break;
    }
    case ParserToken::Kind::ScopeBlock: {
        auto sb = std::make_shared<ScopeBlock>(text, term, 0);
        sb->m_body = std::move(body);
        node = std::move(sb);
        break;
    }
    case ParserToken::Kind::FuncDecl: {
        auto fd = std::make_shared<FuncDecl>(text, term);
        // First parameter-like children go to m_params, rest to m_body
        std::vector<AstNodePtr> params;
        std::vector<AstNodePtr> fnBody;
        bool inParams = true;
        for (auto& tok : body) {
            if (inParams && tok && tok->kind() == ParserToken::Kind::ParamDecl) {
                params.push_back(std::move(tok));
            } else {
                inParams = false;
                fnBody.push_back(std::move(tok));
            }
        }
        if (!params.empty())
            fd->m_params = std::move(params);
        if (!fnBody.empty())
            fd->m_body = std::move(fnBody);
        node = std::move(fd);
        break;
    }
    case ParserToken::Kind::ParamDecl: {
        auto pd = std::make_shared<ParamDecl>(text, term);
        node = std::move(pd);
        break;
    }
    case ParserToken::Kind::VarDecl:
    case ParserToken::Kind::AssignmentStmt: {
        // Legacy CREATE_NAME stores left=name, right=initializer.
        // convertTermToSeq emits m_left first, then m_right.
        std::string name = text;
        AstNodePtr init;
        if (!body.empty()) {
            name = std::string(body[0]->text());
            if (body.size() >= 2)
                init = std::move(body[1]);
        }
        auto vd = std::make_shared<VarDecl>(name, term);
        vd->m_initializer = std::move(init);
        node = std::move(vd);
        break;
    }
    case ParserToken::Kind::ReturnStmt:
    case ParserToken::Kind::ThrowStmt: {
        // Роль (return/throw) определяется по TermID, а не по тексту.
        // Текст узла AST — '++'/'--'; namespace из m_text уходит в m_label.
        auto js = std::make_shared<JumpStmt>(kind, term);
        if (term->getTermID() == trust::TermID::INT_PLUS || term->getTermID() == trust::TermID::INT_MINUS) {
            std::string_view ns = term->getText();
            const char* op = kind == ParserToken::Kind::ReturnStmt ? "++" : "--";
            if (!ns.empty() && ns != op) {
                auto labelTerm = Term::Create(trust::TermID::NAME, std::string(ns));
                js->m_label = std::make_shared<AstNodeAttr>(ParserToken::Kind::Ident, std::move(labelTerm));
            }
        }
        if (!body.empty()) {
            js->m_value = std::move(body[0]);
        }
        node = std::move(js);
        break;
    }
    case ParserToken::Kind::IntLiteral:
    case ParserToken::Kind::FloatLiteral:
    case ParserToken::Kind::StringLiteral: {
        node = std::make_shared<Literal>(kind, text, term);
        break;
    }
    default: {
        if (is_binary_kind(kind)) {
            // Составной (бинарный) узел: расширяем range терма до полного охвата left/right
            // (универсально для `=`, `+=`, `-=`, `::=` и т.п.) — см. expandTermRangeToChildren.
            expandTermRangeToChildren(term);
            AstNodePtr left;
            AstNodePtr right;
            if (body.size() >= 1 && body[0])
                left = body[0];
            if (body.size() >= 2 && body[1])
                right = body[1];
            node = std::make_shared<Binary>(kind, term, std::move(left), std::move(right));
            break;
        }
        // Generic node: if there are children, make a Sequence, otherwise plain AstNodeAttr
        if (!body.empty()) {
            auto seq = std::make_shared<Sequence>(kind, text, term);
            seq->m_body = std::move(body);
            node = std::move(seq);
        } else {
            node = std::make_shared<AstNodeAttr>(kind, term);
        }
        break;
    }
    }

    return node;
}

static AstNodePtr makeNode(const trust::TermPtr& term, Context& ctx) {
    if (!term || term->getTermID() == trust::TermID::END)
        return nullptr;

    ParserToken::Kind kind = mapTermID(term->getTermID());
    // Для блоков (ScopeBlock) text() узла AST — это метка области, а не тело.
    // Метка/namespace единообразно хранится в m_text (правила block_all: ns { }, ::ns::name { }, :: { }).
    std::string text = term->getText();
    // TypeName хранит ':' в начале (":Int8") — срезаем, т.к. реестр типов
    // и CppTranspiler ожидают имя без двоеточия.
    if (kind == ParserToken::Kind::TypeName && !text.empty() && text[0] == ':')
        text.erase(0, 1);
    if (!text.empty() && text.back() == '^') {
        if (canHaveImmutableQualifier(term, kind)) {
            // Иммутабельность сохраняется в имени Term с суффиксом '^'; срезаем его
            // здесь, при конвертации в AstNode (признак уходит в attr::Const).
            text.pop_back();
        } else {
            // Признак иммутабельности неприменим к меткам блоков и областям имён —
            // наличие '^' в метке/имени области является ошибкой синтеза.
            ctx.diag().report(Severity::Error, term->m_mapperRange, "Immutable qualifier '^' is not applicable in block labels or namespaces");
            text.pop_back();
        }
    }

    // Build children recursively — collect from m_block, m_args, m_left, m_right
    std::vector<AstNodePtr> body;
    convertTermToSeq(term, body, ctx);

    // Именованный аргумент: ARGUMENT-обёртка (m_left=имя, m_right=значение)
    // → Binary("=") с left=имя, right=значение (аналог Python ast.keyword).
    // Имя сохраняется в левом поддереве, а не теряется при конвертации.
    AstNodePtr node;
    if (term->getTermID() == trust::TermID::MODULE && term->isCall()) {
        // Вызов модуля \module(func): встраиваем тело загруженного модуля как ModuleNode.
        auto idx = ctx.loader().indexOf(text);
        if (!idx || !ctx.loader().isLoaded(*idx)) {
            ctx.diag().report(Severity::Error, term->m_mapperRange, "Module '{}' is not loaded", text);
            node = std::make_shared<AstNodeAttr>(ParserToken::Kind::Ident, term);
        } else {
            auto mn = std::make_shared<ModuleNode>(*idx, text, term);
            for (const auto& child : ctx.loader().preprocessed(*idx)) {
                if (child)
                    mn->m_body.push_back(child);
            }
            node = std::move(mn);
        }
    } else if (term->getTermID() == trust::TermID::ARGUMENT && term->m_left && term->m_right) {
        AstNodePtr left;
        AstNodePtr right;
        if (body.size() >= 1 && body[0])
            left = body[0];
        if (body.size() >= 2 && body[1])
            right = body[1];
        node = std::make_shared<Binary>(ParserToken::Kind::AssignOp, term, std::move(left), std::move(right));
    } else if (term->getTermID() == trust::TermID::CREATE_TYPE && term->m_left && term->m_left->getTermID() == trust::TermID::NATIVE) {
        // Функция: %name(params):Type ::= { body } (legacy native слева от ::=)
        //
        // Диапазон statement'а функции — [имя, оператор]: левая граница из m_left (имя),
        // правая — конец оператора ::= (term->m_mapperRange). Тело ({ ... }, m_right) сюда
        // НЕ включается: оно маппится отдельно из FuncDecl::blockRange() в
        // generateFuncDeclToFile, чтобы скобки тела отображались в C++.
        if (!term->m_left->m_mapperRange.isInvalid() && !term->m_mapperRange.isInvalid() &&
            term->m_left->m_mapperRange.begin.fileIdx() == term->m_mapperRange.end.fileIdx() &&
            term->m_left->m_mapperRange.begin.offset() <= term->m_mapperRange.end.offset()) {
            term->m_mapperRange = MapperRange{term->m_left->m_mapperRange.begin, term->m_mapperRange.end};
        }
        auto fd = std::make_shared<FuncDecl>(std::string(term->m_left->getText()), term);
        if (term->m_left->m_type)
            fd->m_type = makeNode(term->m_left->m_type, ctx);
        fd->m_params = std::vector<AstNodePtr>{};
        fd->m_body = std::vector<AstNodePtr>{};
        // Параметры: m_args на NATIVE-терме (ARGUMENT: имя в m_left, тип в m_right)
        if (term->m_left->m_args) {
            for (const auto& [name, argTerm] : *term->m_left->m_args) {
                if (!argTerm)
                    continue;
                auto pd = std::make_shared<ParamDecl>(name, argTerm);
                if (argTerm->getTermID() == trust::TermID::ARGUMENT && argTerm->m_right)
                    pd->m_type = makeNode(argTerm->m_right, ctx);
                else if (argTerm->m_type)
                    pd->m_type = makeNode(argTerm->m_type, ctx);
                fd->m_params->push_back(std::move(pd));
            }
        }
        // Тело: m_right — block
        if (term->m_right) {
            std::vector<AstNodePtr> fnBody;
            convertTermToSeq(term->m_right, fnBody, ctx);
            fd->m_body = std::move(fnBody);
        }
        node = std::move(fd);
    } else if (term->getTermID() == trust::TermID::CREATE_NAME) {
        // x := expr  /  x:Type := expr
        // Legacy: m_left = lval (имя; тип в m_left->m_type), m_right = инициализатор.
        // body[0] = имя (из m_left), body[1] = инициализатор (из m_right).
        std::string name = term->m_left ? std::string(term->m_left->getText()) : text;
        // Терм оператора ':=' несёт range только оператора; расширяем до полного охвата
        // left/right (левая граница = имя) — общий хелпер для составных узлов.
        expandTermRangeToChildren(term);
        AstNodePtr init;
        if (term->m_right) {
            if (body.size() >= 2 && body[1])
                init = body[1];
            else if (body.size() >= 1 && body[0])
                init = body[0];
        }
        auto vd = std::make_shared<VarDecl>(name, term);
        if (term->m_left && term->m_left->m_type) {
            vd->m_type = makeNode(term->m_left->m_type, ctx);
        }
        vd->m_initializer = std::move(init);
        node = std::move(vd);
    } else {
        node = makeNodeForKind(term, kind, text, std::move(body));
    }

    convertAttrsToNode(term, node, ctx);
    return node;
}

static void convertTermToSeq(const trust::TermPtr& term, std::vector<AstNodePtr>& out, Context& ctx) {
    if (!term || term->getTermID() == trust::TermID::END)
        return;

    // ── m_block (children stored in a vector, e.g. members of a block) ──
    for (const auto& child : term->m_block) {
        AstNodePtr cn = makeNode(child, ctx);
        if (cn)
            out.push_back(std::move(cn));
    }

    // ── m_args (named arguments, e.g. function call arguments) ──
    if (term->m_args) {
        for (const auto& [name, argTerm] : *term->m_args) {
            (void)name; // Имя аргумента уже сохранено в ARGUMENT-обёртке (m_left)
            if (!argTerm || argTerm->getTermID() == trust::TermID::END)
                continue;
            AstNodePtr an = makeNode(argTerm, ctx);
            if (an)
                out.push_back(std::move(an));
        }
    }

    // ── m_left ──
    // Для INT_PLUS/INT_MINUS m_left не обходим: namespace (label) живёт в m_text
    // и попадает в JumpStmt::m_label в makeNode, а не в тело узла.
    if (term->m_left && term->getTermID() != trust::TermID::INT_PLUS && term->getTermID() != trust::TermID::INT_MINUS) {
        AstNodePtr ln = makeNode(term->m_left, ctx);
        if (ln)
            out.push_back(std::move(ln));
    }

    // ── m_right (chained list) ──
    if (term->m_right) {
        // Walk the m_right chain (singly-linked list of terms)
        trust::TermPtr cur = term->m_right;
        while (cur && cur->getTermID() != trust::TermID::END) {
            AstNodePtr rn = makeNode(cur, ctx);
            if (rn)
                out.push_back(std::move(rn));
            cur = cur->m_right;
        }
    }
}

std::vector<AstNodePtr> termToAst(const trust::TermPtr& term, Context& ctx) {
    std::vector<AstNodePtr> result;
    if (!term)
        return result;

    AstNodePtr root = makeNode(term, ctx);
    if (root)
        result.push_back(std::move(root));
    return result;
}

} // namespace trust