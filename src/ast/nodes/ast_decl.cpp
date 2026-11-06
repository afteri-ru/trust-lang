// Объявления AST: Decl, VarDecl, FuncDecl, DestructureDecl.
#include "ast/ast_nodes.hpp"
#include "ast/nodes/ast_helpers.hpp"
#include "ast/term_to_ast.hpp"
#include "ast/token_type.hpp"
#include "ast/convert_services.hpp"
#include "syntax/term/term.h"
#include "types/call.hpp"
#include "types/visibility.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
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

// C/C++-имя нативной цели без ведущего '%' (`%abs`→"abs", `%std::sqrt`→"std::sqrt").
static std::string nativeTargetName(const TermPtr& term) {
    EXPECT(term && "native target requires a source Term");
    const std::string name(utils::stripNativePrefix(term->getText()));
    EXPECT(!name.empty() && "native target requires a non-empty C/C++ symbol name");
    return name;
}

// Нативная привязка-импорт: ABI выводится из имени (cppLinkageOf) в точке кодогена.

// True, если инициализатор переменной — форма псевдонима-переадресации `f ...`
// (CallExpr с единственным аргументом Ellipsis): для переменной недопустима, алиас имени — `:-`.
static bool isEllipsisAliasInitializer(const AstNodePtr& init) {
    if (!init || init->kind() != ParserToken::Kind::CallExpr) {
        return false;
    }
    const auto* call = init->as<CallExpr>();
    if (call == nullptr || !call->m_callee || call->m_callee->kind() != ParserToken::Kind::Ident) {
        return false;
    }
    if (!call->m_args || call->m_args->size() != 1) {
        return false;
    }
    const auto& arg = (*call->m_args)[0];
    return arg && arg->kind() == ParserToken::Kind::Ellipsis;
}

VarDecl::VarDecl(TermPtr term, AstNodePtr type, AstNodePtr initializer, AttrPool* pool)
: Decl(ParserToken::Kind::VarDecl, term, declNameFromTerm(term), pool)
, m_initializer(std::move(initializer)) {
    EXPECT(term && "VarDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::VarDecl;
    m_type = std::move(type);
    m_nameRange = term->m_left ? term->m_left->m_mapperRange : MapperRange{};
}

VarDecl::VarDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx)
: VarDecl(term, nullptr, nullptr, ctx ? &ctx->attrs() : nullptr) {
    if (ctx && term) {
        if (term->m_left && term->m_left->m_type) {
            m_type = convertChild(*ctx, term->m_left->m_type);
        }
        // Предварительное (forward) объявление `x:Type := ...;`: m_right - чистое многоточие
        // вместо инициализатора. m_initializer остаётся nullptr (forward declaration).
        if (isForwardEllipsisTerm(term->m_right)) {
            return;
        }
        // Нативная привязка-импорт переменной: `x:Type := %cpp ...;` (многоточие = цель,
        // как у функций). Реализация/символ — внешний, значения в trust-коде нет.
        if (term->m_right && term->m_right->getTermID() == trust::TermID::NATIVE && term->m_right->m_right &&
            term->m_right->m_right->getTermID() == trust::TermID::ELLIPSIS) {
            m_pendingNativeCppName = nativeTargetName(term->m_right);
            m_bindsNativeCpp = true;
            return;
        }
        if (term->m_right) {
            std::vector<AstNodePtr> body;
            convertChildren(*ctx, term, body);
            if (body.size() >= 2 && body[1]) {
                m_initializer = std::move(body[1]);
            } else if (body.size() >= 1 && body[0]) {
                m_initializer = std::move(body[0]);
            }
        }
        if (isEllipsisAliasInitializer(m_initializer)) {
            ctx->diag().report(Severity::Error, m_initializer->range(), "variable '{}' cannot forward an alias with '...'; use ':-' for a name alias", text());
            m_initializer.reset();
        }
        if (m_initializer) {
            const MapperRange n = nameRange();
            const MapperRange init = m_initializer->range();
            if (!n.isInvalid() && !init.isInvalid() && n.begin.fileIdx() == init.end.fileIdx() && n.begin.offset() <= init.end.offset()) {
                setRange(MapperRange{n.begin, init.end});
            }
        }
    }
}

