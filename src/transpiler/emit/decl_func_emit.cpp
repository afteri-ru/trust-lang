#include "transpiler/emit/decl_emit.hpp"
#include "transpiler/emit/decl_emit_common.hpp"
#include "transpiler/transpiler.hpp"
#include "transpiler/emit/emit_common.hpp"
#include "ast/ast_nodes.hpp"
#include "attrs/attr_builtin.hpp"
#include "ast/ref_syntax.hpp"
#include "ast/ident_name.hpp"
#include "ast/kind_dispatch.hpp"
#include "ast/token_type.hpp"
#include "diag/registry.hpp"
#include "diag/base_diags.hpp"
#include "syntax/term/term.h"
#include "types/type_system.hpp"
#include "types/types.hpp"
#include "target/runtime_symbols.hpp"
#include "types/intrinsics.hpp"
#include "types/call.hpp"
#include "types/type_id.hpp"
#include "types/names.hpp"
#include "transpiler/emit/diag.hpp"
#include "utils/strings.hpp"
#include <cstdint>
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
    /// Нативный экспорт с C-линковкой: к `lead` добавляется `extern "C" `.
    bool nativeExportCLinkage = false;
    /// namespace-сегменты extern (`extern("ns::fib")`): определение эмитится внутри них.
    std::vector<std::string> namespaces;
    std::string name;
    std::string retType;
    std::string templateHeader;
    std::string lead;
    std::string trail;
    std::string paramsStr;
    /// Позиция ИМЕНИ параметра ОТНОСИТЕЛЬНО начала `paramsStr` (для source-map). Абсолютное
    /// смещение выводится в `emitFunctionSignature` из фактически напечатанного префикса —
    /// формула «<prefix><name>(<params>» существует в одном месте.
    std::vector<std::pair<const ArgNode*, uint32_t>> paramNamePositions;
    /// Активные типовые параметры должны жить до конца эмиссии тела (generic-функция).
    std::unique_ptr<ActiveTemplateParamsGuard> templateGuard;
};

bool DeclEmitter::resolveParamCppType(const ArgNode& param, std::string& cppType) const {
    // ЕДИНЫЙ рендер C++-типа параметра для функций и лямбд. Пустая аннотация — материализуемый
    // Any (тип-имя из реестра), Void/None — "void". ReadOnly — const, type-pack — "...".
    std::string type;
    if (param.m_type && param.m_type->kind() == ParserToken::Kind::TypeName) {
        const std::string_view pt = requiredText(*param.m_type);
        if (pt == type::Void || pt == type::None) {
            type = "void";
        } else {
            type = m_driver.m_type.emitTypeNameForNode(param.m_type.get());
            if (type.empty()) {
                return false; // диагностику уже выдал TypeEmitter
            }
        }
    } else if (auto anyId = m_ectx.m_services.readTypes().findType(type_generic::Any)) {
        auto anyName = m_driver.m_type.emitTypeName(*anyId);
        EXPECT(anyName.has_value() && "untyped parameter: Any type must have a C++ name");
        type = std::move(*anyName);
    } else {
        EXPECT(false && "untyped parameter: Any type must be registered");
    }
    if (param.has_attr(m_ectx.m_services.attrs(), attr::ReadOnly) && type != "void") {
        type = "const " + type;
    }
    if (param.isTypePack) {
        type += "...";
    }
    cppType = std::move(type);
    return true;
}

bool DeclEmitter::resolveReturnCppType(const AstNodePtr& typeNode, std::string& cppType) const {
    // ЕДИНЫЙ рендер типа возврата для функций и лямбд: Void/None (и отсутствие аннотации на этом
    // уровне) — "void"; невыразимый тип — false (диагностика выдана).
    cppType = "void";
    if (!typeNode || typeNode->kind() != ParserToken::Kind::TypeName) {
        return true;
    }
    const std::string_view rt = requiredText(*typeNode);
    if (rt == type::Void || rt == type::None) {
        return true;
    }
    cppType = m_driver.m_type.emitTypeNameForNode(typeNode.get());
    return !cppType.empty();
}

