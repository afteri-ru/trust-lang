#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "session/context.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "types/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/call.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
#include <format>
#include <limits>
#include <memory>
#include <optional>
namespace trust {

namespace {

/// Квалификаторы функции из атрибутов: лидирующие (__attribute__((const/pure)), constexpr)
/// и завершающий (noexcept).
void initFuncQualifiers(const FuncDecl& func, const AttrPool& attrs, std::string& lead, std::string& trail) {
    if (func.has_attr(attrs, attr::FuncConst)) {
        lead += "__attribute__((const)) ";
    }
    if (func.has_attr(attrs, attr::FuncPure)) {
        lead += "__attribute__((pure)) ";
    }
    if (func.has_attr(attrs, attr::FuncConstexpr)) {
        lead += "constexpr ";
    }
    if (func.has_attr(attrs, attr::NoExcept)) {
        trail += " noexcept";
    }
}

} // namespace

struct DeclEmitter::Signature {
    bool funcRangeValid = false;
    bool isEntry = false;
    bool emittedDefault = false;
    std::string name;
    std::string retType;
    std::string templateHeader;
    std::string lead;
    std::string trail;
    std::string paramsStr;
    std::vector<std::pair<const ArgNode*, uint32_t>> paramNamePositions;
    /// Активные типовые параметры должны жить до конца эмиссии тела (generic-функция).
    std::unique_ptr<ActiveTemplateParamsGuard> templateGuard;
};

bool DeclEmitter::prepareFunctionSignature(const FuncDecl& func_node, bool isMember, std::string_view memberOwner, bool suppressDefaults, Signature& sig) {
    // АЛИАС функции (`f = \path::g;`): C++-функция не эмитится.
    if (func_node.aliasKind() != AliasKind::None) {
        return false;
    }
    // @include/@link собираются ДО ранних выходов: нативные импорты/шаблон-типы не эмитят
    // C++-функцию, но их заголовки/линковка обязаны попасть в артефакт.
    m_driver.m_type.collectInclude(func_node);
    m_driver.m_type.collectLinkLib(func_node);
    if (func_node.m_isNativeTemplateCtor) {
        return false;
    }
    if (func_node.m_isNativeTemplateImport) {
        m_ectx.m_declFacts.registerNativeImport(&func_node, func_node.m_nativeName, func_node.text());
        return false;
    }
    if (func_node.m_isNativeImport) {
        m_ectx.m_declFacts.registerNativeImport(&func_node, func_node.m_nativeName, func_node.text());
        return false;
    }

    sig.funcRangeValid = !func_node.range().isInvalid();
    const std::string trustName = std::string(func_node.text());
    const bool emitTrustDefaults = !utils::isNativeName(trustName);
    sig.isEntry = func_node.m_body && !m_ectx.m_mode.inCppBlock && m_ectx.m_output.hiddenNamespaceDepth == 0 && trustName.ends_with("__main__");

    if (const auto guard = stackCheckGuardOf(func_node, m_ectx.m_ctx.attrs())) {
        m_ectx.m_declFacts.registerStackCheck(&func_node, *guard);
    }

    sig.name = cppFuncName(trustName, func_node.m_isOperator, sig.isEntry);
    EXPECT((!func_node.m_isOperator || !sig.name.empty()) && "operator declaration: unknown operator symbol (semantic must validate first)");
    if (!sig.isEntry && !func_node.m_isOperator && !utils::isNativeName(trustName) && !func_node.m_cppSuffix.empty()) {
        sig.name += func_node.m_cppSuffix;
    }
    if (!memberOwner.empty()) {
        sig.name = std::string(memberOwner) + "::" + sig.name;
    }
    if (sig.isEntry) {
        m_ectx.m_declFacts.registerFunctionName(&func_node, sig.name, trustName);
    } else {
        std::string qualified = m_ectx.m_output.qualifiedCppName(sig.name);
        const std::string moduleNamespace = m_ectx.moduleNamespace(func_node.module);
        if (!moduleNamespace.empty()) {
            qualified = moduleNamespace + "::" + qualified;
        }
        m_ectx.m_declFacts.registerFunctionName(&func_node, std::move(qualified), trustName);
    }

    const bool skipForwardDefaults = func_node.m_body.has_value() && m_ectx.m_declFacts.forwardFunctionDefaults.contains(sig.name);
    const bool emitDefaults = emitTrustDefaults && !skipForwardDefaults && !suppressDefaults;

    // Типовые параметры активируются ДО рендера ret/params (`:T` -> `T`), guard живёт до конца
    // эмиссии тела (generic-функция).
    const bool genericFunc = func_node.m_templateParams.has_value() && !func_node.m_isNativeTemplateCtor && !func_node.m_isNativeTemplateImport;
    if (genericFunc) {
        std::vector<std::string> names;
        names.reserve(func_node.m_templateParams->size());
        sig.templateHeader = "template <";
        bool first = true;
        for (const auto& p : *func_node.m_templateParams) {
            if (!p) {
                continue;
            }
            const auto* an = p->is<ArgNode>() ? p->as<ArgNode>() : nullptr;
            if (!first) {
                sig.templateHeader += ", ";
            }
            first = false;
            sig.templateHeader += (an != nullptr && an->isTypePack) ? "typename... " : "typename ";
            sig.templateHeader += std::string(requiredText(*p));
            names.push_back(std::string(requiredText(*p)));
        }
        sig.templateHeader += "> ";
        sig.templateGuard = std::make_unique<ActiveTemplateParamsGuard>(m_ectx, std::move(names));
    }

    sig.retType = "void";
    if (func_node.m_type && func_node.m_type->kind() == ParserToken::Kind::TypeName) {
        const std::string_view rt = requiredText(*func_node.m_type);
        if (rt != type::Void && rt != type::None) {
            sig.retType = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
            if (sig.retType.empty()) {
                return false;
            }
        }
    }
    if (sig.isEntry && func_node.m_type == nullptr) {
        sig.retType = "int";
    }
    initFuncQualifiers(func_node, m_ectx.m_ctx.attrs(), sig.lead, sig.trail);
    if (trustName.starts_with('%') && !func_node.m_body.has_value() && utils::stripNativePrefix(trustName).find("::") == std::string::npos) {
        sig.lead = "extern \"C\" " + sig.lead;
    }

    std::string paramTypesOnly;
    if (func_node.m_params) {
        const uint32_t sigPrefix = static_cast<uint32_t>(sig.templateHeader.length()) + static_cast<uint32_t>(sig.lead.length()) +
                                   static_cast<uint32_t>(sig.retType.length()) + 1 + static_cast<uint32_t>(sig.name.length()) + 1;
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                sig.paramsStr += ", ";
                paramTypesOnly += ", ";
            }
            auto* paramNode = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!paramNode || paramNode->kind() != ParserToken::Kind::ArgNode) {
                sig.paramsStr += "std::any";
                paramTypesOnly += "std::any";
                continue;
            }
            if (isVariadicParamMarker(paramNode)) {
                sig.paramsStr += "...";
                paramTypesOnly += "...";
                continue;
            }
            const size_t paramsCount = func_node.m_params->size();
            const bool postfixDummy = func_node.m_isOperator && op::isIncDecFunction(trustName) &&
                                      ((isMember && paramsCount == 1 && i == 0) || (!isMember && paramsCount == 2 && i == 1));
            if (postfixDummy) {
                sig.paramsStr += "int";
                paramTypesOnly += "int";
                continue;
            }
            std::string paramType;
            if (paramNode->m_type && paramNode->m_type->kind() == ParserToken::Kind::TypeName) {
                const std::string_view pt = requiredText(*paramNode->m_type);
                if (pt == type::Void || pt == type::None) {
                    paramType = "void";
                } else {
                    paramType = m_driver.m_type.emitTypeNameForNode(paramNode->m_type.get());
                    if (paramType.empty()) {
                        return false;
                    }
                }
            } else if (auto aid = m_ectx.m_ctx.readTypes().findType(type_generic::Any)) {
                auto anyName = m_driver.m_type.emitTypeName(*aid);
                EXPECT(anyName.has_value() && "untyped parameter: Any type must have a C++ name");
                paramType = std::move(*anyName);
            } else {
                EXPECT(false && "untyped parameter: Any type must be registered");
            }
            const bool paramReadonly = paramNode->has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly);
            std::string paramCppType = (paramReadonly && paramType != "void") ? "const " + paramType : paramType;
            if (paramNode->isTypePack) {
                paramCppType += "...";
            }
            paramTypesOnly += paramCppType;
            std::string paramName = utils::cppDeclName(requiredText(*paramNode));
            if (paramName.empty()) {
                sig.paramsStr += paramCppType;
            } else {
                const uint32_t namePos = sigPrefix + static_cast<uint32_t>(sig.paramsStr.size()) + static_cast<uint32_t>(paramCppType.length()) + 1;
                sig.paramsStr += paramCppType + " " + paramName;
                sig.paramNamePositions.emplace_back(paramNode, namePos);
            }
            if (emitDefaults && paramNode->m_value) {
                sig.paramsStr += " = ";
                sig.paramsStr += m_driver.renderExprToString(paramNode->m_value.get());
                sig.emittedDefault = true;
            }
        }
    }
    if (sig.isEntry) {
        m_ectx.m_unit.entryParams = paramTypesOnly;
        m_ectx.m_unit.sawEntry = true;
    }
    return true;
}

