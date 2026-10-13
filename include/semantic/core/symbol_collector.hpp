#pragma once

// include/semantic/core/symbol_collector.hpp
// Сборщик имён и типов объявлений (VarDecl/FuncDecl/TypeDecl/ArgNode) для LSP.
// Подключается параллельно к ядру NameResolutionPass через InlineAnalysisHook
// и пишет результат в AnalysisContext::state().symbolIndex().

#include "semantic/core/inline_hook.hpp"
#include "symbols/symbol_index.hpp"
#include "ast/ast_nodes.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class AnalysisContext; // полное определение в semantic/core/pass.hpp

class SymbolCollectorHook : public InlineAnalysisHook {
  public:
    explicit SymbolCollectorHook(AnalysisContext& actx);

    [[nodiscard]] std::string_view name() const override { return "symbol-collector"; }
    [[nodiscard]] bool isEnabled() const override;

    void onDeclare(const Symbol& sym) override;
    void finalize() override;

  private:
    struct Entry {
        std::string_view name;
        std::string_view namespacePath; ///< namespace-путь (интернированный view; qualified-завершение)
        TypeId type;
        AstNodeBase* decl = nullptr;
        MapperRange scopeRange;
        std::optional<ModuleIndex> module;                      ///< модуль объявления (nullopt — вне модуля)
        VisibilityClass visibility = VisibilityClass::Exported; ///< private/exported в своём модуле
    };

    AnalysisContext& m_actx;
    std::vector<Entry> m_entries;
};

} // namespace trust
