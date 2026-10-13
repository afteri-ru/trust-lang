// src/ir/hir/verify.cpp
// Структурный верификатор HIR (см. include/ir/hir_verify.hpp).
#include "ir/hir_verify.hpp"

#include "ast/ast_nodes.hpp"
#include "ast/binary_op.hpp"
#include "ast/ast_nodes.hpp"
#include "ast/trust_prop.hpp"
#include "ir/lowering.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace trust {

namespace {

struct Walk {
    std::optional<std::string> violation;
    std::unordered_set<const AstNodeBase*> visited;
    std::unordered_set<const FuncDecl*> verifiedFunctions;
};

/// Диагностическая привязка синтетического узла к исходной конструкции (E1: `origin`).
[[nodiscard]] std::string originNote(const AstNodeBase& node) {
    if (node.origin == nullptr) {
        return {};
    }
    const std::string_view kind = ParserToken::name(node.origin->kind());
    const MapperRange range = node.origin->range();
    if (range.isInvalid()) {
        return std::format(" (origin {})", kind);
    }
    return std::format(" (origin {} at offset {})", kind, range.begin.offset());
}

/// Имя синтетической временной анализатора/lowering'а: префикс + числовой хвост
/// (`_match1`, `_w0`, `_we0`, `__trust_*_0`, `__cap_0`).
[[nodiscard]] bool isSyntheticTempName(std::string_view name) {
    static constexpr std::string_view kPrefixes[] = {"_match", "_we", "_w", "__trust_last_", "__trust_dst_", "__trust_res_", kLockerTempPrefix};
    for (const std::string_view prefix : kPrefixes) {
        if (!name.starts_with(prefix)) {
            continue;
        }
        const std::string_view suffix = name.substr(prefix.size());
        if (!suffix.empty() && std::all_of(suffix.begin(), suffix.end(), [](char c) { return c >= '0' && c <= '9'; })) {
            return true;
        }
    }
    return false;
}

struct FunctionScan {
    std::unordered_map<std::string, std::size_t> labels;
    std::unordered_map<std::string, std::size_t> gotos;
    std::unordered_map<std::string, const AstNodeBase*> firstGoto;
    std::unordered_map<std::string, const AstNodeBase*> synthetic;
    std::string duplicateTempName;
    const AstNodeBase* duplicateTempNode = nullptr;
    /// Инварианты-контракты тела и привязанные к циклам (для проверки ассоциации).
    std::unordered_set<const AstNodeBase*> invariantContracts;
    std::unordered_set<const AstNodeBase*> attachedInvariants;
};

/// Обойти тело ОДНОЙ функции (вложенные FuncDecl - отдельные функции/CFG).
void scanFunction(AstNodeBase& node, FunctionScan& scan) {
    if (node.is<FuncDecl>()) {
        return;
    }
    if (node.kind() == ParserToken::Kind::LabelStmt) {
        ++scan.labels[std::string(requiredText(node))];
    } else if (node.kind() == ParserToken::Kind::GotoStmt) {
        const std::string name(requiredText(node));
        ++scan.gotos[name];
        scan.firstGoto.try_emplace(name, &node);
    } else if (node.kind() == ParserToken::Kind::TrustContract) {
        const auto& contract = static_cast<const TrustContract&>(node);
        if (contract.kind == PropertyKind::Invariant) {
            scan.invariantContracts.insert(&node);
        }
    }
    if (node.kind() == ParserToken::Kind::WhileStmt) {
        const auto* loop = node.as<WhileStmt>();
        scan.attachedInvariants.insert(loop->m_invariants.begin(), loop->m_invariants.end());
    } else if (node.kind() == ParserToken::Kind::DoWhileStmt) {
        const auto* loop = node.as<DoWhileStmt>();
        scan.attachedInvariants.insert(loop->m_invariants.begin(), loop->m_invariants.end());
    }
    if (node.is<VarDecl>()) {
        const auto& var = *node.as<VarDecl>();
        const std::string name(var.text());
        if (isSyntheticTempName(name)) {
            const auto [it, inserted] = scan.synthetic.emplace(name, &var);
            if (!inserted && scan.duplicateTempName.empty()) {
                scan.duplicateTempName = name;
                scan.duplicateTempNode = &var;
            }
        }
    }
    std::vector<AstNodePtr*> slots;
    node.collectChildren(slots);
    for (AstNodePtr* slot : slots) {
        if (slot != nullptr && *slot != nullptr) {
            scanFunction(**slot, scan);
        }
    }
}

void checkFunction(Walk& walk, FuncDecl& fn) {
    if (!walk.verifiedFunctions.insert(&fn).second) {
        return;
    }
    FunctionScan scan;
    if (fn.m_body) {
        for (AstNodePtr& stmt : *fn.m_body) {
            if (stmt) {
                scanFunction(*stmt, scan);
            }
        }
    }
    if (!scan.duplicateTempName.empty()) {
        walk.violation = std::format("HIR verify: synthetic temporary '{}' is declared twice in function '{}'{}", scan.duplicateTempName, fn.text(),
                                     scan.duplicateTempNode != nullptr ? originNote(*scan.duplicateTempNode) : std::string{});
        return;
    }
    for (const auto& [name, count] : scan.labels) {
        if (count != 1) {
            walk.violation = std::format("HIR verify: label '{}' is defined {} times in function '{}'", name, count, fn.text());
            return;
        }
    }
    for (const auto& [name, count] : scan.gotos) {
        const auto label = scan.labels.find(name);
        const std::size_t defined = (label == scan.labels.end()) ? 0 : label->second;
        if (defined != 1) {
            const AstNodeBase* gotoNode = scan.firstGoto.contains(name) ? scan.firstGoto.at(name) : nullptr;
            walk.violation = std::format("HIR verify: goto '{}' must have exactly one defining label in the function (found {}){}", name, defined,
                                         gotoNode != nullptr ? originNote(*gotoNode) : std::string{});
            return;
        }
    }
    // Ассоциация инвариантов: каждый инвариант-контракт обязан быть привязан ровно к одному
    // циклу, и каждая привязка обязана указывать на инвариант-контракт тела функции.
    for (const AstNodeBase* contract : scan.invariantContracts) {
        if (!scan.attachedInvariants.contains(contract)) {
            walk.violation = std::format("HIR verify: loop invariant is not attached to a loop in function '{}'{}", fn.text(), originNote(*contract));
            return;
        }
    }
    for (const AstNodeBase* contract : scan.attachedInvariants) {
        if (contract == nullptr || !scan.invariantContracts.contains(contract)) {
            walk.violation = std::format("HIR verify: attached loop invariant is not an invariant contract of function '{}'", fn.text());
            return;
        }
    }
}

void checkVarDecl(Walk& walk, const VarDecl& var) {
    // Пользовательская нетипизированная переменная обязана иметь выведенный тип: кодоген в этой
    // ветке не имеет другого источника. Исключения: `_` (тип выводится по истории записей в DA,
    // который идёт ПОСЛЕ гейта) и синтетические временные (`__cap_` эмитится как auto).
    const bool deferredToDa = isNoneMarker(var.m_initializer.get());
    if (var.m_type == nullptr && var.m_initializer != nullptr && !deferredToDa && !isSyntheticTempName(var.text()) && !var.inferredType.isValid()) {
        walk.violation = std::format("HIR verify: inferred type of variable '{}' is not materialized by semantic analysis{}", var.text(), originNote(var));
    }
}

void checkCall(Walk& walk, const CallExpr& call) {
    // Кодоген печатает приведения СТРОГО по `argCasts` в порядке аргументов вызова.
    if (call.argCasts.empty()) {
        return;
    }
    const std::size_t count = call.boundArgs.has_value() ? call.boundArgs->size() : (call.m_args.has_value() ? call.m_args->size() : 0);
    if (call.argCasts.size() != count) {
        walk.violation =
            std::format("HIR verify: call argCasts has {} entries, but the call has {} argument slot(s){}", call.argCasts.size(), count, originNote(call));
    }
}

void checkBinary(Walk& walk, const Binary& binary) {
    // Кодоген обязан прочитать готовую классификацию контролируемой арифметики (codegen EXPECT).
    if (isOverflowCheckableOp(binary.m_op) && !binary.m_overflowCheck.has_value()) {
        walk.violation =
            std::format("HIR verify: overflow-check classification is missing for Binary kind '{}'{}", ParserToken::name(binary.kind()), originNote(binary));
    }
}

void checkNode(Walk& walk, AstNodeBase& node) {
    if (walk.violation || !walk.visited.insert(&node).second) {
        return;
    }
    if (node.kind() == ParserToken::Kind::ErrorExpr) {
        walk.violation = "HIR verify: recovery node ErrorExpr reached the lowered form";
        return;
    }
    if (node.kind() == ParserToken::Kind::TypeName) {
        const auto* type = node.as<IdentType>();
        if (type == nullptr || !type->resolvedTypeId.isValid()) {
            walk.violation = std::format("HIR verify: type annotation '{}' is not materialized by semantic analysis{}", requiredText(*type), originNote(node));
            return;
        }
    }
    if (node.kind() == ParserToken::Kind::BreakStmt || node.kind() == ParserToken::Kind::ContinueStmt) {
        // Именованные переходы обязаны быть развёрнуты lowering'ом (goto/return); выживший
        // label у break/continue - дефект понижения (иначе CFG/кодоген неверно строят переход).
        const auto& jump = static_cast<const JumpStmt&>(node);
        if (jump.m_label != nullptr) {
            walk.violation = std::format("HIR verify: named break/continue must be lowered to goto/return{}", originNote(node));
            return;
        }
    }
    if (node.kind() == ParserToken::Kind::SemicolonStmt) {
        const auto& semi = static_cast<const SemicolonStmt&>(node);
        if (semi.m_expr == nullptr || semi.m_expr->kind() == ParserToken::Kind::SemicolonStmt) {
            walk.violation = std::format("HIR verify: SemicolonStmt must wrap exactly one non-SemicolonStmt expression{}", originNote(node));
            return;
        }
    }
    if (node.kind() == ParserToken::Kind::MemberAccess) {
        // Левый операнд-имя обязан быть разрешён: value-ссылка (`resolvedDecl`) ИЛИ тип
        // (`resolvedTypeId`). Иначе кодоген не может выбрать `(obj).field` / `Type::member`.
        const auto& access = static_cast<const Binary&>(node);
        if (access.m_left != nullptr && access.m_left->kind() == ParserToken::Kind::Ident) {
            const auto* left = access.m_left->as<IdentName>();
            const bool nameLike = left != nullptr && (left->is_simple() || left->is_local()) && !left->is_special();
            if (nameLike && left->resolvedDecl == nullptr && !left->isTypeName()) {
                walk.violation = std::format("HIR verify: member access object '{}' is neither a resolved value nor a type{}", left->text(), originNote(node));
                return;
            }
        }
    }
    if (node.is<VarDecl>()) {
        checkVarDecl(walk, *node.as<VarDecl>());
        if (walk.violation) {
            return;
        }
    }
    if (node.is<CallExpr>()) {
        checkCall(walk, *node.as<CallExpr>());
        if (walk.violation) {
            return;
        }
    }
    if (node.is<Binary>()) {
        checkBinary(walk, *node.as<Binary>());
        if (walk.violation) {
            return;
        }
    }
    if (node.is<FuncDecl>()) {
        checkFunction(walk, *node.as<FuncDecl>());
        if (walk.violation) {
            return;
        }
    }
    std::vector<AstNodePtr*> slots;
    node.collectChildren(slots);
    for (AstNodePtr* slot : slots) {
        if (slot != nullptr && *slot != nullptr) {
            checkNode(walk, **slot);
            if (walk.violation) {
                return;
            }
        }
    }
}

} // namespace

std::optional<std::string> verifyHir(const HirModule& hir) {
    Walk walk;
    for (const AstNodePtr& root : hir.nodes()) {
        if (root) {
            checkNode(walk, *root);
            if (walk.violation) {
                return walk.violation;
            }
        }
    }
    return std::nullopt;
}

} // namespace trust
