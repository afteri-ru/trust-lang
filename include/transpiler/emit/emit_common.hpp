#pragma once

// include/transpiler/emit/emit_common.hpp
// Общие низкоуровневые помощники кодогенерации, разделяемые эмиттерами и драйвером
// CppTranspiler: RAII-обёртка пары mapStart/mapStop, сборка операторов тела из узла-тела,
// форматирование скалярных значений членов Enum/Variant. Вынесены сюда, чтобы эмиттеры
// компилировались независимо (по одному TU на компонент).

#include "types/type_read.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "sourcemap/mapper.hpp"
#include "location/location.hpp"
#include "types/call.hpp"
#include "types/type_system.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
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
    MapperScope(SourceMapWriter& mapper, MapperRange range, MapperFile out)
    : m_mapper(mapper)
    , m_range(range) {
        if (!m_range.isInvalid()) {
            m_mapper.mapStart(range, out);
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
/// числовые/прочие как есть. Единый источник форматирования значений членов Enum/Variant.
inline std::string memberValueCpp(const TypeReadModel& reg, TypeId mt, std::string raw) {
    const TypeId mc = reg.store().desugaredTypeId(mt);
    const TypeId sc = reg.store().desugaredTypeId(reg.getType(type::StrChar));
    const TypeId sw = reg.store().desugaredTypeId(reg.getType(type::StrWide));
    const TypeId rat = reg.store().desugaredTypeId(reg.getType(type::Rational));
    if (mc == sc) {
        return "\"" + raw + "\"";
    }
    if (mc == sw) {
        return "L\"" + raw + "\"";
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
        auto cpp = reg.printer().getCppTypeName(mc);
        if (!cpp) {
            throw std::logic_error("memberValueCpp: " + cpp.error());
        }
        return *cpp + "(\"" + esc + "\")";
    }
    // Числовые значения: срезаем разделители разрядов '_' (целевой C++ не принимает `10_000`).
    return stripDigitSeparators(raw);
}

/// Элемент литерала словаря/коллекции (имя/метка, узел значения, тип из семантики).
/// Единый источник чтения элементов `m_body` (ArgNode) для кодогенерации словарей/диапазонов.
struct DictElement {
    std::string name;         ///< имя/метка элемента ("" - позиционный)
    const AstNodeBase* value; ///< узел значения (nullptr - пустой элемент)
    QualType resultType;      ///< тип значения (из семантики; identity + квалификаторы)
};

/// Собирает элементы литерала словаря (`DictLiteralNode::m_body`) в единый список DictElement.
/// Пропускает не-ArgNode элементы. Используется эмиттерами выражений (dict/typed construction).
inline std::vector<DictElement> dictElements(const DictLiteralNode& n) {
    std::vector<DictElement> out;
    out.reserve(n.m_body.size());
    for (const auto& el : n.m_body) {
        if (!el || el->kind() != ParserToken::Kind::ArgNode) {
            continue;
        }
        const auto& a = static_cast<const ArgNode&>(*el);
        out.push_back(DictElement{std::string(a.text()), a.m_value.get(), a.resultType});
    }
    return out;
}

/// Квалифицированное C++-имя trust-сущности: namespace-сегменты через `nameToCpp`
/// (`ns::sub` → `c_ns::c_sub`). ЕДИНАЯ точка маппинга сегментов - utils::cppNamespacePath
/// (общая с печатью типов).
using utils::cppNamespacePath;

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
    kType,     ///< тип: `c_ns::c_Point`
    kFunction, ///< функция: `c_ns::c_f` (+ identity-суффикс снаружи)
    kValue,    ///< переменная/статический член: `c_ns::c_x`
    kField,    ///< поле/член экземпляра: `c_field` (без namespace)
    kOperator, ///< оператор: `operator<sym>`
    kEntry,    ///< entry-функция модуля: сырое имя
};

/// ЕДИНАЯ точка вычисления C++-имени (кодоген + экспорт модуля + forward-decl):
/// `ns::f` → `c_ns::c_f`, `ns::Class::field` → `c_ns::c_Class::c_field`, `::f` → `c_f`.
/// Нативные имена - C++-символ как есть (снимается только `%`). `nameToCpp` получает
/// только ОДИН сегмент: квалифицированное имя разбирается здесь (иначе `ns::x` → `cpp_ns$$x`).
/// `moduleNamespace` (`<enc>`) — ЕДИНСТВЕННЫЙ механизм изоляции: модуль ставится ПЕРВЫМ
/// сегментом (`<enc>::c_ns::c_f`); для полей/нативных/entry пусто.
inline std::string cppName(std::string_view trustName, CppEntityKind kind, std::string_view moduleNamespace = {}) {
    if (kind == CppEntityKind::kOperator) {
        return op::operatorCppNameByFunction(trustName);
    }
    if (kind == CppEntityKind::kEntry) {
        return std::string(utils::stripNativePrefix(trustName));
    }
    if (kind == CppEntityKind::kField) {
        return utils::cppIdent(trustName);
    }
    const std::string qualified = utils::qualifiedCppName(trustName);
    // Нативные имена не изолируются: символ фиксирован внешней библиотекой.
    if (moduleNamespace.empty() || utils::isNativeName(trustName)) {
        return qualified;
    }
    return std::string(moduleNamespace) + "::" + qualified;
}

/// C++-имя функции/метода/оператора: обёртка над `cppName` (kind=kFunction/kOperator)
/// с совместимой сигнатурой для существующих вызовов.
inline std::string cppFuncName(std::string_view trustName, bool isOperator, bool isEntry, std::string_view moduleNamespace = {}) {
    return cppName(trustName, isEntry ? CppEntityKind::kEntry : (isOperator ? CppEntityKind::kOperator : CppEntityKind::kFunction), moduleNamespace);
}

} // namespace trust
