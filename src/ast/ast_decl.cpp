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
    // Оператор-МЕТОД/свободный оператор: имя-СИМВОЛ в обратных кавычках (лексема REFLECTION) -
    // грамматика кладёт REFLECTION-терм в m_left оператора `:=` (operator_sig, parser.y.in).
    // Текст = символ КАК ЕСТЬ: normalizeTermText здесь неприменим (срезает хвостовой '^' и
    // маркеры имён), а символ оператора обязан сохраниться дословно (`` `[]` ``, `` `==` ``).
    if (m_term->m_left && m_term->m_left->getTermID() == trust::TermID::REFLECTION) {
        m_isOperator = true;
        set_text(std::string(m_term->m_left->getText()));
        return;
    }
    m_text = normalizeTermText(ParserToken::Kind::FuncDecl, declNameFromTerm(m_term));
}

// -- FuncDecl: uniform терм-конструктор (kind = FuncDecl). --
FuncDecl::FuncDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: FuncDecl(std::move(term)) {
    if (!ctx || !m_term) {
        return;
    }
    // Лямбда-выражение `[captures](params):Ret { body }` (TermID::LAMBDA): m_sequence - захваты,
    // m_args - параметры, m_right - тело, m_type - тип возврата. Имя не регистрируется (семантика).
    if (m_term->getTermID() == trust::TermID::LAMBDA) {
        // Внутренняя синтетическая метка-имя лямбды: именованный возврат
        // (`@return v;` → `@__FUNCTION__ ++ v ++`) и валидация метки работают КАК У ОБЫЧНОЙ
        // ФУНКЦИИ (имя = метка возврата). Имя НЕ регистрируется в скоупе и НЕ эмитится как
        // C++-функция (кодоген идёт через emitLambdaExpr). Детерминировано по позиции исходника.
        if (!m_term->m_mapperRange.isInvalid()) {
            const auto& b = m_term->m_mapperRange.begin;
            set_text("__lambda__" + std::to_string(b.fileIdx().as_index()) + "_" + std::to_string(b.offset()));
        } else {
            set_text("__lambda__");
        }
        m_params = std::vector<AstNodePtr>{};
        // Параметры: как у функции - m_args (ARGUMENT / типизированное имя `x:Type`).
        if (m_term->m_args) {
            for (const auto& [pname, argTerm] : *m_term->m_args) {
                (void)pname;
                if (!argTerm) {
                    continue;
                }
                m_params->push_back(std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, argTerm, ctx));
            }
        }
        // Захваты: ТОЛЬКО имена, ТОЛЬКО по значению (копия). Иные формы - явная диагностика.
        m_captures = std::vector<AstNodePtr>{};
        for (const auto& cterm : m_term->m_sequence) {
            if (!cterm) {
                continue;
            }
            if (cterm->getTermID() == trust::TermID::OPERATOR_PTR) {
                ctx->diag().report(Severity::Error, cterm->m_mapperRange, "reference capture is not supported; only capture-by-value '[name]' is allowed");
                continue;
            }
            if (cterm->getTermID() == trust::TermID::NAME && !cterm->m_type && !cterm->m_right && !cterm->m_args) {
                // ArgNode с термом - сохраняет диапазон исходника (позиция в диагностике).
                m_captures->push_back(std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, cterm, ctx));
                continue;
            }
            ctx->diag().report(Severity::Error, cterm->m_mapperRange,
                               "unsupported lambda capture; only a plain variable name '[name]' (capture-by-value) is allowed");
        }
        // Тип возврата (опционально).
        if (m_term->m_type) {
            m_type = convertChild(*ctx, m_term->m_type);
        }
        // Тело.
        if (m_term->m_right) {
            std::vector<AstNodePtr> fnBody;
            convertChildren(*ctx, m_term->m_right, fnBody);
            m_body = std::move(fnBody);
        }
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

// -- ClassDecl: forward-объявление (нативного) класса `Pair ::= %std::pair<T1,T2>{...};` --

