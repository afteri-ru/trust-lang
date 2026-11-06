#pragma once

// Общие низкоуровневые помощники кодогенерации, разделяемые эмиттерами и драйвером
// CppTranspiler: RAII-обёртка пары mapStart/mapStop, сборка операторов тела из узла-тела,
// форматирование скалярных значений членов Enum/Variant. Вынесены сюда, чтобы эмиттеры
// компилировались независимо (по одному TU на компонент).

#include "types/type_read.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "sourcemap/mapper.hpp"
#include "location/location.hpp"
#include "transpiler/emit/expr_emit_util.hpp"
#include "types/call.hpp"
#include "types/visibility.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// RAII-обёртка пары mapStart/mapStop: mapStart в конструкторе, mapStop в деструкторе.
/// Гарантирует закрытие маппинга даже при раннем выходе (return/EXPECT). Некопируем.
/// Range для mapStop всегда совпадает с range, переданным в mapStart.
/// При невалидном range (isInvalid, напр. промежуточный токен раскрытого макроса) маппинг НЕ
/// создаётся: mapStart/mapStop не вызываются, валидность проверяется ДО обращения к мапперу
/// (маппер строгий). Проверка - isInvalid() (как в mapStart), а не isValid() (границы файла):
/// диапазон с offset за концом файла допустим (test-codegen строит такие вручную).
class MapperScope {
  public:
    MapperScope(SourceMapWriter& mapper, MapperRange range, MapperFile out, bool recordRange = true)
    : m_mapper(mapper)
    , m_range(range) {
        if (!m_range.isInvalid()) {
            m_mapper.mapStart(range, out, recordRange);
        }
    }
    ~MapperScope() {
        if (!m_range.isInvalid()) {
            m_mapper.mapStop(m_range);
        }
    }
    MapperScope(const MapperScope&) = delete;
    MapperScope& operator=(const MapperScope&) = delete;

  private:
    SourceMapWriter& m_mapper;
    MapperRange m_range;
};

/// Собрать операторы тела и диапазон блока из узла-тела
/// (ScopeBlock/Sequence → m_body + range блока; одиночный statement → сам узел).
inline void collectBodyStatements(const AstNodePtr& bodyNode, std::vector<AstNodePtr>& out, MapperRange& blockRange) {
    out.clear();
    blockRange = {};
    if (!bodyNode) {
        return;
    }
    // Блочный узел (ScopeBlock/sequence/ModuleNode) → его m_body + range; иначе - одиночный statement.
    // as_sequence() (virtual) устраняет kind/static_cast; Attr исключён через is_block_kind.
    if (is_block_kind(bodyNode->kind())) {
        out = bodyNode->as_sequence()->m_body;
        blockRange = bodyNode->range();
    } else {
        out.push_back(bodyNode);
        blockRange = bodyNode->range();
    }
}

/// True, если текущая позиция вывода — начало строки: вывод пуст, заканчивается '\n', или всё после
/// последнего '\n' — только пробелы/табуляции (отступ). Используется перед эмиссией документирующих
/// комментариев: не добавляет лишний перевод строки, если предыдущий statement уже позиционировал строку
/// (в т.ч. через отступ indentPrefix), и добавляет его, если комментарий «прилип» к хвосту строки.
inline bool outputAtLineStart(std::string_view body) noexcept {
    if (body.empty() || body.back() == '\n') {
        return true;
    }
    const size_t nl = body.rfind('\n');
    const std::string_view after = (nl == std::string_view::npos) ? body : body.substr(nl + 1);
    for (const char c : after) {
        if (c != ' ' && c != '\t') {
            return false;
        }
    }
    return true;
}

/// Проверяет, завершается ли тело функции явным return (для инжекта `return 0;` в entry-функцию).
inline bool bodyEndsWithReturn(const std::vector<AstNodePtr>& body) {
    return !body.empty() && body.back() && body.back()->kind() == ParserToken::Kind::ReturnStmt;
}

