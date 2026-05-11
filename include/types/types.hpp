#ifndef TYPES_TYPES_HPP
#define TYPES_TYPES_HPP

#include "utils/error.hpp"
#include "stdlib/category.hpp"
#include "stdlib/buildin.hpp"
#include "types/type_info.hpp"
#include "types/forward.hpp"
#include "types/value.hpp"

#include <algorithm>

#include "types/integers.hpp"
#include "types/numbers.hpp"
#include "types/rational.hpp"
#include "types/strings.hpp"
#include "types/tensors.hpp"
#include "types/templates.hpp"

#include <expected>
#include <stdexcept>
#include <string>
#include <map>
#include <unordered_map>

namespace trust {

class Types {
  public:
    static Types& instance() {
        static Types i;
        return i;
    }

    std::expected<TypeInfo*, std::string> add(TypeInfo i) {
        auto name = type_kind_name(i.id);
        if (m_name_to_kind.contains(std::string(name))) {
            return std::unexpected(std::format("Type name '{}' already exists!", name));
        }
        if (m_registry.contains(i.id)) {
            return std::unexpected(std::format("TypeKind {} already exists!", name));
        }
        m_registry[i.id] = std::move(i);
        m_name_to_kind[std::string(name)] = i.id;
        return &m_registry[i.id];
    }
    TypeInfo& append(TypeInfo i) {
        auto type = add(i);
        if (type) {
            return **type;
        }
        FAULT_AS(std::invalid_argument, "Registration type {} failed!", i.to_string(true));
    }

    const TypeInfo& get(TypeKind id) const {
        auto it = m_registry.find(id);
        if (it == m_registry.end()) {
            FAULT_AS(std::invalid_argument, "TypeKind '{}' not registered!", type_kind_name(id));
        }
        return it->second;
    }

    Category category(TypeKind id) const { return KindOps::category_of(id); }

    std::string_view name(TypeKind id) const { return type_kind_name(id); }
    std::string_view cpp_name(TypeKind id) const { return get(id).cpp_name; }
    const std::vector<std::string>& headers(TypeKind id) const { return get(id).headers; }
    const std::vector<std::string>& libraries(TypeKind id) const { return get(id).libraries; }
    LanguageVersion min_version(TypeKind id) const { return get(id).min_version; }

    TypeKind find(std::string_view n) const {
        auto it = m_name_to_kind.find(std::string(n));
        if (it == m_name_to_kind.end()) {
            FAULT_AS(std::invalid_argument, "Unknown type name '{}'", n);
        }
        return it->second;
    }

    // // Find or register a user-defined type by name
    // TypeInfo find_or_add_user(std::string_view name) {
    //     auto it = m_name_to_kind.find(std::string(name));
    //     if (it != m_name_to_kind.end()) {
    //         return get(it->second);
    //     }
    //     return add_user_type(name);
    // }

    std::string to_string(const Any& v, bool wi = false) const { return stringify_value(v, wi); }
    Any convert(const Any& v, TypeKind t) const { return runtime_convert(v, t); }

    // --- Bulk addition of headers ---
    void add_headers(TypeKind kind, std::initializer_list<std::string> headers) {
        if (!KindOps::is_alias(kind)) {
            FAULT_AS(std::invalid_argument, "For a group update TypeKind '{}' must by alias!", type_kind_name(kind));
        }
        auto cat = KindOps::category_of(kind);
        auto grp = KindOps::group_of(kind);
        for (auto& [k, info] : m_registry) {
            if (KindOps::category_of(k) == cat && KindOps::group_of(k) == grp) {
                append_headers_to(info, headers);
            }
        }
    }
    void add_headers(std::initializer_list<TypeKind> kinds, std::initializer_list<std::string> headers) {
        for (auto kind : kinds) {
            append_headers_to(const_cast<TypeInfo&>(get(kind)), headers);
        }
    }

    // --- Bulk addition of libraries ---
    void add_libraries(TypeKind kind, std::initializer_list<std::string> libraries) {
        if (!KindOps::is_alias(kind)) {
            FAULT_AS(std::invalid_argument, "For a group update TypeKind '{}' must by alias!", type_kind_name(kind));
        }
        auto cat = KindOps::category_of(kind);
        auto grp = KindOps::group_of(kind);
        for (auto& [k, info] : m_registry) {
            if (KindOps::category_of(k) == cat && KindOps::group_of(k) == grp) {
                append_libraries_to(info, libraries);
            }
        }
    }
    void add_libraries(std::initializer_list<TypeKind> kinds, std::initializer_list<std::string> libraries) {
        for (auto kind : kinds) {
            append_libraries_to(const_cast<TypeInfo&>(get(kind)), libraries);
        }
    }

    // // Register a user-defined type (enum, struct, etc.) dynamically
    // TypeInfo &add_user_type(std::string_view name) {
    //     static uint8_t user_kind_counter = 0;
    //     auto kind = KindOps::make_user_kind(Category::User, 0, user_kind_counter++);
    //     TypeInfo ti(kind, name);
    //     m_registry[kind] = ti;
    //     m_name_to_kind[std::string(name)] = kind;
    //     return m_registry[kind];
    // }

    // // Get name for any TypeKind (built-in or user-defined)
    // std::string kind_name_str(TypeKind k) const {
    //     if (KindOps::is_user_defined(k)) {
    //         for (auto &[name, kind] : m_name_to_kind) {
    //             if (kind == k)
    //                 return name;
    //         }
    //     }
    //     return std::string(type_kind_name(k));
    // }

    Types() { register_types(); }
    void register_types();

  private:
    std::map<TypeKind, TypeInfo> m_registry;
    std::unordered_map<std::string, TypeKind> m_name_to_kind;

    static void append_headers_to(TypeInfo& info, std::initializer_list<std::string> headers) {
        for (auto& h : headers) {
            if (std::find(info.headers.begin(), info.headers.end(), h) == info.headers.end()) {
                info.headers.push_back(h);
            }
        }
    }
    static void append_libraries_to(TypeInfo& info, std::initializer_list<std::string> libraries) {
        for (auto& l : libraries) {
            if (std::find(info.libraries.begin(), info.libraries.end(), l) == info.libraries.end()) {
                info.libraries.push_back(l);
            }
        }
    }
};

// // Full name lookup: handles both built-in and user-defined types
// inline std::string type_kind_name_full(TypeKind k) {
//     if (KindOps::is_user_defined(k)) {
//         return Types::instance().kind_name_str(k);
//     }
//     return std::string(type_kind_name(k));
// }

} // namespace trust

#endif // TYPES_TYPES_HPP