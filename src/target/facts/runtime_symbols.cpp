// Реализация списка рантайм-символов (единственный источник — X-macro таблица заголовка).
#include "target/runtime_symbols.hpp"

namespace trust {

namespace {

std::vector<RuntimeSymbol> buildRuntimeSymbols() {
    std::vector<RuntimeSymbol> symbols;
    symbols.reserve(static_cast<std::size_t>(RuntimeSymbolId::kCount));
    for (std::size_t i = 0; i < static_cast<std::size_t>(RuntimeSymbolId::kCount); ++i) {
        const auto id = static_cast<RuntimeSymbolId>(i);
        const std::span<const std::string_view> headers = runtimeSymbolHeaders(id);
        symbols.push_back(RuntimeSymbol{std::string(runtimeSymbolName(id)), std::vector<std::string>(headers.begin(), headers.end())});
    }
    return symbols;
}

} // namespace

const std::vector<RuntimeSymbol>& runtimeSymbols() {
    static const std::vector<RuntimeSymbol> kSymbols = buildRuntimeSymbols();
    return kSymbols;
}

} // namespace trust