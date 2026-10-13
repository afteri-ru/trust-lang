// Негативный fixture для kind_dispatch_guard: НЕ компилируется, используется только
// CMake-тестом (guard обязан найти нарушение вне allowlist).
void bad_dispatch(Node& node) {
    // KIND_DISPATCH_GUARD_BEGIN
    if (node.kind() == ParserToken::Kind::Ident) {
        return;
    }
    // KIND_DISPATCH_GUARD_END
    switch (node.kind()) {
    case ParserToken::Kind::Ident:
        break;
    default:
        break;
    }
}
