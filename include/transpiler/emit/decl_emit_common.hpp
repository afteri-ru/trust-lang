#pragma once

// Помощники эмиссии объявлений, общие для decl_emit/type_decl_emit/module_emit.

#include "transpiler/emit/emit_ctx.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/declaration_ids.hpp"
#include "ast/token_base.hpp"
#include "location/decl_id.hpp"
#include "attrs/attr_builtin.hpp"
#include "diag/base_diags.hpp"
#include "utils/strings.hpp"
#include <optional>
#include <set>
#include <vector>

namespace trust {

/// Ссылается ли тело на сущность МОДУЛЬНОГО уровня, не опубликованную заголовком.
inline bool bodyUsesNonPublishedEntity(const FuncDecl& func, const std::set<DeclId>& published, ModuleIndex module) {
    if (!func.hasBody()) {
        return false;
    }
    std::vector<const AstNodeBase*> stack;
    for (const AstNodePtr& node : *func.body()) {
        if (node) {
            stack.push_back(node.get());
        }
    }
    while (!stack.empty()) {
        const AstNodeBase* node = stack.back();
        stack.pop_back();
        if (node->is<IdentName>()) {
            const AstNodeBase* resolved = node->as<IdentName>()->resolvedDecl;
            if (resolved != nullptr &&
                (resolved->isFilePrivate() || (module.isValid() && resolved->module == module && published.count(declarationIdOf(*resolved)) == 0))) {
                return true;
            }
        }
        for (const AstNodePtr& child : node->children()) {
            if (child) {
                stack.push_back(child.get());
            }
        }
    }
    return false;
}

/// Тело обязано быть в заголовке модуля, но ссылается на модульную сущность, которую заголовок
/// НЕ публикует: ошибка отчётена (вызывающий НЕ эмитит тело). Вызывать ТОЛЬКО под guard'ом
/// «тело обязано быть в заголовке» (`bodyMustBeInModuleHeader`). `owner` — trust-имя record-типа
/// для метода (`Owner::member`); пусто — свободная функция.
inline bool headerBodyUnpublishable(CppEmitContext& ectx, const FuncDecl& func, std::string_view owner = {}) {
    if (!bodyUsesNonPublishedEntity(func, ectx.m_declFacts.headerPublishedDecls, ectx.m_output.currentModule)) {
        return false;
    }
    const std::string qualified = owner.empty() ? std::string(func.text()) : utils::qualifyTrustName(owner, func.text());
    ectx.m_services.report(func.range(), diag::DiagId::ParseError,
                           "{} '{}' references a module-level entity that the module header does not publish "
                           "(e.g. a '__::' helper): a template or constexpr {} must be defined in the header; "
                           "move the helper out of '__::' or drop the template",
                           owner.empty() ? "function" : "member", qualified, owner.empty() ? "function" : "member");
    return true;
}

/// Разбор типовых параметров объявления в форму C++-шаблона. ЕДИНЫЙ источник для generic-функций,
/// record-классов и обёрток нативных классов: `template <typename T, typename... U>`.
/// Имена НЕ манглируются кодеком C++-имени: ровно этот текст TypeEmitter видит у типового
/// параметра (`:T` рендерится из реестра, где хранится trust-текст параметра), поэтому
/// манглинг здесь разошёлся бы с использованием.
struct CppTemplateParams {
    std::string header;             ///< «template <typename T, ...>»; пусто — объявление не шаблон
    std::vector<std::string> names; ///< имена параметров в порядке объявления
    std::vector<bool> packs;        ///< true — параметр объявлен как type-pack (`T...`)
};

inline CppTemplateParams cppTemplateParams(const std::vector<AstNodePtr>& params) {
    CppTemplateParams out;
    for (const auto& p : params) {
        if (!p) {
            continue;
        }
        const auto* arg = p->is<ArgNode>() ? p->as<ArgNode>() : nullptr;
        const bool pack = arg != nullptr && arg->isTypePack;
        if (!out.names.empty()) {
            out.header += ", ";
        }
        out.header += pack ? "typename... " : "typename ";
        out.header += std::string(requiredText(*p));
        out.names.emplace_back(requiredText(*p));
        out.packs.push_back(pack);
    }
    if (!out.names.empty()) {
        out.header = "template <" + out.header + ">";
    }
    return out;
}

/// Типовые параметры ОБЪЯВЛЕНИЯ (`m_templateParams` record/класса/функции). ЕДИНЫЙ критерий
/// «объявление — шаблон» — непустой `names`: nullopt, пустой список и неименованный узел шаблоном
/// не являются (тот же разбор, что у `std::vector`-перегрузки).
inline CppTemplateParams cppTemplateParams(const std::optional<std::vector<AstNodePtr>>& params) {
    static const std::vector<AstNodePtr> kNoParams;
    return cppTemplateParams(params.has_value() ? *params : kNoParams);
}

/// Тело метода может жить в заголовке модуля только если его НЕЛЬЗЯ вынести out-of-line:
/// член шаблонного record либо шаблонный метод и `@[constexpr]`. «Шаблонность» метода —
/// тот же критерий, что у `cppTemplateParams` (непустые имена), а не «непустой список узлов».
inline bool methodBodyMustBeInHeader(const FuncDecl& func, bool recordIsTemplate, const AttrPool& attrs) {
    return recordIsTemplate || !cppTemplateParams(func.m_templateParams).names.empty() || func.has_attr(attrs, attr::FuncConstexpr);
}

/// Разбор значения члена Enum/Variant из элемента словаря. `outValue` — узел-значение либо
/// nullptr (безнарный член `x` / значение отсутствует). Возвращает false, если значение
/// нелитерально (массив/словарь/диапазон): диагностика уже выдана (`containerKind` —
/// "enum"/"variant" для текста сообщения), вызывающий НЕ эмитит. ЕДИНАЯ точка для обоих типов.
[[nodiscard]] inline bool resolveEnumVariantMemberValue(CppEmitContext& ectx, const DictElement& element, std::string_view containerKind,
                                                        std::string_view containerTrust, const AstNodeBase*& outValue) {
    outValue = element.value;
    if (element.name.empty() && outValue != nullptr && outValue->kind() == ParserToken::Kind::Ident) {
        outValue = nullptr; // безнарный член - имя в значении, значения нет
    }
    if (outValue != nullptr && !is_literal_kind(outValue->kind())) {
        ectx.m_services.report(element.range, diag::DiagId::ParseError, "значение члена {} '{}' (массив/словарь/диапазон) ещё не реализовано", containerKind,
                               containerTrust);
        return false;
    }
    return true;
}

/// Временное снятие forward-режима заголовка на время эмиссии ОДНОГО метода.
class BodyEmitInHeaderGuard {
  public:
    explicit BodyEmitInHeaderGuard(CppEmitContext& ectx)
    : m_ectx(ectx)
    , m_saved(ectx.m_mode.forwardDeclOnly) {
        m_ectx.m_mode.forwardDeclOnly = false;
    }
    ~BodyEmitInHeaderGuard() { m_ectx.m_mode.forwardDeclOnly = m_saved; }
    BodyEmitInHeaderGuard(const BodyEmitInHeaderGuard&) = delete;
    BodyEmitInHeaderGuard& operator=(const BodyEmitInHeaderGuard&) = delete;

  private:
    CppEmitContext& m_ectx;
    bool m_saved;
};

} // namespace trust