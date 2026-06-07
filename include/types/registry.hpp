#pragma once

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

#include "types/type_id.hpp"
#include "ast/attr.hpp"
#include "diag/location.hpp"

namespace trust {

// Forward declarations
class Context;

// ── TypeData — дополнительные данные для разных категорий типов ──

// 1. Простой тип (примитив/алиас) — дополнительных данных не нужно
struct SimpleTypeData {};

// 2. Функция / прототип вызова: return + параметры + variadic type
struct FunctionTypeData {
    TypeId returnType;
    std::vector<TypeId> paramTypes;
    TypeId variadicType{INVALID_TYPE_ID}; // INVALID = не variadic,
                                          // AnyId = variadic с любым типом,
                                          // иначе = variadic только этого типа
};

// 3. Параметризованный тип / Template: сам шаблон + аргументы
//    Например: Vector<Int32> → templateTypeId=Vector(T), args=[Int32]
struct TemplateTypeData {
    TypeId templateTypeId;    // TypeId самого шаблона (e.g. Vector<T>)
    std::vector<TypeId> args; // аргументы шаблона (e.g. Int32)
};

// 4. Массив: тип элемента + размерности
struct ArrayTypeData {
    TypeId elementType;
    std::vector<uint64_t> dimensions; // пусто = incomplete, [n] = constant
    bool isVariadic{false};           // срез/открытый массив
};

// 5. Указатель на член структуры
struct MemberPointerTypeData {
    TypeId classType;  // класс/структура
    TypeId memberType; // тип члена
};

// 6. Pack expansion (variadic template параметры)
struct PackExpansionTypeData {
    TypeId pattern; // тип, который повторяется
};

// ── Объединение вариантов ──
using TypeData = std::variant<SimpleTypeData, FunctionTypeData, TemplateTypeData, ArrayTypeData, MemberPointerTypeData, PackExpansionTypeData>;

// ── TypeDataKind — идентификатор варианта TypeData ──────────
enum class TypeDataKind : uint8_t {
    kSimple,
    kFunction,
    kTemplate,
    kArray,
    kMemberPointer,
    kPackExpansion,
};

// ── TypeKey для структурного интернирования ──────────────
struct TypeKey {
    TypeKind kind;                // TypeKind без registry_index
    std::vector<TypeId> children; // все дочерние TypeId для сравнения

    bool operator==(const TypeKey& other) const noexcept { return kind == other.kind && children == other.children; }
};

struct TypeKeyHash {
    size_t operator()(const TypeKey& key) const noexcept {
        size_t h = std::hash<uint32_t>{}(static_cast<uint32_t>(key.kind));
        for (auto child : key.children) {
            h ^= std::hash<uint64_t>{}(child) + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

// ── Type descriptor ──────────────────────────────────────
struct TypeDescriptor {
    std::string_view name;            // canonical name (e.g., "Int32", "Vector")
    std::vector<AttrId> attrs;        // атрибуты
    MapperRange sourceRange{};        // position of type declaration in source file
    std::string cppName;              // C++ имя для codegen (e.g. "int32_t")
    std::string preprocInclude;       // директива препроцессора для включения в C++ код (e.g. "#include <cstdint>"), пусто если не требуется
    TypeId baseType{INVALID_TYPE_ID}; // для алиас-цепочки: A → Byte → Int32
    std::optional<TypeData> data;     // nullopt = forward declaration (incomplete type)
};

// ── TypeRegistry ─────────────────────────────────────────
class TypeRegistry {
  public:
    explicit TypeRegistry(Context* ctx);

    // ── Основные операции ──

    TypeId getType(std::string_view name) const;
    std::optional<TypeId> findType(std::string_view name) const noexcept;
    const TypeDescriptor* lookup(TypeId id) const;
    std::string getFullTypeName(TypeId id) const;

    /// Returns the C++ name for a given TypeId (e.g. "int32_t" for Int32).
    /// Returns std::unexpected with an error message if no C++ name is registered.
    std::expected<std::string, std::string> getCppTypeName(TypeId id) const;

    /// Register a user-defined type based on an existing TypeId.
    /// Extracts TypeKind from baseTypeId, assigns a new registry_index,
    /// stores name + attrs + sourceRange + preprocInclude in m_descriptors.
    /// @return TypeId of the new type, or INVALID_TYPE_ID on duplicate.
    TypeId registerType(std::string_view name, TypeId baseTypeId, std::vector<AttrId> attrs = {}, MapperRange sourceRange = {},
                        std::string_view preprocInclude = {});

    /// Structural uniquing: create or retrieve a structural type identified by
    /// kind + children. Used for FunctionType, TemplateType, ArrayType, etc.
    TypeId getOrCreateStructuralType(std::string_view name, TypeKind kind, std::vector<TypeId> children, std::optional<TypeData> data = std::nullopt,
                                     std::string_view preprocInclude = {});

    /// Create or retrieve a FunctionType by structural uniquing.
    /// @param returnType  TypeId of the return type (INVALID_TYPE_ID = Void).
    /// @param paramTypes  List of parameter TypeIds.
    /// @param variadicType INVALID_TYPE_ID = not variadic.
    /// @return TypeId of the created FunctionType.
    TypeId getOrCreateFunctionType(TypeId returnType, std::vector<TypeId> paramTypes, TypeId variadicType = INVALID_TYPE_ID);

    /// Returns the preprocInclude for a registered type (by TypeId).
    /// For builtin types (no descriptor) returns empty string_view.
    std::string_view getPreprocInclude(TypeId id) const noexcept;

    /// Returns the sourceRange for a registered type (by TypeId).
    MapperRange getTypeSourceRange(TypeId id) const;

    /// Returns the canonical TypeId, i.e. resolves alias chains via baseType.
    /// For builtin types returns id unchanged.
    /// For aliases follows baseType until a non-alias type is found.
    /// For structural types (no baseType) returns id unchanged.
    TypeId getCanonicalTypeId(TypeId id) const noexcept;

    // ── Type info accessors ──

    /// Returns the baseType from TypeDescriptor, or INVALID_TYPE_ID if not an alias.
    TypeId getBaseType(TypeId id) const noexcept;

    /// Safely access TypeData from TypeDescriptor.
    const std::optional<TypeData>& getTypeData(TypeId id) const noexcept;

    /// Typed accessor — returns pointer to the requested TypeData variant,
    /// or nullptr if the type is not of the requested kind.
    template <typename T>
    const T* getTypeDataAs(TypeId id) const noexcept {
        const auto& data = getTypeData(id);
        if (data && std::holds_alternative<T>(*data))
            return &std::get<T>(*data);
        return nullptr;
    }

    /// Check if the type data holds a specific variant kind.
    bool isTypeDataKind(TypeId id, TypeDataKind kind) const noexcept;

    /// Check if the type has non-nullopt data (complete type).
    bool isCompleteType(TypeId id) const noexcept;

    /// Check if the type has nullopt data (forward declaration).
    bool isForwardDecl(TypeId id) const noexcept;

  private:
    TypeId registerBuiltinType(std::string_view name, Group group, uint8_t data = 0, std::string_view cpp_name = {}, std::string_view preproc_include = {});
    void registerBuiltinTypes();

    Context* m_ctx;

    // ── Lookup by name ──
    std::unordered_map<std::string_view, TypeId> m_name_to_id;
    std::vector<TypeDescriptor> m_descriptors;

    // ── Structural uniquing ──
    std::unordered_map<TypeKey, TypeId, TypeKeyHash> m_structural;
};

} // namespace trust