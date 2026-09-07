#include "semantic/stack_check_infer.hpp"

#include "semantic/stack_check.hpp"
#include "semantic/symbol_table.hpp"
#include "ast/attr_builtin.hpp"
#include "diag/context.hpp"

#include <unordered_set>

namespace trust {

StackCheckInferHook::StackCheckInferHook(AnalysisContext& actx)
: m_actx(actx) {
}

// Собирает рёбра графа вызовов. onNode вызывается ядром для каждого узла ДО обработки; здесь
// нас интересуют только CallExpr с идентификаторным callee, разрешимым в объявление функции.
bool StackCheckInferHook::onNode(AstNodePtr& node) {
    if (!node || node->kind() != ParserToken::Kind::CallExpr) {
        return false;
    }
    const auto& call = static_cast<const CallExpr&>(*node);
    // Текущая функция (вызывающая) - по скоуп-стекам ядра; вне функции (top-level) вызов не входит.
    const FuncDecl* caller = m_actx.currentFunc();
    if (!caller || !call.m_callee || call.m_callee->kind() != ParserToken::Kind::Ident) {
        return false;
    }
    if (const Symbol* s = m_actx.symbols().resolve(call.m_callee->text())) {
        if (s->decl && s->decl->kind() == ParserToken::Kind::FuncDecl) {
            const auto* callee = static_cast<const FuncDecl*>(s->decl);
            m_calls[caller].insert(callee);
            m_ranges.emplace(callee, callee->range());
            m_ranges.emplace(caller, caller->range());
        }
    }
    return false;
}

// После полного обхода: найти рекурсивные функции и применить политику режима.
void StackCheckInferHook::finalize() {
    const auto mode = semantic::stackCheckModeFromOptions(m_actx.ctx().opts());
    if (mode != semantic::StackCheckMode::kRecursion && mode != semantic::StackCheckMode::kAuto) {
        return;
    }
    const AttrPool& attrs = m_actx.ctx().attrs();
    const auto stackCheckId = attrs.lookup(attr::StackCheck);

    for (const auto& [f, _] : m_calls) {
        (void)_;
        if (!f || !isRecursive(f)) {
            continue;
        }
        const bool guarded = stackCheckId.has_value() && f->has_attr(*stackCheckId);
        if (mode == semantic::StackCheckMode::kAuto && stackCheckId.has_value() && !guarded) {
            // Авто-маркировка рекурсивной функции: транспилятор увидит атрибут при резолве callee
            // и вставит check_stack_limit() перед каждым вызовом.
            const_cast<FuncDecl*>(f)->add_attr(*stackCheckId, /*manual=*/false);
            continue;
        }
        if (mode == semantic::StackCheckMode::kRecursion && !guarded) {
            auto it = m_ranges.find(f);
            const MapperRange r = (it != m_ranges.end()) ? it->second : f->range();
            m_actx.ctx().report(r, semantic::DiagId::StackCheckInfer,
                                "recursive function '{}' is not protected against stack overflow; add @[stack_check@] "
                                "or use --stack-check=auto",
                                std::string(f->text()));
        }
    }
}

bool StackCheckInferHook::isRecursive(const FuncDecl* f) const {
    // Обход в глубину из f; если снова достигаем f - рекурсия.
    std::unordered_set<const FuncDecl*> visited;
    std::vector<const FuncDecl*> stack;
    stack.push_back(f);
    while (!stack.empty()) {
        const FuncDecl* cur = stack.back();
        stack.pop_back();
        auto it = m_calls.find(cur);
        if (it == m_calls.end()) {
            continue;
        }
        for (const FuncDecl* nxt : it->second) {
            if (nxt == f) {
                return true;
            }
            if (visited.insert(nxt).second) {
                stack.push_back(nxt);
            }
        }
    }
    return false;
}

} // namespace trust
