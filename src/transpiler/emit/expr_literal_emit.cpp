// ExprEmitter: Литералы, идентификаторы/имена типов, embed/document и контекст-макросы.

#include "transpiler/emit/expr_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/call.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"

#include "transpiler/emit/expr_emit_util.hpp"
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace trust {

// Литерал - всегда только текст (statement-позицию оборачивает SemicolonStmt, добавляя ';').
void ExprEmitter::visit_IntLiteral(const Literal& n) {
    // Разделители разрядов '_' срезаются перед эмитом: целевой C++ (clang в этой toolchain)
    // не принимает `10_000` как разделитель разрядов, а значение литерала от формата не зависит.
    const std::string cleaned = stripDigitSeparators(n.text());
    // Сверхразрядный целочисленный литерал (вне Int64) типизирован как BigInteger: эмитим
    // `trust::BigInteger("...")` - десятичный строковый конструктор. Постфиксная аннотация
    // `N :Rational` → `trust::Rational("N", "1")` (числитель/знаменатель; работает и для
    // сверхразрядных значений). Инклуды записываются через emitTypeName (единая точка сбора).
    const TypeReadModel& reg = m_ectx.m_services.readTypes();
    const TypeId bi = reg.store().desugaredTypeId(reg.getType(type::BigInteger));
    const TypeId rat = reg.store().desugaredTypeId(reg.getType(type::Rational));
    const TypeId tid = (n.typeState.getTypeId() != INVALID_TYPE_ID) ? reg.store().desugaredTypeId(n.typeState.getTypeId()) : INVALID_TYPE_ID;
    if (tid == bi) {
        auto cpp = m_driver.m_type.emitTypeName(bi);
        if (!cpp) {
            return; // emitTypeName уже сообщил диагностику
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::format("{}(\"{}\")", *cpp, cleaned));
        return;
    }
    if (tid == rat) {
        auto cpp = m_driver.m_type.emitTypeName(rat);
        if (!cpp) {
            return;
        }
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::format("{}(\"{}\", \"1\")", *cpp, cleaned));
        return;
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, cleaned);
}

void ExprEmitter::visit_RationalLiteral(const Literal& n) {
    // Рациональный литерал `num\den` создаётся непосредственно из строки: эмитим
    // `trust::Rational("num\den")` - парсинг `num\den` выполняет однострочный конструктор
    // Rational. В C++-строковом литерале обратная косая экранируется (`\` → `\\`).
    // Инклуд Rational-типа записывается через emitTypeName (единая точка сбора).
    std::optional<std::string> cpp;
    if (n.typeState.getTypeId() != INVALID_TYPE_ID) {
        cpp = m_driver.m_type.emitTypeName(n.typeState);
    } else if (auto rid = m_ectx.m_services.readTypes().findType(type::Rational)) {
        cpp = m_driver.m_type.emitTypeName(*rid);
    }
    if (!cpp) {
        return; // emitTypeName уже сообщил диагностику
    }
    const std::string_view t = n.text();
    std::string escaped;
    escaped.reserve(t.size() * 2);
    for (const char c : t) {
        if (c == '\\') {
            escaped += "\\\\";
        } else {
            escaped += c;
        }
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::format("{}(\"{}\")", *cpp, escaped));
}

void ExprEmitter::visit_StrChar(const Literal& n) {
    // Строка в двойных кавычках "…" → StrChar → обычная "…". Ограничитель StrChar - ",
    // поэтому сырая " в теле невозможна (закрыла бы строку); в C++-литерале "…" экранируем
    // её в \" (страховка для синтезированных узлов). Escape-последовательности trust сохранены
    // как есть и валидны в C++ без изменений - копируем их (backslash + следующий символ) не трогая.
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::format("\"{}\"", escapeCppStringBody(n.text())));
}

void ExprEmitter::visit_StrWide(const Literal& n) {
    // Строка в одинарных кавычках '…' → StrWide → wide-строка L"…". Ограничитель StrWide - ',
    // поэтому сырая " в теле допустима; в C++-литерале L"…" её экранируем в \". Escape-
    // последовательности trust сохранены как есть и валидны в C++ без изменений.
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, std::format("L\"{}\"", escapeCppStringBody(n.text())));
}

void ExprEmitter::visit_FloatLiteral(const Literal& n) {
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, n.text());
}

// Контекст-макросы (@__NAMESPACE__, @::, @__FUNCTION__, @__FUNCSIG__, @__FUNCDNAME__)
// раскрываются анализатором (заменяются на Literal/StrChar или имя) до транспиляции.
// До транспилятора такие узлы доходить не должны - это недостижимая ветка (сигнал бага).
void ExprEmitter::visit_ContextMacro(const ContextMacro&) {
    FAULT("ContextMacro reached the transpiler (must be expanded by the analyzer)");
}