void DeclEmitter::emitFunctionSignature(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx) {
    const std::string text = sig.templateHeader + std::format("{}{} {}({}){}", sig.lead, sig.retType, sig.name, sig.paramsStr, sig.trail);
    m_ectx.m_ctx.source().output_append(output_idx, text);
    if (!func_node.m_body.has_value() && sig.emittedDefault) {
        m_ectx.m_declFacts.forwardFunctionDefaults.insert(sig.name);
    }
    // Имя функции выводится сразу после "<lead>ret_type " (offset = lead + ret + 1).
    if (sig.funcRangeValid && !sig.name.empty()) {
        const MapperLocation trustFnBegin = m_ectx.m_ctx.source().makeLoc(func_node.range().begin.fileIdx(), func_node.range().begin.offset());
        const MapperLocation trustFnEnd = m_ectx.m_ctx.source().makeLoc(
            trustFnBegin.fileIdx(), trustFnBegin.offset() + static_cast<uint32_t>(utils::stripNativePrefix(func_node.text()).size()));
        const MapperRange trustFnNameRange(trustFnBegin, trustFnEnd);
        mapDeclaredName(output_idx, trustFnNameRange, static_cast<uint32_t>(sig.lead.length()) + static_cast<uint32_t>(sig.retType.length()) + 1,
                        func_node.text(), sig.name);
    }
    for (const auto& [paramNode, namePos] : sig.paramNamePositions) {
        const std::string rawParam = std::string(requiredText(*paramNode));
        if (rawParam.empty()) {
            continue;
        }
        mapDeclaredName(output_idx, paramNode->range(), namePos, rawParam, utils::cppDeclName(rawParam));
    }
    if (sig.funcRangeValid && !m_ectx.m_ctx.source().mappingSuppressed()) {
        m_ectx.m_ctx.source().mapStop(func_node.range());
    }
}

