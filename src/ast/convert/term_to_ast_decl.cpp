// Term->AST conversion (declaration conversions).

#include "ast/term_to_ast.hpp"
#include "ast/convert/term_to_ast_internal.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/convert_services.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/nodes/attr_parser.hpp"
#include "syntax/check_area.hpp"
#include "ast/ident_name.hpp"
#include "ast/token_base.hpp"
#include "ast/token_type.hpp"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "types/typekind.hpp"
#include "types/visibility.hpp"
#include "utils/error.hpp"

#include <memory>
#include <optional>
#include <type_traits>
#include <vector>

namespace trust {

AstNodePtr TermToAstConverter::visit_MODULE(const trust::TermPtr& term, ConvertServices& ctx) {
    // MODULE-терм — только сайт импорта `\path(...)` (`m_right` на MODULE-терме не производится:
    // модуль не является префиксом разрешения имён).
    if (!term->isCall()) {
        auto ident = std::make_shared<IdentName>(ParserToken::Kind::Ident, term, &ctx);
        if (!term->m_moduleIndices.empty()) {
            ident->moduleRef = term->m_moduleIndices.front();
        }
        return ident;
    }
    if (term->m_moduleIndices.empty()) {
        reportModuleNotLoaded(ctx.diag(), term->m_mapperRange, term->getText());
        // Узел-заглушка для имени модуля: kind Ident ⇒ класс IdentName (инвариант kind→класс).
        return std::make_shared<IdentName>(ParserToken::Kind::Ident, term, &ctx);
    }
    // Тело каждого файла конвертируется ОДИН раз (ModuleRegistry::m_bodyAst), сайт импорта несёт
    // список индексов + маски. Индекс ставит загрузчик в MODULE-терм при парсинге импорта.
    auto mn = std::make_shared<ModuleNode>(term->m_moduleIndices, term, /*isImport=*/true);
    // Аргументы оператора загрузки `\module(mod, masks)` - список масок фильтра
    // экспорт-интерфейса (glob `*`/`?`, через запятую = OR). Без аргументов - все экспорты.
    std::string masks;
    if (term->m_args) {
        for (const auto& [name, arg] : *term->m_args) {
            if (arg) {
                if (!masks.empty()) {
                    masks += ',';
                }
                masks += std::string(arg->getText());
            }
        }
    }
    mn->setImportMasks(std::move(masks));
    return mn;
}

// True, если RHS-терм `::=` - forward-объявление (нативного) класса: CLASS-терм, чей текст -
// ИМЯ (native `%std::pair` или голое имя), а НЕ type_class (`:Base`, начинается с ':')
// пользовательского класса (class_type_def). Нативный класс: `%`-имя → C++-имя без '%'.
// CLASS-терм пользовательского Record-типа: голова цепочки — БАЗА-тип (`:Struct`/`:Class`/`:Base`)
// либо маркер варианта наследования (`::`/`_::`/`__::`; class_type_def допускает его первым).
// Маркер-голова: известный маркер ИЛИ текст, оканчивающийся на `::` (невалидный маркер `foo::` —
// тоже форма списка баз record-типа, чтобы диагностику выдал RecordDecl, а не «нативный класс»).
static bool isUserRecordTerm(const trust::TermPtr& rhs) {
    if (!rhs || rhs->getTermID() != trust::TermID::CLASS) {
        return false;
    }
    const std::string_view text = rhs->getText();
    if (visibilityOf(text).has_value() || (text.size() >= 2 && text.ends_with("::"))) {
        return true;
    }
    return !text.empty() && text.front() == ':';
}

// Класс-терм forward-объявления (нативного) класса: голова цепочки — ИМЯ (native `%std::pair`
// либо голое имя), а НЕ тип-ссылка (`:`), см. isUserRecordTerm (class_type_def).
static bool isNativeClassFwdTerm(const trust::TermPtr& rhs) {
    return rhs && rhs->getTermID() == trust::TermID::CLASS && !isUserRecordTerm(rhs);
}

AstNodePtr TermToAstConverter::visit_CREATE_TYPE(const trust::TermPtr& term, ConvertServices& ctx) {
    // `::=` тип-определение. Forward-объявление (нативного) класса:
    // `Pair ::= %std::pair<T1,T2> { ... };` / `String ::= %std::string { };` - RHS CLASS-терм с
    // m_left = имя (native/голое) → right = ClassDecl (trust-имя слева, C++-имя из RHS,
    // типовые параметры, члены-интерфейс). Иначе - generic (алиас/enum/variant/польз. класс).
    if (isNativeClassFwdTerm(term->m_right)) {
        auto ncd = std::make_shared<ClassDecl>(ParserToken::Kind::ClassDecl, term, &ctx);
        // Атрибуты (@[include]) нативной декларации класса - на терме-имени `::=` (m_left),
        // куда template_prefix/name_attr положили их; конвертируем в атрибуты узла.
        if (term->m_left) {
            AstNodePtr ncdBase = ncd;
            detail::convertAttrsToNode(term->m_left, ncdBase, ctx);
        }
        // Тип-определение: left = trust-имя (Ident), right = ClassDecl.
        auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, term, &ctx);
        b->m_right = ncd;
        return b;
    }
    // Объявление пользовательского Record-типа (Struct/Class): `:Name ::= :Base{, :Base}{ ... };`
    // RHS - CLASS-терм с базой-типом (`:Struct`/`:Class`/`:Base`). Единый узел RecordDecl;
    // Struct vs Class определяется базой (семантика → Group TypeKind).
    if (isUserRecordTerm(term->m_right)) {
        auto rec = std::make_shared<RecordDecl>(ParserToken::Kind::StructDecl, term, &ctx);
        if (term->m_left) {
            AstNodePtr recBase = rec;
            detail::convertAttrsToNode(term->m_left, recBase, ctx);
        }
        auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, term, &ctx);
        b->m_right = rec;
        return b;
    }
    return convertForKind<ParserToken::Kind::TypeDecl>(term, ctx);
}

