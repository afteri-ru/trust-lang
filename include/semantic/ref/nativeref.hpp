#pragma once

// include/semantic/ref/nativeref.hpp
// NativeRefHook: диагностики нативных (сырых) C++-ссылок D3–D7.
//
// Инвалидация производных представлений (`@[borrowed]`, `&?`-weak) выполняется отдельным
// flow-анализом (`BorrowFlowPass`, `semantic/ref/borrow_flow.hpp`); здесь — только контракт
// нативных ссылок:
//   D3/D7 — нативный маркер в возврате/параметрах функции;
//   D4   — нативная ссылка в static/global/TLS-переменной;
//   D5   — сохранение нативной ссылки в переменную внешнего скоупа;
//   D6   — swap нативных ссылок разных уровней скоупа.
//
// Реализация - InlineAnalysisHook: onDeclare (D4 + регистрация), onNode (D3/D5/D6/D7).

#include "semantic/core/inline_hook.hpp"
#include "semantic/core/pass.hpp"
#include "semantic/diag.hpp"
#include "ast/ast_nodes.hpp"
#include "types/typekind.hpp"

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

class NativeRefHook : public InlineAnalysisHook {
  public:
    explicit NativeRefHook(AnalysisContext& actx);

    [[nodiscard]] std::string_view name() const override { return "native-ref"; }

    void enterScope() override;
    void exitScope() override;
    bool onNode(AstNodePtr& node) override;
    void onDeclare(const Symbol& sym) override;

  private:
    /// Один фрейм лексического скоупа (вход/выход синхронны с SymbolTable ядра).
    struct Frame {
        // bare-имя нативной переменной -> (глубина скоупа декларации, вид ссылки).
        // Нативные переменные видны в текущем и внутренних скоупах (поиск сверху вниз).
        std::map<std::string, std::pair<size_t, RefType>> nativeVars;
    };

    std::vector<Frame> m_frames;

    AnalysisContext& m_actx;

    // -- Диагностики нативных (сырых) ссылок (D3..D7) --
    /// Вид нативной ссылки из аннотации типа (`@[reftype("ptr"|"ref")]`). kValue - не нативная.
    static RefType nativeKindOfTypeNode(const AstNodeBase* typeNode);
    /// Регистрирует нативную переменную в текущем фрейме + D4 (static/global native -> error).
    void registerNativeVar(const Symbol& sym, RefType kind);
    /// D3+D7: нативные маркеры в сигнатуре функции (возврат/параметры) -> error.
    void checkFuncSignature(const FuncDecl& fn);
    /// D5: сохранение нативной ссылки в переменную внешнего скоупа -> error.
    void checkAssignIntoOuterNative(const Binary& b);
    /// D6: swap нативных ссылок на разных уровнях скоупов -> error.
    void checkSwapNativeAcrossScopes(const Binary& b);
    /// Находит нативную переменную по bare-имени (фреймы сверху вниз). nullptr - не нативная.
    const std::pair<size_t, RefType>* findNativeVar(const std::string& bare) const;
    /// Производит ли выражение нативную ссылку (копию нативной переменной / результат нативной функции).
    bool exprProducesNativeRef(const AstNodeBase* e) const;
};

} // namespace trust