DestructureDecl::DestructureDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx)
: AstNodeAttr(ParserToken::Kind::DestructureDecl, term) {
    EXPECT(term && "DestructureDecl term-constructor requires a source Term");
    if (!ctx || !term) {
        return;
    }
    m_isAssign = term->getTermID() == trust::TermID::ASSIGN;
    m_isSpread = term->m_right && term->m_right->getTermID() == trust::TermID::ELLIPSIS;
    if (term->m_left) {
        for (const TermPtr* t = &term->m_left; *t; t = &(*t)->m_left) {
            // Цель - имя переменной из терма lval; IdentName конструируем напрямую (convertChild
            // для lval-терма может дать не тот kind). rest-цель (`rest...`) - lval с суффиксом
            // ELLIPSIS в m_right (грамматика assign_item: `lval ELLIPSIS`). Явная аннотация типа
            // цели (`a:Int32`) хранится в lval->m_type (грамматика `lval_var: name type_item`
            // → m_type = type_item); конвертируем её в узел типа для семантики/кодгена.
            const bool isRest = (*t)->m_right && (*t)->m_right->getTermID() == trust::TermID::ELLIPSIS;
            auto target = std::make_shared<IdentName>(std::string((*t)->getText()), &ctx->attrs());
            if (!target->text().empty() && target->text().front() == '%') {
                ctx->diag().report(Severity::Error, target->range(),
                                   "native name '{}' cannot be declared: a destructuring declaration creates trust identifiers only", target->text());
            }
            Target entry;
            entry.name = std::move(target);
            entry.typeNode = (*t)->m_type ? convertChild(*ctx, (*t)->m_type) : AstNodePtr{};
            entry.isRest = isRest;
            m_targets.push_back(std::move(entry));
        }
    }
    TermPtr src;
    if (m_isSpread) {
        if (term->m_right && term->m_right->m_right) {
            src = term->m_right->m_right;
        }
    } else {
        src = term->m_right;
    }
    if (src) {
        m_source = convertChild(*ctx, src);
    }
    const bool beginOk = term->m_left && !term->m_left->m_mapperRange.isInvalid();
    const bool endOk = m_source && !m_source->range().isInvalid();
    if (beginOk && endOk && term->m_left->m_mapperRange.begin.fileIdx() == m_source->range().end.fileIdx() &&
        term->m_left->m_mapperRange.begin.offset() <= m_source->range().end.offset()) {
        setRange(MapperRange{term->m_left->m_mapperRange.begin, m_source->range().end});
    }
}

std::string DestructureDecl::dump(size_t indent) const {
    std::string result(indent, ' ');
    result += "DestructureDecl";
    if (m_isAssign) {
        result += " (assign)";
    }
    result += "\n";
    for (const Target& target : m_targets) {
        result += std::string(indent + 2, ' ') + "target: " + (target.name ? std::string(requiredText(*target.name)) : std::string{}) +
                  (target.isRest ? "..." : "") + "\n";
    }
    if (m_source) {
        result += m_source->dump(indent + 2);
    }
    return result;
}

