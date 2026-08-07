#pragma once

// include/semantic/symbol_collector.hpp
// Сборщик имён и типов объявлений (VarDecl/FuncDecl/TypeDecl/ArgNode) для LSP.
// Подключается параллельно к ядру NameResolutionPass через InlineAnalysisHook
// и пишет результат в AnalysisContext::symbolIndex().

#include "semantic/inline_hook.hpp"
#include "semantic/symbol_index.hpp"
#include "ast/ast_nodes.hpp"

#include <string>
#include <vector>

namespace trust {

class AnalysisContext; // полное определение в semantic/pass.hpp

class SymbolCollectorHook : public InlineAnalysisHook {
  public:
    explicit SymbolCollectorHook(AnalysisContext& actx);

    std::optional<FlagKind> gateFlag() const override { return FlagKind::Symbols; }

    void onDeclare(const Symbol& sym) override;
    void finalize() override;

  private:
    struct Entry {
        std::string name;
        TypeId type;
        AstNodeBase* decl = nullptr;
        MapperRange scopeRange;
    };

    AnalysisContext& m_actx;
    std::vector<Entry> m_entries;
};

} // namespace trust