void DeclEmitter::emitFunctionBody(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx) {
    if (!func_node.m_body || m_ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_ctx.source().output_append(output_idx, ";");
        return;
    }
    const MapperRange blockRange = func_node.blockRange();
    std::string beforeClose;
    if (sig.isEntry && !bodyEndsWithReturn(*func_node.m_body)) {
        beforeClose = "return 0;";
    }
    std::string afterOpen;
    if (sig.isEntry && analysis::stackCheckActive(m_ectx.m_behavioral)) {
        std::string init;
        const auto funcs = m_ectx.m_behavioral.stackCheckFunctions;
        if (!funcs.empty()) {
            const auto addrs = m_ectx.resolveStackCheckAddresses(funcs);
            if (!addrs.empty()) {
                std::string list;
                for (size_t i = 0; i < addrs.size(); ++i) {
                    if (i) {
                        list += ", ";
                    }
                    list += addrs[i];
                }
                init += "trust::stack_check::set_limit({" + list + "});\n";
            }
        }
        if (auto reserve = m_ectx.m_behavioral.stackCheckReserve) {
            init += "trust::stack_check::set_reserve(" + std::to_string(*reserve) + ");\n";
        }
        if (!init.empty()) {
            m_driver.m_type.recordRequiredInclude("@trust/stack_check.hpp");
            afterOpen = init;
        }
    }
    std::vector<AstNodePtr> preTrust;
    std::vector<AstNodePtr> postTrust;
    for (const auto& t : func_node.m_trust) {
        if (!t) {
            continue;
        }
        if (t->is<TrustContract>() && t->as<TrustContract>()->kind == PropertyKind::Post) {
            postTrust.push_back(t);
        } else {
            preTrust.push_back(t);
        }
    }
    const bool isVoidFunc = (sig.retType == "void");
    m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel()});
    m_driver.m_stmt.emitBlockBodyToFile(*func_node.m_body, blockRange, output_idx, /*mapBlock=*/true, beforeClose, afterOpen,
                                        preTrust.empty() ? nullptr : &preTrust, (isVoidFunc && !postTrust.empty()) ? &postTrust : nullptr);
    m_ectx.m_output.scopeStack.pop_back();
}