// Имя сущности для C++-манглинга: у алиаса имя ссылки (`f`) отличается от имени объявления-цели
// (`g`), а C++-символ принадлежит цели. Квалифицированный spelling сохраняется - его разбирает
// `cppName` (`ns::x` → `ns::x`).
static std::string_view declEntityName(const IdentName& node) {
    const AstNodeBase* decl = node.resolvedDecl;
    if (decl == nullptr || utils::isQualifiedName(node.text()) || !(decl->is<FuncDecl>() || decl->is<VarDecl>())) {
        return node.text();
    }
    const HasText* text = decl->as_text();
    return text != nullptr && !text->text().empty() ? text->text() : node.text();
}

// EmbedExpr - raw text: statement с маппингом, выражение - только текст.
void ExprEmitter::visit_EmbedExpr(const RawText& n) {
    // Узкий чек ТОЛЬКО по тексту вставки: рантайм-функции/символы → запись их заголовков
    // (нужно для заголовков, которые физически отсутствуют в каталоге подключаемых файлов
    // и должны быть извлечены из trust-runtime).
    m_driver.m_type.recordRuntimeSymbolsInText(n.text());
    if (m_ectx.m_mode.exprDepth > 0) {
        // trust-маркеры `$x`/`@ns::x` → C++-имена (`x`/`ns::x`): модуль в имя не входит.
        m_driver.emitExprText(utils::transformEmbedCpp(n.text()));
    } else {
        MapperScope scope(m_ectx.m_services.source(), n.range(), m_ectx.m_output.out);
        m_driver.emitExpr(&n);
    }
}

// Document - документирующий комментарий: эмитится в statement-позиции. Trust-доки `##`/`##<`
// невалидны в C++ (префикс '#' - препроцессор), поэтому нормализуются в однострочные C++ `///`/`///<`;
// `/** … */` и `///` выводится как есть. Подавление (флаг -fno-comments) - на уровне обхода.
void ExprEmitter::visit_Document(const RawText& n) {
    if (m_ectx.m_mode.exprDepth > 0) {
        return;
    }
    // Комментарий всегда с новой строки: если текущая позиция НЕ в начале строки (напр. сразу после
    // '}' функции — комментарий раскрытого макроса `/// expanded macro ...`), добавляем перевод
    // ПЕРЕД комментарием, чтобы не сливать `} /// ...` в одну строку. Отступ (только пробелы после
    // '\n') строкой не считается — лишний перевод не дублируется.
    const std::string_view body = m_ectx.m_services.source().output_body(m_ectx.m_output.out);
    if (!outputAtLineStart(body)) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "\n");
    }
    std::string_view t = n.text();
    if (t.starts_with("##")) {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, "///");
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, t.substr(2));
    } else {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, t);
    }
}

void ExprEmitter::visit_Ident(const IdentName& n) {
    // Пост-условие функции: имя функции = возвращаемое значение (temp, см. visit_ReturnStmt).
    if (!m_ectx.m_result.cpp.empty() && n.text() == m_ectx.m_result.name) {
        m_driver.emitExprText(m_ectx.m_result.cpp);
        return;
    }
    // Нативный импорт-алиас: вызов trust-имени переписывается в прямой вызов нативного C++-имени.
    // Ключ — ОБЪЯВЛЕНИЕ ссылки (`resolvedDecl`), а не текст: одноимённые импорты разных модулей
    // дают разные записи.
    if (const std::string_view native = m_ectx.nativeCppNameOf(n.resolvedDecl); !native.empty()) {
        m_driver.emitExprText(native);
        return;
    }
    // Статический член нативного класса: C++-имя — проекция владельца (`std::string::npos`), а не
    // манглинг trust-имени. Работает для плоского имени через `using` и для вызванной статики.
    if (const TypeId owner = nativeStaticOwner(n); owner != INVALID_TYPE_ID) {
        emitNativeStaticMember(owner, n.text());
        return;
    }
    // Идентификатор в выражении: единый C++-манглинг (значение/статический член TRUST-класса,
    // `ns::x` → `ns::x`; короткое имя → `x`). Модуль в имя не входит.
    m_driver.emitExprText(m_ectx.cppName(declEntityName(n), CppEntityKind::kValue));
}

void ExprEmitter::visit_TypeName(const IdentType& n) {
    m_driver.emitExprText(n.text());
}

// Expression-kind'ы без реализованной генерации: как statement - no-op,
// как выражение - placeholder "{}" (default-поведение emitExpr).
void ExprEmitter::visit_VarRef(const AstNodeAttr&) {
    m_driver.emitPlaceholderExpr(m_ectx.m_output.out);
}

} // namespace trust
