#pragma once

// include/semantic/lint.hpp
// Опциональный анализатор (InlineAnalysisHook), подключаемый параллельно к ядру
// разрешения имён. Сообщает о неиспользуемых переменных (semantic::DiagId::UnusedVariable)
// и параметрах функций (semantic::DiagId::UnusedParameter). Режим задаётся строковым
// значением флага semantic::FlagKind::Lint:
//   - (пусто/не задано) - severity из semantic::DiagId::UnusedVariable/UnusedParameter;
//   - "aggressive"      - диагностика становится Error (жёсткая проверка).

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "semantic/symbol_table.hpp"
#include "semantic/diag.hpp"

#include <map>
#include <set>
#include <string>

namespace trust {

class LintHook : public InlineAnalysisHook {
  public:
    explicit LintHook(AnalysisContext& actx);

    std::optional<semantic::FlagKind> gateFlag() const override { return semantic::FlagKind::Lint; }

    void onDeclare(const Symbol& sym) override;
    void onResolve(const AstNodeBase& node, const Symbol* sym) override;
    void finalize() override;

  private:
    AnalysisContext& m_actx;
    bool m_aggressive;
    /// name -> {is_parameter, range}
    std::map<std::string, std::pair<bool, MapperRange>> m_declared;
    std::set<std::string> m_used;
};

} // namespace trust