AstNodePtr TermToAstConverter::visit_ALIAS_TYPE(const trust::TermPtr& term, ConvertServices& ctx) {
    // `:A ::- T;` - ВТОРОЕ имя существующего типа: новый TypeId НЕ создаётся (тот же TypeId, что
    // у цели). Узел - TypeDecl (как у `::=`), различает формы кэш `Binary::m_isTypeAlias`.
    // Грамматика (`alias_type_rhs`) допускает только типовой RHS, поэтому определение
    // record/native через `::-` невыразимо.
    if (!term->m_left || term->m_left->getTermID() != trust::TermID::TYPE) {
        ctx.diag().report(Severity::Error, term->m_mapperRange, "type alias requires a type sigil on the left: write ':A ::- T;' (use ':-' to alias a name)");
        return nullptr;
    }
    return convertForKind<ParserToken::Kind::TypeDecl>(term, ctx);
}

AstNodePtr TermToAstConverter::visit_TYPE_DEFINE(const trust::TermPtr& term, ConvertServices& ctx) {
    // `=` - ОПРЕДЕЛЕНИЕ ранее объявленного типа. Форма RHS та же, что у `::=`, но узел
    // помечается TermID::TYPE_DEFINE: семантика различает объявление (создать тип) и
    // определение (привязать реализацию к существующему предобъявлению).
    if (isNativeClassFwdTerm(term->m_right)) {
        auto ncd = std::make_shared<ClassDecl>(ParserToken::Kind::ClassDecl, term, &ctx);
        if (term->m_left) {
            AstNodePtr ncdBase = ncd;
            detail::convertAttrsToNode(term->m_left, ncdBase, ctx);
        }
        auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, term, &ctx);
        b->m_right = ncd;
        return b;
    }
    if (isUserRecordTerm(term->m_right)) {
        auto rec = std::make_shared<RecordDecl>(ParserToken::Kind::StructDecl, term, &ctx);
        if (term->m_left) {
            AstNodePtr recBase = rec;
            detail::convertAttrsToNode(term->m_left, recBase, ctx);
        }
        auto b = std::make_shared<Binary>(ParserToken::Kind::TypeDecl, term, &ctx);
        b->m_right = rec;
        return b;
    }
    return convertForKind<ParserToken::Kind::TypeDecl>(term, ctx);
}

