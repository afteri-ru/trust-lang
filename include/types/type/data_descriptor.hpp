#pragma once
// include/types/type/data_descriptor.hpp
// TypeDescriptor и правило ключа метода (нативность/константность из ключа).
// Часть модели данных типов: самостоятельным контрактом НЕ является (umbrella —
// types/type_data.hpp). Набор include повторяет umbrella, чтобы часть компилировалась
// независимо от порядка включения.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "attrs/attr.hpp"
#include "location/location.hpp"
#include "location/module_index.hpp"
#include "types/call/callable_info.hpp"
#include "types/group.hpp"
#include "types/type/ref_type.hpp"
#include "types/type_id.hpp"

namespace trust {

// -- Метод типа --------------------------------------------
// Описание метода `obj.method(...)` хранится как полный ключ-имя → интернированный
// функциональный тип (TypeId). Ключ кодирует нативность и константность: ведущий '%' -
// нативный член, хвостовой '^' - константный (напр. "%count^"). Нативность/константность
// ВЫВОДЯТСЯ из ключа, отдельно не хранятся (TypeId - интернированная сигнатура). Алиас -
// просто другое имя существующего метода: доверенное имя → целевой ключ (обязан повторять
// семантику цели: нативность и константность совпадают, проверяется EXPECT при регистрации).

// -- Специализация шаблона (C++-модель TemplateSpecializationKind) -----------
// Классификация record-типа. Значение НЕ выводится из TypeId (интернированный ключ не кодирует
// семантический статус) — хранится явно на дескрипторе.
//   kNone                  - обычный record / primary-шаблон (аналог Clang Undeclared);
//   kImplicitInstantiation - неявная инстанциация (выставляется при создании инстанциации);
//   kExplicitSpecialization- явная полная специализация (резерв под этап S5).
enum class TemplateSpecializationKind : uint8_t {
    kNone = 0,
    kImplicitInstantiation,
    kExplicitSpecialization,
};

// -- Type descriptor --------------------------------------
struct TypeDescriptor {
    std::string name;                         // canonical name (e.g., "Int32", "Vector") - владеющая копия
                                              // (для пользовательских алиасов имя из временной строки analyzeTypeDecl)
    std::vector<AttrId> attrs;                // атрибуты
    MapperRange sourceRange{};                // position of type declaration in source file
    std::string cppName;                      // C++ имя для codegen (e.g. "int32_t")
    std::vector<std::string> preprocIncludes; // директивы препроцессора для C++ кода; первый - основной
                                              // (e.g. "#include <cstdint>" / "@trust/dict.hpp"), пусто если не требуется
    TypeId baseType{INVALID_TYPE_ID};         // для алиас-цепочки: A → Byte → Int32
    std::optional<TypeData> data;             // nullopt = forward declaration (incomplete type)
    // Методы типа (obj.method): полный ключ (native '%', const '^') → НАБОР сигнатур
    // (CallableSignature: identity + параметры + C++-суффикс). Нативность/константность —
    // из ключа. Набор непуст; один элемент — не-перегруженный метод.
    std::map<std::string, std::vector<CallableSignature>> methods;
    // Алиасы методов: bare-имя алиаса → полный ключ цели (напр. "length" → "%count^").
    // Алиас обязан повторять семантику цели (native/const) - проверяется при регистрации.
    std::map<std::string, std::string> methodAliases;
    // Базовые классы (наследование Record-типов) — НЕ путать с baseType!
    // baseType используется для обнаружения алиаса и сохранения НЕПОСРЕДСТВЕННОЙ цели
    // (`A → B → Int32`); канонический корень хранится отдельно в canonicalType.
    // должны идти СЮДА, иначе производный тип схлопнулся бы в базовый. Абстрактные маркеры
    // (:Any/:Struct/:Class) сюда НЕ попадают (не эмитятся в C++).
    std::vector<TypeId> baseClasses;
    // Логические библиотеки из `@[link("name")]` на объявлении нативного класса/шаблона.
    // Применяются on-use (TypeEmitter::recordUsedType): импорт модуля сам по себе НЕ линкует.
    std::vector<std::string> libs;
    // Канонический TypeId для getDesugaredTypeId (O(1)): корень alias-цепочки. Заполняется
    // только для алиасов (baseType != INVALID); для не-алиасов INVALID (getDesugaredTypeId
    // возвращает сам id, сохраняя kind-биты, напр. fast-path RefType). Это кеш ПРОИЗВОДНОГО
    // от baseType значения, а не второй источник identity.
    TypeId canonicalType{INVALID_TYPE_ID};
    // -- Проекции канонического ключа регистрации (ключ живёт в TypeNameIndex; формат -
    // types/storage_key.hpp): name остаётся display-именем, namespacePath/filePrivate/module -
    // вычисленные из ключа проекции.
    std::string namespacePath;
    bool filePrivate{false};
    /// Модуль объявления (проекция модуля ключа). `kInvalid` — плоский ключ/встроенный домен.
    /// Нужен C++-проекции имени (`nominalCppName`), чтобы модульная изоляция типа совпадала
    /// на объявлении и на использовании; отдельной таблицы «тип → модуль» нет.
    ModuleIndex module = ModuleIndex::invalid();
    /// Специализация шаблона (см. TemplateSpecializationKind выше). Для record-инстанциаций
    /// выставляется в `kImplicitInstantiation` при создании; primary-шаблон и обычный record — `kNone`.
    TemplateSpecializationKind specializationKind{TemplateSpecializationKind::kNone};
};

} // namespace trust