/// C++-литерал значения члена по типу: StrChar "x" → "x", StrWide 'x' → L"x", Rational num\den →
/// <C++-имя Rational>("num\den") (обратная косая экранируется; имя учитывает провайдера),
/// числовые/прочие как есть. Единый источник форматирования значений членов Enum/Variant
/// (строковое тело экранируется тем же helper'ом, что и литералы выражений).
inline std::string memberValueCpp(const TypeReadModel& reg, const TypeCppFactsView& facts, TypeId mt, std::string raw) {
    const TypeId mc = reg.store().desugaredTypeId(mt);
    const TypeId sc = reg.store().desugaredTypeId(reg.getType(type::StrChar));
    const TypeId sw = reg.store().desugaredTypeId(reg.getType(type::StrWide));
    const TypeId rat = reg.store().desugaredTypeId(reg.getType(type::Rational));
    if (mc == sc) {
        return "\"" + escapeCppStringBody(raw) + "\"";
    }
    if (mc == sw) {
        return "L\"" + escapeCppStringBody(raw) + "\"";
    }
    if (mc == rat) {
        std::string esc;
        esc.reserve(raw.size() * 2);
        for (const char c : raw) {
            if (c == '\\') {
                esc += "\\\\";
            } else {
                esc += c;
            }
        }
        auto cpp = CppTypeRenderer(reg, facts).render(mc);
        if (!cpp) {
            FAULT("memberValueCpp: no C++ name for the member type: {}", cpp.error());
        }
        return *cpp + "(\"" + esc + "\")";
    }
    // Числовые значения: срезаем разделители разрядов '_' (целевой C++ не принимает `10_000`).
    return stripDigitSeparators(raw);
}

/// Текст C++-проверки POD для Struct-типа. ЕДИНЫЙ источник: объявление Struct и инстанциация
/// Struct-шаблона печатают одну и ту же формулировку (диагностика C++ одинакова).
inline std::string podAssertText(std::string_view cppName, std::string_view trustName) {
    const std::string cpp(cppName);
    return "static_assert(std::is_trivial_v<" + cpp + "> && std::is_standard_layout_v<" + cpp + ">, \"trust: Struct '" + std::string(trustName) +
           "' must be POD\");";
}

/// Элемент литерала словаря/коллекции (имя/метка, узел значения, тип из семантики, диапазон).
/// Единый источник чтения элементов `m_body` (ArgNode) для кодогенерации словарей/диапазонов и
/// членов Enum/Variant (диапазон нужен name-маппингу членов).
struct DictElement {
    std::string name;         ///< имя/метка элемента ("" - позиционный)
    const AstNodeBase* value; ///< узел значения (nullptr - пустой элемент)
    QualType resultType;      ///< тип значения (из семантики; identity + квалификаторы)
    MapperRange range;        ///< исходный диапазон элемента (ArgNode)
};

/// Собирает элементы литерала словаря (`DictLiteralNode::m_body`) в единый список DictElement.
/// Пропускает не-ArgNode элементы. Используется эмиттерами выражений (dict/typed construction) и
/// объявлениями Enum/Variant.
inline std::vector<DictElement> dictElements(const DictLiteralNode& n) {
    std::vector<DictElement> out;
    out.reserve(n.m_body.size());
    for (const auto& el : n.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        out.push_back(DictElement{std::string(a.text()), a.m_value.get(), a.resultType, a.range()});
    }
    return out;
}

/// C++-имя, разложенное на namespace-сегменты и собственное имя сущности.
/// ЕДИНАЯ точка разбора `ns::sub::name` (класс в native-namespace, экспорт-функция, статический
/// член): сегменты НЕ манглятся кодеком C++-имени (они уже C++-идентификаторы), собственное имя -
/// последний сегмент как есть.
struct CppQualifiedParts {
    std::vector<std::string> namespaces;
    std::string name;
};

inline CppQualifiedParts splitCppName(std::string_view cppName) {
    CppQualifiedParts out;
    std::string_view rest = cppName;
    for (std::size_t sep = rest.find("::"); sep != std::string_view::npos; sep = rest.find("::")) {
        out.namespaces.emplace_back(rest.substr(0, sep));
        rest.remove_prefix(sep + 2);
    }
    out.name = std::string(rest);
    return out;
}

/// Квалифицированное C++-имя trust-сущности: namespace-сегменты через `nameToCpp`
/// (`ns::sub` → `ns::sub`). ЕДИНАЯ точка маппинга сегментов - utils::cppNamespacePath
/// (общая с печатью типов).
using utils::cppNamespacePath;

/// C++-only имя (namespace/template), несовместимое с C-сцеплением экспорта: `std::string`,
/// `Foo<T>`. ЕДИНЫЙ предикат для функции и переменной (`@[export]`/`@[link]` с `extern "C"`).
[[nodiscard]] inline bool requiresCppLinkage(std::string_view cppTypeName) noexcept {
    return cppTypeName.find('<') != std::string_view::npos || cppTypeName.find("::") != std::string_view::npos;
}

/// Открытие C++-namespace-сегмента. `inlineForm` — определение продолжается на той же строке
/// (`namespace ns { `, напр. определение функции/метода), иначе сегмент открывается со своей
/// строки (`namespace ns {\n`, напр. класс/переменная). ЕДИНАЯ точка форматирования.
[[nodiscard]] inline std::string cppNamespaceOpen(std::string_view ns, bool inlineForm) {
    return inlineForm ? ("namespace " + std::string(ns) + " { ") : ("namespace " + std::string(ns) + " {\n");
}