bool DeclEmitter::prepareFunctionSignature(const FuncDecl& func_node, bool isMember, std::string_view memberOwner, bool suppressDefaults, Signature& sig) {
    // АЛИАС имени (`f :- g;`): C++-функция не эмитится.
    if (func_node.aliasKind() != AliasKind::None) {
        return false;
    }
    // @include/@link собираются ДО ранних выходов: нативные импорты/шаблон-типы не эмитят
    // C++-функцию, но их заголовки/линковка обязаны попасть в артефакт.
    m_driver.m_type.collectInclude(func_node);
    m_driver.m_type.collectLinkLib(func_node);
    if (func_node.isNativeImport()) {
        return false;
    }

    sig.funcRangeValid = !func_node.range().isInvalid();
    const std::string trustName = std::string(func_node.text());
    // Экспорт (`@[export("...")]`): символ публикуется под заданным
    // C/C++-именем; trust-манглинг (identity-суффикс), модульная изоляция и trust-defaults
    // не применяются. ABI задан атрибутом: C → `extern "C"`, C++ → без него.
    const std::string_view native = m_ectx.m_services.declCppFacts().cppNameOf(func_node.declId());
    const bool nativeExport = !native.empty();
    const bool emitTrustDefaults = !nativeExport;
    sig.isEntry = func_node.hasBody() && !m_ectx.m_mode.inCppBlock && m_ectx.m_output.hiddenNamespaceDepth == 0 && trustName.ends_with("__main__");

    if (const auto guard = stackCheckGuardOf(func_node, m_ectx.m_services.attrs())) {
        m_ectx.m_declFacts.registerStackCheck(func_node, *guard);
    }

    if (nativeExport) {
        // `extern("ns::fib")`: внутри namespace-сегментов имя — последний компонент,
        // сами сегменты открываются вокруг эмиссии (generateFuncDeclToFile). Разбор
        // `ns::sub::name` — общий с типом/переменной (`splitCppName`).
        const CppQualifiedParts parts = splitCppName(native);
        sig.namespaces = parts.namespaces;
        sig.name = parts.name;
        sig.nativeExportCLinkage = cppLinkageOf(native) == CppLinkage::C;
    } else {
        sig.name = cppName(trustName, sig.isEntry ? CppEntityKind::kEntry : (func_node.m_isOperator ? CppEntityKind::kOperator : CppEntityKind::kFunction));
        EXPECT((!func_node.m_isOperator || !sig.name.empty()) && "operator declaration: unknown operator symbol (semantic must validate first)");
        const std::string_view cppSuffix = m_ectx.m_services.declCppFacts().cppSuffixOf(func_node.declId());
        if (!sig.isEntry && !func_node.m_isOperator && !cppSuffix.empty()) {
            sig.name += cppSuffix;
        }
    }
    if (!memberOwner.empty()) {
        sig.name = std::string(memberOwner) + "::" + sig.name;
    }
    if (sig.isEntry) {
        m_ectx.m_declFacts.registerFunctionName(func_node, sig.name, trustName);
    } else if (nativeExport) {
        // Экспорт: C/C++-имя символа фиксировано атрибутом (модульная изоляция не применяется).
        m_ectx.m_declFacts.registerFunctionName(func_node, std::string(native), trustName);
    } else {
        m_ectx.m_declFacts.registerFunctionName(func_node, m_ectx.m_output.qualifiedCppName(sig.name), trustName);
    }

    const bool skipForwardDefaults = func_node.hasBody() && m_ectx.m_declFacts.forwardFunctionDefaults.contains(sig.name);
    const bool emitDefaults = emitTrustDefaults && !skipForwardDefaults && !suppressDefaults;

    // Типовые параметры активируются ДО рендера ret/params (`:T` -> `T`), guard живёт до конца
    // эмиссии тела (generic-функция). Шапка шаблона строится единым helper'ом (общим с
    // record-классами и обёртками нативных классов).
    if (func_node.m_templateParams.has_value()) {
        const CppTemplateParams tpl = cppTemplateParams(func_node.m_templateParams);
        if (!tpl.names.empty()) {
            sig.templateHeader = tpl.header + " ";
            sig.templateGuard = std::make_unique<ActiveTemplateParamsGuard>(m_ectx, tpl.names);
        }
    }

    sig.retType = "void";
    if (!resolveReturnCppType(func_node.m_type, sig.retType)) {
        return false;
    }
    if (sig.isEntry && func_node.m_type == nullptr) {
        sig.retType = "int";
    }
    initFuncQualifiers(func_node, m_ectx.m_services.attrs(), sig.lead, sig.trail);
    if (sig.nativeExportCLinkage) {
        // extern: C-линковка (extern "C"); имя символа фиксировано атрибутом.
        sig.lead = "extern \"C\" " + sig.lead;
    }

    std::string paramTypesOnly;
    if (func_node.m_params) {
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                sig.paramsStr += ", ";
                paramTypesOnly += ", ";
            }
            auto* paramNode = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!paramNode || paramNode->kind() != ParserToken::Kind::ArgNode) {
                const std::string anyCpp = m_driver.m_type.anyTypeName();
                sig.paramsStr += anyCpp;
                paramTypesOnly += anyCpp;
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
            std::string paramCppType;
            if (!resolveParamCppType(*paramNode, paramCppType)) {
                return false;
            }
            paramTypesOnly += paramCppType;
            std::string paramName = utils::cppDeclName(requiredText(*paramNode));
            if (paramName.empty()) {
                sig.paramsStr += paramCppType;
            } else {
                const uint32_t namePos = static_cast<uint32_t>(sig.paramsStr.size()) + static_cast<uint32_t>(paramCppType.length()) + 1;
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
    if (sig.nativeExportCLinkage) {
        // C-сцепление несовместимо с C++-именами (namespace/templates/std-типы): диагностика,
        // иначе генерируется невалидный C++.
        if (requiresCppLinkage(sig.retType) || requiresCppLinkage(sig.paramsStr)) {
            m_ectx.m_services.report(func_node.range(), diag::DiagId::ParseError,
                                     "C-linkage export '{}' requires C-compatible types (no C++ namespaces/templates)", func_node.text());
            return false;
        }
    }
    return true;
}

void DeclEmitter::emitFunctionSignature(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx) {
    // Позиции для source-map выводятся из фактически напечатанного префикса «<templateHeader><lead><ret> »:
    // offset имени = длина префикса; параметры смещены от начала `paramsStr`.
    const std::string prefix = sig.templateHeader + sig.lead + sig.retType + " ";
    const std::string text = prefix + sig.name + "(" + sig.paramsStr + ")" + sig.trail;
    m_ectx.m_services.source().output_append(output_idx, text);
    const uint32_t namePrefix = static_cast<uint32_t>(prefix.size());
    if (!func_node.hasBody() && sig.emittedDefault) {
        m_ectx.m_declFacts.forwardFunctionDefaults.insert(sig.name);
    }
    if (sig.funcRangeValid && !sig.name.empty()) {
        const MapperLocation trustFnBegin = m_ectx.m_services.source().makeLoc(func_node.range().begin.fileIdx(), func_node.range().begin.offset());
        const MapperLocation trustFnEnd =
            m_ectx.m_services.source().makeLoc(trustFnBegin.fileIdx(), trustFnBegin.offset() + static_cast<uint32_t>(func_node.text().size()));
        const MapperRange trustFnNameRange(trustFnBegin, trustFnEnd);
        mapDeclaredName(output_idx, trustFnNameRange, namePrefix, func_node.text(), sig.name);
    }
    // paramsStr начинается после «<prefix><name>(».
    const uint32_t paramsBegin = namePrefix + static_cast<uint32_t>(sig.name.size()) + 1;
    for (const auto& [paramNode, relNamePos] : sig.paramNamePositions) {
        const std::string rawParam = std::string(requiredText(*paramNode));
        if (rawParam.empty()) {
            continue;
        }
        mapDeclaredName(output_idx, paramNode->range(), paramsBegin + relNamePos, rawParam, utils::cppDeclName(rawParam));
    }
    if (sig.funcRangeValid && !m_ectx.m_services.source().mappingSuppressed()) {
        m_ectx.m_services.source().mapStop(func_node.range());
    }
}

void DeclEmitter::emitFunctionBody(const FuncDecl& func_node, const Signature& sig, MapperFile output_idx) {
    auto* fb = func_node.body();
    if (!fb || m_ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_services.source().output_append(output_idx, ";");
        return;
    }
    const MapperRange blockRange = func_node.blockRange();
    std::string beforeClose;
    if (sig.isEntry && !bodyEndsWithReturn(*fb)) {
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
    m_driver.m_stmt.emitBlockBodyToFile(*fb, blockRange, output_idx, /*mapBlock=*/true, beforeClose, afterOpen, preTrust.empty() ? nullptr : &preTrust,
                                        (isVoidFunc && !postTrust.empty()) ? &postTrust : nullptr);
    m_ectx.m_output.scopeStack.pop_back();
}

void DeclEmitter::generateFuncDeclToFile(const FuncDecl& func_node, MapperFile output_idx, bool isMember, std::string_view memberOwner, bool suppressDefaults) {
    Signature sig;
    if (!prepareFunctionSignature(func_node, isMember, memberOwner, suppressDefaults, sig)) {
        return;
    }
    // Заголовок модуля эмитит ту же сигнатуру с подавленным маппингом (позиции принадлежат `.cppt`).
    // extern: определение живёт ВНУТРИ заданных namespace-сегментов; открываем их до mapStart,
    // чтобы маппинг тела/сигнатуры не смещался.
    for (const std::string& ns : sig.namespaces) {
        m_ectx.m_services.source().output_append(output_idx, cppNamespaceOpen(ns, /*inlineForm=*/true));
    }
    if (sig.funcRangeValid && !m_ectx.m_services.source().mappingSuppressed()) {
        // Диапазон объявления записывается заголовком: в `.cppt` он принадлежит интерфейсу.
        const bool recordRange = m_ectx.m_mode.forwardDeclOnly || m_ectx.m_output.selfHeaderInclude.empty();
        m_ectx.m_services.source().mapStart(func_node.range(), output_idx, recordRange);
    }
    // Выражение приёмника `$0` доступно ТОЛЬКО в теле метода-члена (`(*this)`).
    const std::string savedSelfExpr = m_ectx.m_output.selfExpr;
    m_ectx.m_output.selfExpr = isMember ? "(*this)" : std::string();
    emitFunctionSignature(func_node, sig, output_idx);
    emitFunctionBody(func_node, sig, output_idx);
    m_ectx.m_output.selfExpr = savedSelfExpr;
    for (std::size_t i = 0; i < sig.namespaces.size(); ++i) {
        m_ectx.m_services.source().output_append(output_idx, cppNamespaceClose(/*inlineForm=*/true));
    }
}

// Лямбда-выражение как ЗНАЧЕНИЕ: `[captures](params)[ -> Ret] { body }`.
// Захваты - только имена по значению (C++-захват копией `[c_x, c_y]`); параметры/тело - как у функции.
// Специализированный путь: НЕ generateFuncDeclToFile (тот эмитит top-level определение и ломает выражение).
void DeclEmitter::emitLambdaExpr(const FuncDecl& func_node) {
    // Список захватов (по значению): trust-имя -> C++-идентификатор (c_x).
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "[");
    bool firstCap = true;
    if (func_node.m_captures) {
        for (const auto& cap : *func_node.m_captures) {
            if (!cap || cap->kind() != ParserToken::Kind::ArgNode) {
                continue;
            }
            if (!firstCap) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            }
            firstCap = false;
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, utils::cppDeclName(requiredText(*cap)));
        }
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, "](");
    // Параметры.
    if (func_node.m_params) {
        for (size_t i = 0; i < func_node.m_params->size(); ++i) {
            if (i > 0) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, ", ");
            }
            const auto* p = static_cast<const ArgNode*>((*func_node.m_params)[i].get());
            if (!p || p->kind() != ParserToken::Kind::ArgNode) {
                // Не-аргумент в списке параметров лямбды — нарушение контракта AST: диагностика
                // (R9 — без молчаливой подстановки), параметр эмитится как Any (тип неизвестен).
                m_ectx.m_services.report(func_node.range(), diag::DiagId::ParseError, "lambda parameter is not a valid argument node");
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_driver.m_type.anyTypeName());
                continue;
            }
            if (isVariadicParamMarker(p)) {
                // Единый с функциями признак вариадик-маркера (не сравнение текста, R9).
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, "...");
                continue;
            }
            // Тип параметра — ЕДИНЫЙ рендер с функциями (аннотация/Void/Any/const/pack). Невыразимый
            // тип: диагностика уже выдана TypeEmitter, параметр эмитится как Any (значение всё равно
            // не скомпилируется без типа, но без молчаливой подмены — диагностика есть).
            std::string ptype;
            if (!resolveParamCppType(*p, ptype)) {
                ptype = m_driver.m_type.anyTypeName();
            }
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ptype);
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, " ");
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, utils::cppDeclName(requiredText(*p)));
            if (p->m_value) {
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, " = ");
                // ТОТ ЖЕ путь, что default-аргумент функции (`renderExprToString`): подавленный
                // mapping и ВЫКЛЮЧЕННЫЕ overflow-проверки (default — не тело функции; lambda-обёртка
                // с захватом в значении по умолчанию недопустима).
                m_ectx.m_services.source().output_append(m_ectx.m_output.out, m_driver.renderExprToString(p->m_value.get()));
            }
        }
    }
    m_ectx.m_services.source().output_append(m_ectx.m_output.out, ")");
    // Тип возврата — ЕДИНЫЙ рендер с функциями (Void/None → `void`).
    if (func_node.m_type) {
        std::string ret;
        if (!resolveReturnCppType(func_node.m_type, ret)) {
            ret.clear(); // диагностика выдана; явная аннотация невыразима
        }
        if (!ret.empty()) {
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, " -> ");
            m_ectx.m_services.source().output_append(m_ectx.m_output.out, ret);
        }
    }
    // Тело.
    auto* fbBody = func_node.body();
    if (fbBody && !m_ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_output.scopeStack.push_back({m_ectx.m_output.indentLevel()});
        m_driver.m_stmt.emitBlockBodyToFile(*fbBody, func_node.blockRange(), m_ectx.m_output.out, /*mapBlock=*/true, {}, {}, nullptr, nullptr);
        m_ectx.m_output.scopeStack.pop_back();
    } else {
        m_ectx.m_services.source().output_append(m_ectx.m_output.out, " {}");
    }
}

} // namespace trust
