#pragma once

// include/semantic/ref_kind.hpp
// Единая точка применения/проверки ЗАЯВЛЕННОГО вида ссылки (атрибут `@[reftype]` или маркер) к
// базовому (pointee) типу. Устраняет дублирование правила «совместим -> применить, иначе mismatch»
// между DeclAnalyzer/NameResolutionPass и ExprTyper.
//
// Тексты диагностик параметризованы (subjectName/actualLabel/RefNameStyle), поэтому существующие
// сообщения сохраняются дословно у каждого вызывающего.

#include "attrs/attr_builtin.hpp"
#include "location/location.hpp"
#include "semantic/pass.hpp"
#include "types/registry.hpp"
#include "types/typekind.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Формат отображения ФАКТИЧЕСКОГО вида в сообщении о несовпадении.
enum class RefNameStyle {
    Mnemonic, ///< только мнемоника вида: `unique`, `shared`
    Full,     ///< полное имя типа: `unique<Int32>`, `shared<MyInt>`
};

/// Резолв и валидация политики доступа из аргументов `@[reftype(...)]` (2-й аргумент).
/// Политика - РЕАЛЬНЫЙ тип реестра (Group::kAccessPolicy): `findType` + `isAccessPolicyTypeFor`.
/// Допустимые политики зависят от вида: shared/weak - AccessMutex/AccessRwMutex/AccessSingleThread;
/// `unique` - монопольное владение, политик НЕ имеет.
/// Возвращает TypeId политики либо INVALID_TYPE_ID (нет аргумента / ошибка диагностирована).
[[nodiscard]] inline TypeId resolveAccessPolicy(AnalysisContext& actx, const std::vector<std::string>* args, RefType kind, MapperRange range) {
    if (args == nullptr || args->size() < 2 || args->at(1).empty()) {
        return INVALID_TYPE_ID;
    }
    if (kind != RefType::kShared && kind != RefType::kWeak && kind != RefType::kUnique) {
        actx.ctx().diag().report(Severity::Error, range, "access policy is only valid for reference kinds 'shared'/'weak'/'unique', got '{}'",
                                 refTypeName(kind));
        return INVALID_TYPE_ID;
    }
    const TypeRegistry& reg = actx.ctx().types();
    const auto pid = reg.findType(args->at(1));
    if (!pid.has_value() || !reg.isAccessPolicyTypeFor(kind, *pid)) {
        actx.ctx().diag().report(Severity::Error, range,
                                 "unknown access policy type '{}' for reference kind '{}' (expected a policy registered for this kind, e.g. AccessMutex for "
                                 "shared/weak; 'unique' is monopolistic and takes no access policy)",
                                 args->at(1), refTypeName(kind));
        return INVALID_TYPE_ID;
    }
    return *pid;
}

/// Резолв и валидация ОПЦИОНАЛЬНОГО класса реализации механизма доступа (3-й аргумент
/// `@[reftype("<kind>", <policy>, <impl>)@]`). Реализация - РЕАЛЬНЫЙ тип реестра (напр. нативный
/// класс-примитив): она ПОВЕРХ политики (политика - семантика, impl - механизм), поэтому impl
/// допустим только при явной политике. Применяется только к видам с синхронизацией
/// (shared/weak); для unique локер/политику реализация не заменяет.
/// Возвращает TypeId реализации либо INVALID_TYPE_ID (нет аргумента / ошибка диагностирована).
[[nodiscard]] inline TypeId resolveImpl(AnalysisContext& actx, const std::vector<std::string>* args, RefType kind, TypeId policy, MapperRange range) {
    if (args == nullptr || args->size() < 3 || args->at(2).empty()) {
        return INVALID_TYPE_ID;
    }
    if (policy == INVALID_TYPE_ID) {
        actx.ctx().diag().report(Severity::Error, range, "implementation class requires an explicit access policy (2nd argument), got kind '{}'",
                                 refTypeName(kind));
        return INVALID_TYPE_ID;
    }
    if (kind != RefType::kShared && kind != RefType::kWeak) {
        actx.ctx().diag().report(Severity::Error, range, "implementation class is only valid for reference kinds 'shared'/'weak', got '{}'", refTypeName(kind));
        return INVALID_TYPE_ID;
    }
    const TypeRegistry& reg = actx.ctx().types();
    const auto iid = reg.findType(args->at(2));
    if (!iid.has_value()) {
        actx.ctx().diag().report(Severity::Error, range,
                                 "unknown implementation type '{}' (expected a type registered in the TypeRegistry, e.g. a native class)", args->at(2));
        return INVALID_TYPE_ID;
    }
    return *iid;
}

