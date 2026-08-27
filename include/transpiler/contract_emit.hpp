#pragma once

// include/transpiler/contract_emit.hpp
// Компонент кодогенерации: ContractEmitter. Разделяет CppEmitContext с драйвером CppTranspiler;
// рекурсия/вызовы других компонентов идут через драйвер (friend).

#include "transpiler/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "location/location.hpp"
#include "types/type_id.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace trust {

class CppTranspiler;

class ContractEmitter {
  public:
    explicit ContractEmitter(CppEmitContext& ectx, CppTranspiler& driver)
    : m_ectx(ectx)
    , m_driver(driver) {}
    void emitRuntimeAssertCheck(const CallExpr& call);
    void emitRuntimeAssert(const AstNodeBase* cond, MapperRange range, std::string_view message = "");
    void emitTrustCheck(const TrustContract& tc);
    void emitTrustChecks(const std::vector<AstNodePtr>& trust);
    void emitTypeTrustChecks(const std::vector<AstNodePtr>& conds, std::string_view trustName, std::string_view varCpp);
    void emitTypeChecksAfterAssignment(const AstNodeBase* expr);
    void visit_TrustContract(const TrustContract& n);
    void visit_TrustElem(const TrustElem& node);

  private:
    CppEmitContext& m_ectx;
    CppTranspiler& m_driver;
};

} // namespace trust
