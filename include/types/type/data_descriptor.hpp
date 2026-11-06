#pragma once
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
#include "types/inheritance.hpp"
#include "types/type/method_key.hpp"
#include "types/type/ref_type.hpp"
#include "types/type_id.hpp"

namespace trust {

// Описание метода `obj.method(...)` хранится как СТРУКТУРНЫЙ ключ MethodKey
// (имя + const/static) → запись MethodInfo (набор сигнатур + isNative).
// Флаги константности/статичности — поля ключа, в имени не кодируются; нативность — запись.
// Алиас - просто другое имя существующего метода: доверенное имя → целевой MethodKey
// (обязан повторять семантику цели: const/static совпадают, проверяется EXPECT при регистрации).

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

// Ключ (`MethodKey`) — имя-форма; ЗНАЧЕНИЕ несёт набор перегрузок и C++-проекцию (isNative).
// Нативность — свойство записи/объявления, НЕ идентичность перегрузки (симметрия со свободной
// функцией, где это целевой факт `DeclCppFacts::cppNameOf`).
struct MethodInfo {
    std::vector<CallableSignature> overloads; ///< >= 1; identity + параметры + ключ суффикса (DeclId)
    bool isNative = false;                    ///< C++-имя без манглинга
};

struct TypeDescriptor {
    std::string name;                 // canonical name (e.g., "Int32", "Vector") - владеющая копия
                                      // (для пользовательских алиасов имя из временной строки analyzeTypeDecl)
    std::vector<AttrId> attrs;        // атрибуты
    MapperRange sourceRange{};        // position of type declaration in source file
    TypeId baseType{INVALID_TYPE_ID}; // для алиас-цепочки: A → Byte → Int32
    std::optional<TypeData> data;     // nullopt = forward declaration (incomplete type)
    // Методы типа (obj.method): СТРУКТУРНЫЙ ключ-имя-форма → ЗАПИСЬ (набор сигнатур + isNative).
    // Набор непуст; один элемент — не-перегруженный метод.
    std::map<MethodKey, MethodInfo> methods;
    // Алиасы методов: bare-имя алиаса → ключ цели (напр. "length" → {count, const, non-static}).
    // Алиас обязан повторять семантику цели (const/static) - проверяется при регистрации.
    std::map<std::string, MethodKey> methodAliases;
    // Базовые классы (наследование Record-типов) — НЕ путать с baseType!
    // baseType используется для обнаружения алиаса и сохранения НЕПОСРЕДСТВЕННОЙ цели
    // (`A → B → Int32`); канонический корень хранится отдельно в canonicalType.
    // должны идти СЮДА, иначе производный тип схлопнулся бы в базовый. Абстрактные маркеры
    // (:Any/:Struct/:Class) сюда НЕ попадают (не эмитятся в C++).
    std::vector<BaseClass> baseClasses;
    // Канонический TypeId для getDesugaredTypeId (O(1)): корень alias-цепочки. Заполняется
    // только для алиасов (baseType != INVALID); для не-алиасов INVALID (getDesugaredTypeId
    // возвращает сам id, сохраняя kind-биты, напр. fast-path RefType). Это кеш ПРОИЗВОДНОГО
    // от baseType значения, а не второй источник identity.
    TypeId canonicalType{INVALID_TYPE_ID};
    // types/storage_key.hpp): name остаётся display-именем, namespacePath/filePrivate/module -
    // вычисленные из ключа проекции.
    std::string namespacePath;
    bool filePrivate{false};
    /// Модуль объявления (проекция модуля ключа). `kInvalid` — плоский ключ/встроенный домен.
    /// Идентичность КОМПИЛЯТОРА (два одинаковых имени в разных модулях различимы); в C++-имя
    /// типа НЕ входит - изоляция через namespace-путь программиста.
    ModuleIndex module = ModuleIndex::invalid();
    /// Специализация шаблона (см. TemplateSpecializationKind выше). Для record-инстанциаций
    /// выставляется в `kImplicitInstantiation` при создании; primary-шаблон и обычный record — `kNone`.
    TemplateSpecializationKind specializationKind{TemplateSpecializationKind::kNone};
};

} // namespace trust
