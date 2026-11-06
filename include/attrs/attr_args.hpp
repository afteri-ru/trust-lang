#pragma once

#include "attrs/attr_pool.hpp"
#include "utils/error.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

/// Первый аргумент атрибута `attr_name` у узла. nullptr - атрибута нет, аргументов нет или
/// первый аргумент пуст.
template <typename Node>
[[nodiscard]] const std::string* firstAttrArg(const AttrPool& pool, const Node& node, std::string_view attr_name) {
    const auto id = pool.lookup(attr_name);
    if (!id.has_value() || !node.has_attr(*id)) {
        return nullptr;
    }
    const std::vector<std::string>* args = node.attr_args(*id);
    if (args == nullptr || args->empty() || args->at(0).empty()) {
        return nullptr;
    }
    return &args->at(0);
}

/// Директива из аргумента `@[include(...)]` по контракту `attr::Include`:
///   '@'-префикс (пакетный заголовок) - возвращается как есть (путь резолвит потребитель);
///   '"...'/'<...' - аргумент задаёт директиву целиком;
///   голое имя - угловой инклуд по умолчанию.
/// Пустой заголовок недопустим (гарантия `firstAttrArg`) - молчаливой пустой директивы нет.
[[nodiscard]] inline std::string includeDirective(std::string_view header) {
    EXPECT(!header.empty() && "include header must not be empty");
    if (header.front() == '@') {
        return std::string(header);
    }
    if (header.front() == '"' || header.front() == '<') {
        return "#include " + std::string(header);
    }
    return "#include <" + std::string(header) + ">";
}

} // namespace trust