FuncDecl::FuncDecl(TermPtr term, AttrPool* pool)
: Decl(ParserToken::Kind::FuncDecl, term, declNameFromTerm(term), pool) {
    EXPECT(term && "FuncDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::FuncDecl;
    m_blockRange = term->m_right ? term->m_right->m_mapperRange : MapperRange{};
    if (term->getTermID() == trust::TermID::CREATE_NAME && term->m_left && term->m_left->isCall() && !term->m_left->m_mapperRange.isInvalid() &&
        !term->m_mapperRange.isInvalid() && term->m_left->m_mapperRange.begin.fileIdx() == term->m_mapperRange.end.fileIdx() &&
        term->m_left->m_mapperRange.begin.offset() <= term->m_mapperRange.end.offset()) {
        setRange(MapperRange{term->m_left->m_mapperRange.begin, term->m_mapperRange.end});
    }
    // Оператор-объявление — обычная функция с СИСТЕМНЫМ именем (`__eq__`, `__getitem__`, ...).
    // Признак производный от имени (единый реестр types/operator_registry.hpp).
    m_isOperator = op::isOperatorFunctionName(text());
}

FuncDecl::FuncDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx)
: FuncDecl(term, ctx ? &ctx->attrs() : nullptr) {
    if (!ctx || !term) {
        return;
    }
    // Лямбда-выражение `[captures](params):Ret { body }` (TermID::LAMBDA): m_sequence - захваты,
    // m_args - параметры, m_right - тело, m_type - тип возврата. Имя не регистрируется (семантика).
    if (term->getTermID() == trust::TermID::LAMBDA) {
        // Внутренняя синтетическая метка-имя лямбды: именованный возврат
        // (`@return v;` → `@__FUNCTION__ ++ v ++`) и валидация метки работают КАК У ОБЫЧНОЙ
        // ФУНКЦИИ (имя = метка возврата). Имя НЕ регистрируется в скоупе и НЕ эмитится как
        // C++-функция (кодоген идёт через emitLambdaExpr). Детерминировано по позиции исходника.
        if (!term->m_mapperRange.isInvalid()) {
            const auto& b = term->m_mapperRange.begin;
            set_text_interned("__lambda__" + std::to_string(b.fileIdx().as_index()) + "_" + std::to_string(b.offset()));
        } else {
            set_text_interned("__lambda__");
        }
        m_params = std::vector<AstNodePtr>{};
        // Параметры: как у функции - m_args (ARGUMENT / типизированное имя `x:Type`).
        if (term->m_args) {
            for (const auto& [pname, argTerm] : *term->m_args) {
                (void)pname;
                if (!argTerm) {
                    continue;
                }
                m_params->push_back(std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, argTerm, ctx));
            }
        }
        // Захваты: ТОЛЬКО имена, ТОЛЬКО по значению (копия). Иные формы - явная диагностика.
        m_captures = std::vector<AstNodePtr>{};
        for (const auto& cterm : term->m_sequence) {
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
        if (term->m_type) {
            m_type = convertChild(*ctx, term->m_type);
        }
        // Тело.
        if (term->m_right) {
            std::vector<AstNodePtr> fnBody;
            convertChildren(*ctx, term->m_right, fnBody);
            m_definition = Definition::fromBody(std::move(fnBody));
        }
        return;
    }
    // CREATE_NAME (`:=`) с сигнатурой функции в m_left (m_left->isCall()) - это функция.
    const bool sigInLeft = term->getTermID() == trust::TermID::CREATE_NAME && term->m_left && term->m_left->isCall();
    if (sigInLeft) {
        if (term->m_left->m_type) {
            m_type = convertChild(*ctx, term->m_left->m_type);
        }
        m_params = std::vector<AstNodePtr>{};
        // Параметры: m_args на сигнатуре (ARGUMENT: имя в m_left, тип в m_right).
        // Раскладка m_type - в ArgNode-конструкторе.
        if (term->m_left->m_args) {
            for (const auto& [name, argTerm] : *term->m_left->m_args) {
                (void)name;
                if (!argTerm) {
                    continue;
                }
                m_params->push_back(std::make_shared<ArgNode>(ParserToken::Kind::ArgNode, argTerm, ctx));
            }
        }
        // Нативная привязка-импорт `<name>(...) := %cpp ...;` - АЛИАС имени на внешний
        // C/C++-символ: C++-функция НЕ эмитится, вызовы переписываются на цель.
        // Нативное имя (без '%') - целевой факт хранилища `DeclCppFacts` (R1).
        if (isNativeImportTerm(term->m_right)) {
            m_pendingNativeCppName = nativeTargetName(term->m_right);
            m_bindsNativeCpp = true;
            return;
        }
        // Тело: m_right - block. Предварительное объявление `f():Type := ...;` -
        // m_right - чистое многоточие вместо тела → m_body = nullopt (forward declaration).
        if (isForwardEllipsisTerm(term->m_right)) {
            return;
        }
        if (term->m_right) {
            std::vector<AstNodePtr> fnBody;
            convertChildren(*ctx, term->m_right, fnBody);
            m_definition = Definition::fromBody(std::move(fnBody));
        }
    } else {
        // Lambda/iterator: split params (ArgNode) / body из convertChildren.
        std::vector<AstNodePtr> body;
        convertChildren(*ctx, term, body);
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
            m_definition = Definition::fromBody(std::move(fnBody));
        }
    }
}

