#pragma once

// include/semantic/lint.hpp
// Опциональный анализатор (InlineAnalysisHook), подключаемый параллельно к ядру
// разрешения имён. Сообщает о неиспользуемых переменных. Режим задаётся строковым
// значением флага FlagKind::Lint:
//   - (пусто/не задано) - severity из OptKind::UnusedVar;
//   - "aggressive"      - диагностика становится Error (жёсткая проверка).

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "semantic/symbol_table.hpp"

#include <map>
#include <set>
#include <string>

namespace trust {

class LintHook : public InlineAnalysisHook {
  public:
    explicit LintHook(AnalysisContext& actx);

    std::optional<FlagKind> gateFlag() const override { return FlagKind::Lint; }

    void onDeclare(const Symbol& sym) override;
    void onResolve(const AstNodeBase& node, const Symbol* sym) override;
    void finalize() override;

  private:
    AnalysisContext& m_actx;
    bool m_aggressive;
    std::map<std::string, MapperRange> m_declared;
    std::set<std::string> m_used;
};

} // namespace trust