/// Закрытие C++-namespace-сегмента — обратная пара к cppNamespaceOpen.
[[nodiscard]] inline std::string cppNamespaceClose(bool inlineForm) {
    return inlineForm ? std::string(" }") : std::string("}\n");
}

/// База C++-класса для `cppClassFrame`: C++-имя + вариант наследования (порядок = порядок баз).
struct CppBase {
    std::string name;
    Visibility access{Visibility::Public};
    /// true — имя базиса печатается БЕЗ спецификатора доступа (рантайм-базис `trust::Enum<...>`:
    /// наследование всегда публичное, `public` избыточен).
    bool omitAccessKeyword = false;
};

/// Каркас C++-класса — ЕДИНЫЙ для пользовательского Record и обёртки/generated builtin-класса:
///   open  = [template<...>\n][namespace ns {\n]*<keyword> Name[ : <access> B, ...] {\n
///   close = `};\n` + `}\n` на каждый namespace-сегмент
/// `nameOffset` — смещение локального C++-имени класса внутри `open` (для source-map).
struct CppClassFrame {
    std::string open;
    std::string close;
    uint32_t nameOffset = 0;
};

[[nodiscard]] inline CppClassFrame cppClassFrame(std::string_view keyword, std::string_view templateHeader, const std::vector<std::string>& namespaces,
                                                 std::string_view className, const std::vector<CppBase>& bases) {
    CppClassFrame frame;
    frame.open += templateHeader;
    for (const std::string& ns : namespaces) {
        frame.open += "namespace " + ns + " {\n";
    }
    frame.open += std::string(keyword) + " ";
    frame.nameOffset = static_cast<uint32_t>(frame.open.size());
    frame.open += className;
    if (!bases.empty()) {
        frame.open += " : ";
        for (std::size_t i = 0; i < bases.size(); ++i) {
            if (i != 0) {
                frame.open += ", ";
            }
            if (!bases[i].omitAccessKeyword) {
                frame.open += visibilityKeyword(bases[i].access);
                frame.open += " ";
            }
            frame.open += bases[i].name;
        }
    }
    frame.open += " {\n";
    frame.close = "};\n";
    for (std::size_t i = 0; i < namespaces.size(); ++i) {
        frame.close += cppNamespaceClose(/*inlineForm=*/false);
    }
    return frame;
}

/// Тип-владелец статического члена нативного класса (`m_nativeOwner` объявления-цели);
/// INVALID_TYPE_ID — ссылка не является таким членом.
inline TypeId nativeStaticOwner(const IdentName& ident) {
    const AstNodeBase* decl = ident.resolvedDecl;
    if (decl == nullptr || !(decl->is<VarDecl>() || decl->is<FuncDecl>())) {
        return INVALID_TYPE_ID;
    }
    // Статическим членом может быть только объявление (Decl-наследник).
    return static_cast<const Decl*>(decl)->m_nativeOwner;
}

/// Вид сущности в C++-пространстве имён (влияет на форму имени, не на namespace-квалификацию).
enum class CppEntityKind : uint8_t {
    kType,     ///< тип: `ns::Point`
    kFunction, ///< функция: `ns::f` (+ identity-суффикс снаружи)
    kValue,    ///< переменная/статический член: `ns::x`
    kField,    ///< поле/член экземпляра: `field` (без namespace)
    kOperator, ///< оператор: `operator<sym>`
    kEntry,    ///< entry-функция модуля: сырое имя
};

/// ЕДИНАЯ точка вычисления C++-имени (кодоген + экспорт модуля + forward-decl):
/// `ns::f` → `ns::f`, `ns::Class::field` → `ns::Class::field`, `::f` → `f`.
/// Нативные имена - C++-символ как есть (снимается только `%`). `nameToCpp` получает
/// только ОДИН сегмент: квалифицированное имя разбирается здесь (иначе `ns::x` был бы экранирован).
/// Модуль в имя НЕ входит: изоляция одноимённых экспортов разных модулей - ответственность
/// программиста через области имён `ns:: { ... }` (ABI полностью совместим с C++).
inline std::string cppName(std::string_view trustName, CppEntityKind kind) {
    if (kind == CppEntityKind::kOperator) {
        return op::operatorCppNameByFunction(trustName);
    }
    if (kind == CppEntityKind::kEntry) {
        return std::string(trustName);
    }
    if (kind == CppEntityKind::kField) {
        return utils::cppIdent(trustName);
    }
    return utils::qualifiedCppName(trustName);
}

} // namespace trust