void DeclEmitter::generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx, bool isMember, std::string_view memberOwner, bool suppressDefaults) {
    Signature sig;
    if (!prepareFunctionSignature(func_node, isMember, memberOwner, suppressDefaults, sig)) {
        return;
    }
    // Заголовок модуля эмитит ту же сигнатуру с подавленным маппингом (позиции принадлежат `.cppt`).
    if (sig.funcRangeValid && !m_ectx.m_ctx.source().mappingSuppressed()) {
        m_ectx.m_ctx.source().mapStart(func_node.range(), output_idx);
    }
    emitFunctionSignature(func_node, sig, output_idx);
    emitFunctionBody(func_node, sig, output_idx);
}

// Лямбда-выражение как ЗНАЧЕНИЕ: `[captures](params)[ -> Ret] { body }`.
// Захваты - только имена по значению (C++-захват копией `[c_x, c_y]`); параметры/тело - как у функции.
// Специализированный путь: НЕ generateFuncDeclToFile (тот эмитит top-level определение и ломает выражение).
void DeclEmitter::emitLambdaExpr(const FuncDecl& func_node) {
    // Список захватов (по значению): trust-имя -> C++-идентификатор (c_x).
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "[");
    bool firstCap = true;
    if (func_node.m_captures) {
        for (const auto& cap : *func_node.m_captures) {
            if (!cap || cap->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (!firstCap) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
            }
            firstCap = false;
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, utils::cppDeclName(requiredText(*cap)));
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "](");
    // Параметры.
    if (func_node.m_params) {
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ", ");
            }
            const auto* p = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!p || p->kind() != ParserToken::Kind::ArgNode || requiredText(*p) == "...") {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, "...");
                continue;
            }
            // Тип параметра: явный (emitTypeNameForNode) либо :Any (std::any).
            std::string ptype;
            if (p->m_type) {
                if (p->m_type->kind() == ParserToken::Kind::TypeName) {
                    const std::string_view pt = requiredText(*p->m_type);
                    if (pt == type::Void || pt == type::None) {
                        ptype = "void";
                    } else {
                        ptype = m_driver.m_type.emitTypeNameForNode(p->m_type.get());
                    }
                } else {
                    ptype = m_driver.m_type.emitTypeNameForNode(p->m_type.get());
                }
            } else if (auto anyId = m_ectx.m_ctx.readTypes().findType(type_generic::Any)) {
                auto anyName = m_driver.m_type.emitTypeName(*anyId);
                ptype = anyName ? std::move(*anyName) : std::string("std::any");
            } else {
                ptype = "std::any";
            }
            if (ptype.empty()) {
                ptype = "std::any";
            }
            if (ptype != "void" && p->has_attr(m_ectx.m_ctx.attrs(), attr::ReadOnly)) {
                ptype = "const " + ptype;
            }
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ptype);
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " ");
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, utils::cppDeclName(requiredText(*p)));
            if (p->m_value) {
                m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " = ");
                m_driver.emitExpr(p->m_value.get());
            }
        }
    }
    m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ")");
    // Тип возврата - только при явной аннотации (`void` для Void/None).
    if (func_node.m_type) {
        std::string ret;
        if (func_node.m_type->kind() == ParserToken::Kind::TypeName) {
            const std::string_view rt = requiredText(*func_node.m_type);
            if (rt == type::Void || rt == type::None) {
                ret = "void";
            } else {
                ret = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
            }
        } else {
            ret = m_driver.m_type.emitTypeNameForNode(func_node.m_type.get());
        }
        if (!ret.empty()) {
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " -> ");
            m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, ret);
        }
    }
    // Тело.
    if (func_node.m_body && !m_ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel()});
        m_driver.m_stmt.emitBlockBodyToFile(*func_node.m_body, func_node.blockRange(), m_ectx.m_output.out, /*mapBlock=*/true, {}, {}, nullptr, nullptr);
        m_ectx.m_output.scopeStack.pop_back();
    } else {
        m_ectx.m_ctx.source().output_append(m_ectx.m_output.out, " {}");
    }
}

} // namespace trust