ClassDecl::ClassDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: Decl("") {
    EXPECT(term && "ClassDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::ClassDecl;
    // trust-имя класса - из m_left операторного терма `::=` (`Pair`). Типовые параметры
    // шаблона (`<T1,T2>`) template_prefix кладёт в m_template САМОГО терма `::=`.
    if (term->m_left) {
        set_text(normalizeTermText(ParserToken::Kind::ClassDecl, term->m_left->getText()));
    }
    if (term->m_template) {
        m_templateParams.emplace();
        for (const auto& [pname, pterm] : *term->m_template) {
            (void)pterm;
            m_templateParams->push_back(std::make_shared<ArgNode>(pname));
        }
    }
    // RHS CLASS-терм (native_class_fwd): нативное имя (текст терма), параметры реализации, члены тела.
    const TermPtr rhs = term->m_right;
    if (rhs) {
        m_term = rhs; // range() указывает на RHS (связывание/члены)
        // RHS-терм САМ является именем (native_class_fwd: `$$ = $1` = имя), текст - связывание.
        std::string binding = std::string(rhs->getText());
        if (!binding.empty() && binding.front() == '%') {
            binding.erase(0, 1); // `%std::pair` → "std::pair" (нативное C++-имя)
        }
        m_nativeName = std::move(binding);
        // Шаблонность и generic-признак ВЫВОДЯТСЯ из m_template (см. isGenericTemplate()).
        // Здесь - аргументы реализации из явной формы `имя<T1,T2>`; обобщённая форма
        // `<T1,T2> имя ::= <T1,T2> %std::pair` m_templateArgs НЕ заполняет (остаётся nullopt).
        if (rhs->m_template) {
            m_templateArgs.emplace();
            for (const auto& [aname, aterm] : *rhs->m_template) {
                (void)aterm;
                m_templateArgs->push_back(aname);
            }
        }
        if (ctx) {
            convertChildren(*ctx, rhs, m_body);
        }
    }
}

std::string ClassDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (!m_nativeName.empty()) {
        result += " -> ";
        result += m_nativeName;
    }
    if (isGenericTemplate()) {
        result += " (generic-template)";
    }
    if (m_templateParams) {
        result += " <";
        bool first = true;
        for (const auto& p : *m_templateParams) {
            if (!p) {
                continue;
            }
            if (!first) {
                result += ",";
            }
            first = false;
            result += p->text();
        }
        result += ">";
    }
    Sequence::dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// -- RecordDecl: пользовательский Struct/Class `:Name ::= :Base{, :Base}{ ... };` --
// Единый узел: Struct (`:Struct`-база) и Class (`:Class`/user-база) отличаются только базой.
RecordDecl::RecordDecl(ParserToken::Kind /*k*/, TermPtr term, Context* ctx)
: Decl("") {
    EXPECT(term && "RecordDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::StructDecl;

    TermPtr rhs = term->m_right;
    if (term->getTermID() == trust::TermID::CLASS) {
        // Униформ-конструктор получил сам CLASS-терм (bare `:Base{...}` вне `::=`):
        // имени объявления нет — это не тип-объявление (не семантизируется как тип).
        rhs = term;
    } else if (term->m_left) {
        // trust-имя слева от `::=`. Допустимы обе формы (`Name` и `:Name`) — ведущий ':' срезается.
        std::string name(term->m_left->getText());
        if (!name.empty() && name.front() == ':') {
            name.erase(0, 1);
        }
        set_text(normalizeTermText(ParserToken::Kind::StructDecl, name));
        // Типовые параметры шаблона (`<T> Name ::= ...`) - в m_template терма `::=`.
        if (term->m_template) {
            m_templateParams.emplace();
            for (const auto& [pname, pterm] : *term->m_template) {
                (void)pterm;
                m_templateParams->push_back(std::make_shared<ArgNode>(pname));
            }
        }
    }

    if (!rhs) {
        return;
    }
    m_term = rhs; // range() указывает на базы/тело

    // Базы: CLASS-терм = первая база (текст `:Struct`/`:Class`/`:Base`), её m_right - остальные.
    // База-тип с типовыми аргументами (`:Base<:T>`) обязана сохранить m_template: manual-конструктор
    // IdentType(term) его не читает, поэтому типовые аргументы переносим явно (как visit_TYPE).
    for (TermPtr base = rhs; base && base->getTermID() != trust::TermID::END; base = base->m_right) {
        auto baseNode = std::make_shared<IdentType>(base);
        if (base->m_template.has_value()) {
            std::vector<AstNodePtr> targs;
            for (const auto& [aname, argTerm] : *base->m_template) {
                (void)aname;
                if (argTerm && ctx) {
                    targs.push_back(convertChild(*ctx, argTerm));
                }
            }
            baseNode->setTemplateArgs(std::move(targs));
        }
        m_baseTypes.push_back(std::move(baseNode));
    }

    // Предварительное (forward) объявление - форма БЕЗ тела `:Name ::= :Base ...;`: грамматика
    // кладёт маркер ELLIPSIS в m_sequence вместо членов → m_body = nullopt. Тело `{ ... }`
    // (в т.ч. пустое `{ }`) - полное определение (engaged, возможно пустой вектор).
    const bool forward = rhs->m_sequence.size() == 1 && rhs->m_sequence.front() && rhs->m_sequence.front()->getTermID() == trust::TermID::ELLIPSIS;

    // Члены: class_props разложены в m_sequence (поля/методы). Для forward-объявления членов нет.
    if (!forward) {
        m_body.emplace();
        if (ctx) {
            for (const auto& m : rhs->m_sequence) {
                if (!m || m->getTermID() == trust::TermID::END) {
                    continue;
                }
                if (AstNodePtr n = convertChild(*ctx, m)) {
                    m_body->push_back(std::move(n));
                }
            }
        }
    }
}

std::string RecordDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (!m_baseTypes.empty()) {
        result += " :";
        for (const auto& b : m_baseTypes) {
            if (!b) {
                continue;
            }
            result += " ";
            result += std::string(b->text());
        }
    }
    if (!m_body.has_value()) {
        result += " (forward)";
        return result;
    }
    Sequence::dumpBody(result, *m_body, indent, indent + 2);
    return result;
}
} // namespace trust
