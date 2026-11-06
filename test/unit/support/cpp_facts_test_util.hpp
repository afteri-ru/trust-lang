#pragma once

// test/unit/support/cpp_facts_test_util.hpp
// Хелперы целевых C++-фактов для юнит-тестов: seed интринсиков (как `Pipeline`), запись
// реализации builtin и чтение через порт `TypeCppFactsView` (R37).

#include "session/context.hpp"
#include "target/target.hpp"
#include "transpiler/type/type_cpp_facts_view.hpp"
#include "transpiler/type/cpp_type_renderer.hpp"
#include "types/type_read.hpp"
#include "types/type_system.hpp"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {
namespace test {

/// Seed целевых C++-фактов встроенных интринсиков в контракт цели (как боевой `Pipeline`).
inline void seedIntrinsicCppFacts(TargetContract& target, const TypeReadModel& read) {
    for (const BuiltinCppSeedEntry& entry : builtinCppSeed()) {
        const std::optional<TypeId> id = read.findType(entry.name);
        if (!id.has_value()) {
            continue;
        }
        CppTypeFacts facts;
        facts.cppName = std::string(entry.cppName);
        for (const std::string_view include : entry.includes) {
            facts.preprocIncludes.emplace_back(include);
        }
        target.typeCppFacts().seed(read.store().cppFactsKeyOf(read.store().desugaredTypeId(*id)), std::move(facts));
    }
}

/// Реализация builtin-типа в тестах: C++-факты в контракт цели (в реальной сборке — stdlib/builtin/*.src).
inline void defineBuiltinImpl(const TypeReadModel& read, TargetContract& target, std::string_view name, std::string cppName,
                              std::vector<std::string> includes = {}, std::vector<std::string> libs = {}) {
    const TypeId id = read.getType(name);
    CppTypeFacts facts;
    facts.cppName = std::move(cppName);
    facts.preprocIncludes = std::move(includes);
    facts.libs = std::move(libs);
    target.typeCppFacts().set(read.store().cppFactsKeyOf(read.store().desugaredTypeId(id)), std::move(facts));
}

/// Есть ли у builtin-типа `name` загруженная (пользовательская) реализация.
inline bool builtinImplemented(const TypeReadModel& read, const TargetContract& target, std::string_view name) {
    const TypeId id = read.getType(name);
    return target.typeCppFacts().hasUser(read.store().cppFactsKeyOf(read.store().desugaredTypeId(id)));
}

/// Порт целевых C++-фактов над контекстом (`target` должен быть внедрён через `setTarget`).
inline TypeCppFactsView cppFactsViewOf(const Context& ctx) {
    return TypeCppFactsView(ctx.readTypes(), ctx.target().typeCppFacts());
}

/// Внедряет целевой контракт в контекст и засевает C++-факты встроенных интринсиков (как `Pipeline`).
inline void installTargetFacts(Context& ctx, TargetContract& target) {
    ctx.setTarget(&target);
    seedIntrinsicCppFacts(target, ctx.readTypes());
}

/// C++-рендеринг имени типа для тестов (обёртка `CppTypeRenderer` с портом целевых фактов).
inline std::expected<std::string, std::string> renderCpp(const Context& ctx, TypeId id) {
    return CppTypeRenderer(ctx.readTypes(), cppFactsViewOf(ctx)).render(id);
}
inline std::expected<std::string, std::string> renderCpp(const Context& ctx, QualType state) {
    return CppTypeRenderer(ctx.readTypes(), cppFactsViewOf(ctx)).render(state);
}

/// Реализация builtin-типа с прежней семантикой `BuiltinRegistry::define` (для тестов):
/// имя резолвится, не-builtin/неизвестное — INVALID_TYPE_ID; иначе факты пишутся в `target`, возвращается id.
inline TypeId defineBuiltinImplChecked(const TypeReadModel& read, TargetContract& target, std::string_view name, std::string cppName,
                                       std::vector<std::string> includes = {}, std::vector<std::string> libs = {}) {
    const std::optional<TypeId> id = read.findType(name);
    if (!id.has_value() || !read.store().isBuiltinType(*id)) {
        return INVALID_TYPE_ID;
    }
    defineBuiltinImpl(read, target, name, std::move(cppName), std::move(includes), std::move(libs));
    return *id;
}

/// Регистрация нативного класса + его целевых C++-фактов (C++-имя/include/libs) в `target`.
inline TypeId registerNativeClassFacts(TypeSystem& ts, TargetContract& target, std::string_view name, std::string cppName, MapperRange range = {},
                                       std::string_view include = {}, std::vector<std::string> libs = {},
                                       std::optional<StorageKey> storageKey = std::nullopt) {
    const TypeId id = ts.registrar().registerNativeClass(name, range, storageKey);
    if (id == INVALID_TYPE_ID) {
        return id;
    }
    CppTypeFacts facts;
    facts.cppName = std::move(cppName);
    if (!include.empty()) {
        facts.preprocIncludes.emplace_back(include);
    }
    facts.libs = std::move(libs);
    target.typeCppFacts().set(ts.read().store().cppFactsKeyOf(ts.read().store().desugaredTypeId(id)), std::move(facts));
    return id;
}

/// Регистрация нативного шаблона + его целевых C++-фактов (C++-имя шаблона/include/libs) в `target`.
inline TypeId registerNativeTemplateFacts(TypeSystem& ts, TargetContract& target, std::string_view name, std::string cppTemplate, MapperRange range = {},
                                          std::string_view include = {}, std::vector<std::string> libs = {},
                                          std::optional<StorageKey> storageKey = std::nullopt) {
    const TypeId id = ts.registrar().registerNativeTemplate(name, range, storageKey);
    if (id == INVALID_TYPE_ID) {
        return id;
    }
    CppTypeFacts facts;
    facts.cppTemplate = std::move(cppTemplate);
    if (!include.empty()) {
        facts.preprocIncludes.emplace_back(include);
    }
    facts.libs = std::move(libs);
    target.typeCppFacts().set(ts.read().store().cppFactsKeyOf(ts.read().store().desugaredTypeId(id)), std::move(facts));
    return id;
}

/// Делегат метода builtin с fallback «структурный контейнер → абстрактный builtin» (как прежний аксессор).
inline const std::string* methodDelegateOf(const TypeReadModel& read, const TargetContract& target, TypeId objType, std::string_view method) {
    const TypeId canonical = read.store().desugaredTypeId(objType);
    const MethodDelegates& delegates = target.methodDelegates();
    if (const std::string* found = delegates.findByOwner(canonical, method); found != nullptr) {
        return found;
    }
    const TypeId abstractId = read.builtins().abstractBuiltinOf(canonical);
    return abstractId == INVALID_TYPE_ID ? nullptr : delegates.findByOwner(abstractId, method);
}

} // namespace test
} // namespace trust