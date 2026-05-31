// attr_builtin.cpp — register_builtin_attrs implementation

#include "ast/attr_builtin.hpp"

namespace trust {

void register_builtin_attrs(AttrPool& pool) {
    using namespace attr_names;

    // Attributes without required parameters
    pool.register_attr(kConst, std::vector<AttrParamType>{}, BuiltinAttrKind::kConst);
    pool.register_attr(kPure, std::vector<AttrParamType>{}, BuiltinAttrKind::kPure);
    pool.register_attr(kSend, std::vector<AttrParamType>{}, BuiltinAttrKind::kSend);
    pool.register_attr(kSync, std::vector<AttrParamType>{}, BuiltinAttrKind::kSync);
    pool.register_attr(kThread, std::vector<AttrParamType>{}, BuiltinAttrKind::kThread);
    pool.register_attr(kReadOnly, std::vector<AttrParamType>{}, BuiltinAttrKind::kReadOnly);
    pool.register_attr(kNoExcept, std::vector<AttrParamType>{}, BuiltinAttrKind::kNoExcept);
    pool.register_attr(kStackGuard, std::vector<AttrParamType>{}, BuiltinAttrKind::kStackGuard);

    // @trust has one required string parameter (the trusted assertion)
    pool.register_attr(kTrust, std::vector<AttrParamType>{AttrParamType::kString}, BuiltinAttrKind::kTrust);

    // @require / @ensure take a range parameter (code block or expression)
    pool.register_attr(kRequire, std::vector<AttrParamType>{AttrParamType::kRange}, BuiltinAttrKind::kRequire);
    pool.register_attr(kEnsure, std::vector<AttrParamType>{AttrParamType::kRange}, BuiltinAttrKind::kEnsure);

    // @depend_macro takes zero or more string/identifier arguments (variadic)
    pool.register_variadic_attr(kDependMacro, AttrParamType::kString, BuiltinAttrKind::kDependMacro);
}

} // namespace trust