/// Применяет заявленный вид ссылки к базовому типу.
///  - базовый тип без вида (kValue)  -> оборачивает его заявленным видом;
///  - базовый тип несёт тот же вид    -> без изменений (кроме заданного accessPolicy - пересобирает);
///  - виды разные                     -> Error «reference kind mismatch» + возврат base без изменений.
/// @param subjectName имя переменной для сообщения (пусто - без имени);
/// @param actualLabel как назвать фактический тип в сообщении («type» / «deduced type»);
/// @param style       формат отображения фактического вида.
/// @param accessPolicy  РЕАЛЬНЫЙ тип политики доступа (shared/weak/unique): входит в тип.
/// @param impl          класс реализации механизма доступа (3-й аргумент reftype): входит в тип.
[[nodiscard]] inline TypeId applyDeclaredRefKind(AnalysisContext& actx, TypeId base, RefType declared, MapperRange range, std::string_view subjectName,
                                                 std::string_view actualLabel, RefNameStyle style, TypeId accessPolicy = INVALID_TYPE_ID,
                                                 TypeId impl = INVALID_TYPE_ID) {
    // Зарезервированные, но нереализованные виды (`rref`/`ptrptr`) достижимы через
    // `@[reftype(...)]`; операций/lowering для них нет - явная диагностика (без тихого fallback).
    if (!isSupportedRefKind(declared)) {
        actx.ctx().diag().report(Severity::Error, range, "reference kind '{}' is not implemented; use value/shared/weak/unique/ptr/ref/locker",
                                 refTypeName(declared));
        return base;
    }
    const RefType actual = getRefType(getKindFromId(base));
    if (refKindCompatible(declared, actual)) {
        TypeRegistry& reg = actx.ctx().types();
        if (actual == RefType::kValue) {
            return reg.applyRefType(base, declared, INVALID_TYPE_ID, accessPolicy, impl);
        }
        // Базовый тип уже несёт вид: при заданной политике/реализации пересобираем узел
        // (fast-path бит политику/impl не хранит) - `shared<T>` + policy -> `shared<T, policy[, impl]>`.
        if (accessPolicy != INVALID_TYPE_ID || impl != INVALID_TYPE_ID) {
            const TypeId pointee = reg.getPointeeType(base);
            if (pointee != INVALID_TYPE_ID) {
                return reg.applyRefType(pointee, actual, INVALID_TYPE_ID, accessPolicy, impl);
            }
        }
        return base;
    }
    const std::string namePart = subjectName.empty() ? std::string{} : " '" + std::string(subjectName) + "'";
    const std::string actualName = (style == RefNameStyle::Full) ? actx.ctx().types().getFullTypeName(base) : std::string(refTypeName(actual));
    actx.ctx().diag().report(Severity::Error, range, "reference kind mismatch: variable{} is '{}' but its {} is '{}'", namePart, refTypeName(declared),
                             actualLabel, actualName);
    return base;
}

/// Единая точка матрицы совместимости ортогональных квалификаторов с видом ссылки
/// (types/REFType.md §14). Проверяет ПРИМЕНИМОСТЬ квалификатора (аргументы - в профильных хелперах):
///   deleter  -> владеющие виды (unique/shared);
///   lifetime -> невладеющий view (Native-ось: ptr/ref/rref/ptrptr);
///   pin      -> механика не реализована -> Error (пока не поддержан, консистентно с rref/ptrptr).
/// @return true, если квалификатор применим (иначе - диагностика и false).
inline bool checkQualifierApplicable(AnalysisContext& actx, std::string_view qualifier, RefType kind, MapperRange range) {
    if (qualifier == attr::Deleter) {
        if (kind != RefType::kUnique && kind != RefType::kShared) {
            actx.ctx().diag().report(Severity::Error, range, "attribute '{}' is only valid for owning reference kinds 'unique'/'shared', got '{}'",
                                     attr::Deleter, refTypeName(kind));
            return false;
        }
        return true;
    }
    if (qualifier == attr::Lifetime) {
        if (!isNativeRefKind(kind)) {
            actx.ctx().diag().report(Severity::Error, range, "attribute '{}' is only valid for non-owning reference views (ptr/ref/rref), got '{}'",
                                     attr::Lifetime, refTypeName(kind));
            return false;
        }
        return true;
    }
    if (qualifier == attr::Pin) {
        actx.ctx().diag().report(Severity::Error, range, "attribute '{}' is not implemented yet (address-stability enforcement is a separate track)",
                                 attr::Pin);
        return false;
    }
    return true;
}

/// Валидация контракта `@[lifetime(<area>[, <name>])]` (types/REFType.md).
///   area (обязателен): self | param | var | named | program | thread | scope | _
///   name (опционален):  обязателен только для param/var/named, иначе запрещён
/// `_` = «выведи здесь» (элизия позиции) - принимается без диагностики; отсутствие атрибута =
/// вывод по умолчанию. Применимость (только Native-ось) проверяет общая матрица
/// checkQualifierApplicable(attr::Lifetime, ...). Инференс регионов и escape-правила - отдельный трек.
/// @return true, если контракт валиден (иначе - диагностика и false).
inline bool validateLifetimeArgs(AnalysisContext& actx, const std::vector<std::string>* args, RefType kind, MapperRange range) {
    if (!checkQualifierApplicable(actx, attr::Lifetime, kind, range)) {
        return false;
    }
    if (args == nullptr || args->empty()) {
        actx.ctx().diag().report(Severity::Error, range, "attribute 'lifetime' requires an area argument, e.g. @[lifetime(self)] or @[lifetime(param, x)]");
        return false;
    }
    if (args->size() > 2) {
        actx.ctx().diag().report(Severity::Error, range, "attribute 'lifetime' accepts at most 2 arguments: area[, name]");
        return false;
    }
    static constexpr std::string_view kAreas[] = {"self", "param", "var", "named", "program", "thread", "scope", "_"};
    const std::string& area = args->at(0);
    bool known = false;
    for (std::string_view a : kAreas) {
        if (area == a) {
            known = true;
            break;
        }
    }
    if (!known) {
        actx.ctx().diag().report(Severity::Error, range, "unknown 'lifetime' area '{}' (expected self/param/var/named/program/thread/scope/_)", area);
        return false;
    }
    const bool needsName = (area == "param" || area == "var" || area == "named");
    const bool hasName = args->size() == 2 && !args->at(1).empty();
    if (needsName && !hasName) {
        actx.ctx().diag().report(Severity::Error, range, "attribute 'lifetime' area '{}' requires a name as the second argument", area);
        return false;
    }
    if (!needsName && hasName) {
        actx.ctx().diag().report(Severity::Error, range, "attribute 'lifetime' area '{}' does not take a name argument", area);
        return false;
    }
    return true;
}

} // namespace trust