std::string Decl::dump(size_t indent) const {
    // Имя выводится один раз (ident_name/dump даёт "Kind 'text'").
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

std::string FuncDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_aliasKind != AliasKind::None) {
        result += " alias:";
        result += m_aliasKind == AliasKind::Name ? "Name" : "Namespace";
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
            result += requiredText(*p);
            if (p->is<ArgNode>() && p->as<ArgNode>()->isTypePack) {
                result += "...";
            }
        }
        result += ">";
    }
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
    if (const auto* fb = body()) {
        result += "\n";
        result += std::string(indent, ' ');
        result += "body:";
        for (size_t i = 0; i < fb->size(); ++i) {
            result += "\n";
            const auto& stmt = (*fb)[i];
            if (stmt) {
                result += stmt->dump(indent + 2);
            } else {
                result += std::string(indent + 2, ' ') + "(null)";
            }
        }
    } else if (m_aliasKind != AliasKind::None) {
        result += " (alias)";
    } else if (const std::string* fwd = forwardingTarget()) {
        result += " (forwarding to " + *fwd + ")";
    } else if (bindsNativeCpp()) {
        result += " (native)";
    } else {
        result += " (forward)";
    }
    return result;
}

void FuncDecl::setAlias(AliasKind kind, AstNodePtr target, MapperRange range) {
    EXPECT(((kind == AliasKind::None) == (target == nullptr)) && "alias kind and target must agree");
    m_aliasKind = kind;
    m_aliasTarget = std::move(target);
    setRange(range);
}

std::string FuncDecl::signature(std::string_view namespace_path) const {
    std::string name(text());
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
                sig += requiredText(*pd.m_type);
            }
        }
    }
    sig += ")";
    if (m_type) {
        sig += ":";
        sig += requiredText(*m_type);
    }
    return sig;
}

std::string VarDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (m_initializer) {
        dumpLabeled(result, indent, "init", m_initializer);
        return result;
    }
    switch (initKind()) {
    case VarInitKind::Forward:
        result += " (forward)";
        break;
    case VarInitKind::Bound:
        result += " (bound)";
        break;
    default:
        result += " (no init)";
        break;
    }
    return result;
}

// Маркер варианта наследования в списке баз — терм, оканчивающийся на `::` (`::`/`_::`/`__::`);
// базис всегда начинается с `:` или `%`, поэтому любое иное `...::` — НЕВАЛИДНЫЙ маркер.
// Признак читается по ТЕКСТУ (не по TermID): `class_type_def` перезаписывает `m_id` ГОЛОВНОГО
// терма на CLASS, поэтому у маркера-головы TermID теряется, а текст сохраняется.
static bool applyVisibilityMarker(const TermPtr& baseTerm, Visibility& access, ConvertServices* ctx) {
    if (!baseTerm) {
        return false;
    }
    const std::string_view text = baseTerm->getText();
    if (text.size() < 2 || !text.ends_with("::")) {
        return false;
    }
    if (const auto marker = visibilityOf(text)) {
        access = *marker;
        return true;
    }
    if (ctx != nullptr) {
        ctx->diag().report(Severity::Error, baseTerm->m_mapperRange,
                           "invalid inheritance access marker '{}' (expected '::' (public), '_::' (protected), '__::' (private))", text);
    }
    return true;
}

// Члены тела класса с трекингом маркера видимости. Standalone-терм `::`/`_::`/`__::`
// (TermID::NAMESPACE) задаёт уровень для ВСЕХ последующих членов и в тело класса не попадает;
// каждый реальный член конвертируется и получает текущий уровень на узле
// (`AstNodeBase::visibility` - тот же `Visibility`, что у маркеров области имён и наследования).
// Дефолт — Public. Иное имя-область (`foo::`) — явная ошибка (как у маркеров наследования).
static std::vector<AstNodePtr> convertClassMembers(const std::vector<TermPtr>& members, ConvertServices& ctx) {
    std::vector<AstNodePtr> result;
    Visibility access = Visibility::Public;
    for (const TermPtr& term : members) {
        if (!term || term->getTermID() == trust::TermID::END) {
            continue;
        }
        if (term->getTermID() == trust::TermID::NAMESPACE) {
            if (const auto level = visibilityOf(term->getText())) {
                access = *level;
                continue;
            }
            ctx.diag().report(Severity::Error, term->m_mapperRange,
                              "invalid member access marker '{}' (expected '::' (public), '_::' (protected), '__::' (private))", term->getText());
            continue;
        }
        if (AstNodePtr node = convertChild(ctx, term)) {
            node->visibility = access;
            result.push_back(std::move(node));
        }
    }
    return result;
}

