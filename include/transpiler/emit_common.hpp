#pragma once

// include/transpiler/emit_common.hpp
// Общие низкоуровневые помощники кодогенерации, разделяемые эмиттерами и драйвером
// CppTranspiler: RAII-обёртка пары mapStart/mapStop, сборка операторов тела из узла-тела,
// форматирование скалярных значений членов Enum/Variant. Вынесены сюда, чтобы эмиттеры
// компилировались независимо (по одному TU на компонент).

#include "ast/ast_nodes.hpp"
#include "ast/token.hpp"
#include "diag/mapper.hpp"
#include "location/location.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"
#include "types/type_names.hpp"

#include <string>
#include <vector>

namespace trust {

/// RAII-обёртка пары mapStart/mapStop: mapStart в конструкторе, mapStop в деструкторе.
/// Гарантирует закрытие маппинга даже при раннем выходе (return/EXPECT). Некопируем.
/// Range для mapStop всегда совпадает с range, переданным в mapStart.
class MapperScope {
  public:
    MapperScope(SourceMapWriter& mapper, MapperRange range, MapperFile out)
    : m_mapper(mapper)
    , m_range(range) {
        m_mapper.mapStart(range, out);
    }
    ~MapperScope() { m_mapper.mapStop(m_range); }
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

/// Проверяет, завершается ли тело функции явным return (для инжекта `return 0;` в entry-функцию).
inline bool bodyEndsWithReturn(const std::vector<AstNodePtr>& body) {
    return !body.empty() && body.back() && body.back()->kind() == ParserToken::Kind::ReturnStmt;
}

/// C++-литерал значения члена по типу: StrChar 'x' → "x", StrWide → L"x", Rational num\den →
/// trust::Rational("num\den") (обратная косая экранируется), числовые/прочие как есть.
/// Единый источник форматирования значений членов Enum/Variant (устраняет дублирование).
inline std::string memberValueCpp(const TypeRegistry& reg, TypeId mt, std::string raw) {
    const TypeId mc = reg.getCanonicalTypeId(mt);
    const TypeId sc = reg.getCanonicalTypeId(reg.getType(type::StrChar));
    const TypeId sw = reg.getCanonicalTypeId(reg.getType(type::StrWide));
    const TypeId rat = reg.getCanonicalTypeId(reg.getType(type::Rational));
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
        return "trust::Rational(\"" + esc + "\")";
    }
    return raw;
}

/// Элемент литерала словаря/коллекции (имя/метка, узел значения, тип из семантики).
/// Единый источник чтения элементов `m_body` (ArgNode) для кодогенерации словарей/диапазонов.
struct DictElement {
    std::string name;         ///< имя/метка элемента ("" - позиционный)
    const AstNodeBase* value; ///< узел значения (nullptr - пустой элемент)
    TypeId resultType;        ///< тип значения (из семантики)
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

} // namespace trust
