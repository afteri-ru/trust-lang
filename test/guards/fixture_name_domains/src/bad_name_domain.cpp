// Негативный fixture для name_domains_guard: НЕ компилируется, используется только
// CMake-тестом (guard обязан найти нарушение вне allowlist).
std::string bad_storage_key(int module, std::string_view name) {
    std::string key;
    key.push_back('\x1f');
    key.append(name);
    return key;
}

std::string bad_qualified(std::string_view ns, std::string_view name) {
    return std::string(ns) + "::" + std::string(name);
}

void bad_namespace_path(std::string& out, std::string_view segment) {
    out += "::";
    out.append(segment);
}