ClassDecl::ClassDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx)
: Decl(ParserToken::Kind::ClassDecl) {
    EXPECT(term && "ClassDecl term-constructor requires a source Term");
    m_kind = ParserToken::Kind::ClassDecl;
    // trust-имя класса - из m_left операторного терма `::=` (`Pair`). Типовые параметры
    // шаблона (`<T1,T2>`) template_prefix кладёт в m_template САМОГО терма `::=`.
    if (term->m_left) {
        assignNameFromRaw(*this, *this, ParserToken::Kind::ClassDecl, term->m_left->getText(), ctx ? &ctx->attrs() : nullptr);
    }
    if (term->m_template) {
        m_templateParams.emplace();
        for (const auto& [pname, pterm] : *term->m_template) {
            (void)pname;
            if (pterm) {
                m_templateParams->push_back(std::make_shared<ArgNode>(pterm, nullptr, nullptr, ctx ? &ctx->attrs() : nullptr));
            }
        }
    }
    // RHS CLASS-терм (native_class_fwd): нативное имя (текст терма), параметры реализации, члены тела.
    const TermPtr rhs = term->m_right;
    if (rhs) {
        setRange(rhs->m_mapperRange); // охват RHS (связывание/члены), Term не удерживается
        // RHS-терм САМ является именем (native_class_fwd: `$$ = $1` = имя), текст - связывание.
        std::string binding(utils::stripNativePrefix(rhs->getText()));
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
        // НЕшаблонный native-класс с явными C++-аргументами: аргументы - часть C++-имени.
        // В шаблонной форме (m_templateParams задан) они применяются при инстанциации.
        if (!m_templateParams.has_value() && m_templateArgs.has_value() && !binding.empty()) {
            binding += '<';
            for (size_t i = 0; i < m_templateArgs->size(); ++i) {
                if (i != 0) {
                    binding += ',';
                }
                binding += (*m_templateArgs)[i];
            }
            binding += '>';
        }
        if (!rhs->getText().empty() && rhs->getText().front() == '%' && !binding.empty()) {
            m_pendingNativeCppName = std::move(binding);
            m_bindsNativeCpp = true;
        } else if (ctx != nullptr) {
            // Без `%` RHS — не нативное имя; форма неотличима от обычного, поэтому ошибка в точке разбора.
            ctx->diag().report(Severity::Error, rhs->m_mapperRange,
                               "native class '{}' requires a '%'-prefixed C++ name on the right: 'Name ::= %cpp {{ ... }};'", text());
        }
        // БАЗИСЫ генерируемого класса: RHS-цепочка после C++-имени (`Name = %cpp, %base, :Base`).
        // Нативное имя (`%std::string`) — готовый C++-базис (НЕ тип-ссылка, не резолвится);
        // обычный тип (`:Base`) — IdentType (резолвится семантикой). ЧЛЕНЫ тела — только m_sequence
        // (цепочку m_right как дети НЕ конвертируем: там базисы, а не члены).
        // Вариант наследования действует на все последующие базы до нового маркера.
        // Дефолт для ВСЕХ типов (record и класс) — public; private/protected задаются явным маркером.
        Visibility baseAccess = Visibility::Public;
        for (TermPtr baseTerm = rhs->m_right; baseTerm && baseTerm->getTermID() != trust::TermID::END; baseTerm = baseTerm->m_right) {
            if (applyVisibilityMarker(baseTerm, baseAccess, ctx)) {
                continue;
            }
            const std::string_view baseText = baseTerm->getText();
            if (!baseText.empty() && baseText.front() == '%') {
                m_nativeBaseCppNames.push_back(NativeBase{std::string(baseText.substr(1)), baseAccess});
                continue;
            }
            auto baseNode = std::make_shared<IdentType>(baseTerm, std::nullopt, std::nullopt, ctx ? &ctx->attrs() : nullptr);
            if (baseTerm->m_template.has_value()) {
                std::vector<TemplateArg> targs;
                for (const auto& [aname, argTerm] : *baseTerm->m_template) {
                    (void)aname;
                    if (argTerm && ctx) {
                        targs.push_back(TemplateArg{convertChild(*ctx, argTerm), false});
                    }
                }
                baseNode->setTemplateArgs(std::move(targs));
            }
            m_baseTypes.push_back(TypeBase{std::move(baseNode), baseAccess});
        }
        if (ctx) {
            // Члены тела — ТОЛЬКО m_sequence (базы в m_right выше уже прочитаны).
            m_body = convertClassMembers(rhs->m_sequence, *ctx);
        }
    }
}