AstNodePtr TermToAstConverter::visit_CREATE_NAME(const trust::TermPtr& term, ConvertServices& ctx) {
    // CREATE_NAME (`:=`) - оператор объявления функции И переменной (единый синтаксический узел).
    // Класс-селекция по форме m_left: сигнатура функции (m_left->isCall()) → FuncDecl;
    // иначе переменная → VarDecl. Раскладка детей - в FuncDecl/VarDecl конструкторах.
    // Для переменной охват [имя, expr] вычисляется в VarDecl::range(); диапазон функции
    // ([имя, оператор], без тела) - в FuncDecl::range() (признак функции).
    if (term->m_left && term->m_left->isCall()) {
        auto fd = std::make_shared<FuncDecl>(ParserToken::Kind::FuncDecl, term, &ctx);
        // Типовые параметры `<...>` (грамматика `template_prefix` кладёт их в term->m_template).
        // Различаем три формы:
        //   1) `<T> %cpp::type() := ...;`            - нативный шаблон-ТИП;
        //   2) `<T> name(...) := %native...;`        - шаблонный native-import (ФУНКЦИЯ-алиас);
        //   3) `<T> name(...) := { ... };` / `...;`  - пользовательская generic-функция.
        if (term->m_template.has_value()) {
            // Типовые параметры сохраняем - семантика выдаст явную ошибку для шаблонного оператора.
            std::vector<AstNodePtr> params;
            params.reserve(term->m_template->size());
            for (const auto& [pname, pterm] : *term->m_template) {
                (void)pname;
                if (!pterm) {
                    continue;
                }
                params.push_back(std::make_shared<ArgNode>(pterm, nullptr, nullptr, &ctx.attrs()));
            }
            fd->m_templateParams = std::move(params);

            // Нативная привязка шаблонного объявления задаётся ТОЛЬКО справа: ctor уже
            // выставил временный носитель C++-имени (`Decl::m_pendingNativeCppName`) для `:= %cpp ...;`.
            // Форма `<T> %cpp() := ...;`
            // (нативное имя в левой части) запрещена: нативный шаблон-ТИП объявляется оператором
            // типа (`<T> Name ::= %cpp { };`), диагностику выдаёт rejectNativeDeclaredName.
        }
        // `name(...):T := target ...;` — реализация делегированием к другой функции (нативный `%target ...`
        // сохранён в m_body (делегат из ast_decl.cpp ctor); trust-делегат сохраняем ниже.
        if (term->m_right && term->m_right->m_right && term->m_right->m_right->getTermID() == trust::TermID::ELLIPSIS &&
            term->m_right->getTermID() != trust::TermID::NATIVE) {
            fd->m_definition = Definition::forwardTo(std::string(term->m_right->getText()));
        }
        return fd;
    }
    // Префиксный сигл ссылочного типа `&& x : Int32 := 5` / `&& x := 5` / `&* u := 10` / `&? w := & x`:
    // assign_item = `ptr lval` (`&`/`&?` → OPERATOR_PTR) или `take rval_name` (`*` → TAKE), где левый
    // терм `:=` - ref-оператор, обёртывающий lval-имя (`x : Int32` в m_right; тип - в m_right->m_type,
    // может отсутствовать - авто-вывод pointee из инициализатора). Разворачиваем: имя берём из m_right
    // ref-оператора, а вид ссылки из сигла превращаем в атрибут @[reftype("...")] на объявлении -
    // дальше работают стандартные applyRefAttrs (семантика) и reftype-ветка DeclEmitter (кодоген).
    // Вид/имя атрибута - из единого источника refTypeFromTypeSigil/refTypeName (types/typekind.hpp).
    // В visit_ASSIGN (`*x = 7`) - это разыменование-мутация, НЕ сигл (обрабатывается отдельно).
    if (term->m_left && (term->m_left->getTermID() == trust::TermID::OPERATOR_PTR || term->m_left->getTermID() == trust::TermID::TAKE) &&
        term->m_left->m_right) {
        // Вид из сигла (refTypeFromTypeSigil). Константность задаётся атрибутом @[readonly@]
        // (`^` на имени) и применяется стандартно (convertAttrsToNode / applyRefAttrs).
        const auto kind = refTypeFromTypeSigil(term->m_left->getText());
        if (kind) {
            const TermPtr nameTerm = term->m_left->m_right;
            term->m_left = nameTerm; // `:=`: слева - имя (с типом, если был), справа - инициализатор
            auto vd = std::make_shared<VarDecl>(ParserToken::Kind::VarDecl, term, &ctx);
            const AttrPool& ap = ctx.attrs();
            if (const auto rid = ap.lookup(attr::Reftype); rid.has_value()) {
                // manual=false: вид задан КОРОТКИМ символьным маркером (а не явным @[reftype(...)]).
                // Флаг используется правилом «короткие маркеры запрещены на границе API» (decl_analyzer).
                vd->add_attr(*rid, /*manual=*/false);
                vd->set_attr_args(*rid, {std::string(refTypeName(*kind))});
            }
            return vd;
        }
    }
    // Деструктуризация `a, b, ... := source;`: многоимённый LHS (цепочка m_left->m_left) + RHS
    // (`... source` - spread-коллекция, или выражение-кортеж). `x := ...;` (forward, одно имя) - НЕ.
    const bool isDestructure = term->m_left && term->m_left->m_left && term->m_right;
    if (isDestructure) {
        return std::make_shared<DestructureDecl>(ParserToken::Kind::DestructureDecl, term, &ctx);
    }
    // `x := ... source;` - spread с ОДНОЙ целью: неоднозначно (вся коллекция или первый элемент?)
    // и не поддерживается (точная привязка/rest требует маркер `rest...` или список имён). Guard.
    if (term->m_right && term->m_right->getTermID() == trust::TermID::ELLIPSIS && term->m_right->m_right) {
        ctx.diag().report(Severity::Error, term->m_right->m_mapperRange,
                          "spread '...' requires multiple destructuring targets "
                          "(e.g. 'a, b := ... source;'); single-target 'x := ... source;' is not supported");
    }
    return std::make_shared<VarDecl>(ParserToken::Kind::VarDecl, term, &ctx);
}

} // namespace trust
