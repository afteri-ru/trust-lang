// Реализация хранилища целевых C++-фактов типа и seed-таблицы интринсиков (см. заголовок).
#include "target/type_cpp_facts.hpp"

#include <utility>

namespace trust {

namespace {

constexpr std::string_view kCstdintInclude = "#include <cstdint>";

/// Seed встроенных интринсиков: единственный источник целевых C++-фактов этих типов (R1/R37).
/// Пользовательские факты приходят из реализации/объявления (см. `BuiltinRegistry`/semantic).
const std::vector<BuiltinCppSeedEntry> kBuiltinCppSeed = {
    {.name = "Bool", .cppName = "bool", .includes = {}},
    {.name = "Int8", .cppName = "int8_t", .includes = {kCstdintInclude}},
    {.name = "Int16", .cppName = "int16_t", .includes = {kCstdintInclude}},
    {.name = "Int32", .cppName = "int32_t", .includes = {kCstdintInclude}},
    {.name = "Int64", .cppName = "int64_t", .includes = {kCstdintInclude}},
    {.name = "UInt8", .cppName = "uint8_t", .includes = {kCstdintInclude}},
    {.name = "UInt16", .cppName = "uint16_t", .includes = {kCstdintInclude}},
    {.name = "UInt32", .cppName = "uint32_t", .includes = {kCstdintInclude}},
    {.name = "UInt64", .cppName = "uint64_t", .includes = {kCstdintInclude}},
    {.name = "Float32", .cppName = "float", .includes = {}},
    {.name = "Float64", .cppName = "double", .includes = {}},
    {.name = "CString", .cppName = "const char*", .includes = {}},
};

} // namespace

const std::vector<BuiltinCppSeedEntry>& builtinCppSeed() {
    return kBuiltinCppSeed;
}

void TypeCppFacts::set(TypeId id, CppTypeFacts facts) {
    m_byType[id] = std::move(facts);
    m_user.insert(id);
}

void TypeCppFacts::seed(TypeId id, CppTypeFacts facts) {
    m_byType[id] = std::move(facts);
}

const CppTypeFacts* TypeCppFacts::factsOf(TypeId id) const noexcept {
    const auto it = m_byType.find(id);
    return it == m_byType.end() ? nullptr : &it->second;
}

bool TypeCppFacts::hasUser(TypeId id) const noexcept {
    return m_user.contains(id);
}

std::string_view TypeCppFacts::cppNameOf(TypeId id) const noexcept {
    const CppTypeFacts* facts = factsOf(id);
    return facts != nullptr ? std::string_view(facts->cppName) : std::string_view{};
}

} // namespace trust