std::string ClassDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
    if (bindsNativeCpp()) {
        result += " -> (native)";
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
            result += requiredText(*p);
            if (p->is<ArgNode>() && p->as<ArgNode>()->isTypePack) {
                result += "...";
            }
        }
        result += ">";
    }
    Sequence::dumpBody(result, m_body, indent, indent + 2);
    return result;
}

// Единый узел: Struct (`:Struct`-база) и Class (`:Class`/user-база) отличаются только базой.
RecordDecl::RecordDecl(ParserToken::Kind /*k*/, TermPtr term, ConvertServices* ctx)
: Decl(ParserToken::Kind::StructDecl) {
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
        assignNameFromRaw(*this, *this, ParserToken::Kind::StructDecl, name, ctx ? &ctx->attrs() : nullptr);
        // Типовые параметры шаблона (`<T> Name ::= ...`) - в m_template терма `::=`.
        if (term->m_template) {
            m_templateParams.emplace();
            for (const auto& [pname, pterm] : *term->m_template) {
                (void)pname;
                if (pterm) {
                    m_templateParams->push_back(std::make_shared<ArgNode>(pterm, nullptr, nullptr, ctx ? &ctx->attrs() : nullptr));
                }
            }
        }
    }

    if (!rhs) {
        return;
    }
    setRange(rhs->m_mapperRange); // охват баз/тела, Term не удерживается

    // Базы: CLASS-терм = первая база (текст `:Struct`/`:Class`/`:Base`), её m_right - остальные.
    // База-тип с типовыми аргументами (`:Base<:T>`) обязана сохранить m_template: manual-конструктор
    // IdentType(term) его не читает, поэтому типовые аргументы переносим явно (как visit_TYPE).
    // Маркер варианта наследования (standalone-элемент списка) действует на все последующие базы
    // до нового маркера; дефолт для ВСЕХ типов — public (private/protected — явным маркером).
    Visibility baseAccess = Visibility::Public;
    for (TermPtr base = rhs; base && base->getTermID() != trust::TermID::END; base = base->m_right) {
        if (applyVisibilityMarker(base, baseAccess, ctx)) {
            continue;
        }
        auto baseNode = std::make_shared<IdentType>(base, std::nullopt, std::nullopt, ctx ? &ctx->attrs() : nullptr);
        if (base->m_template.has_value()) {
            std::vector<TemplateArg> targs;
            for (const auto& [aname, argTerm] : *base->m_template) {
                (void)aname;
                if (argTerm && ctx) {
                    targs.push_back(TemplateArg{convertChild(*ctx, argTerm), false});
                }
            }
            baseNode->setTemplateArgs(std::move(targs));
        }
        m_baseTypes.push_back(TypeBase{std::move(baseNode), baseAccess});
    }

    // Предварительное (forward) объявление - форма БЕЗ тела `:Name ::= :Base ...;`: грамматика
    // кладёт маркер ELLIPSIS в m_sequence вместо членов → m_body = nullopt. Тело `{ ... }`
    // (в т.ч. пустое `{ }`) - полное определение (engaged, возможно пустой вектор).
    const bool forward = rhs->m_sequence.size() == 1 && rhs->m_sequence.front() && rhs->m_sequence.front()->getTermID() == trust::TermID::ELLIPSIS;

    // Члены: class_props разложены в m_sequence (поля/методы). Для forward-объявления членов нет.
    if (!forward) {
        std::vector<AstNodePtr> members;
        if (ctx) {
            members = convertClassMembers(rhs->m_sequence, *ctx);
        }
        m_definition = Definition::fromBody(std::move(members));
    }
}

std::string RecordDecl::dump(size_t indent) const {
    std::string result = Decl::dump(indent);
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
            result += requiredText(*p);
            if (p->is<ArgNode>() && p->as<ArgNode>()->isTypePack) {
                result += "...";
            }
        }
        result += ">";
    }
    if (!m_baseTypes.empty()) {
        result += " :";
        for (const auto& b : m_baseTypes) {
            if (!b.type) {
                continue;
            }
            result += " ";
            result += std::string(visibilityKeyword(b.access));
            result += " ";
            result += std::string(requiredText(*b.type));
        }
    }
    if (!m_definition.hasBody()) {
        result += " (forward)";
        return result;
    }
    Sequence::dumpBody(result, *m_definition.body(), indent, indent + 2);
    return result;
}
} // namespace trust
