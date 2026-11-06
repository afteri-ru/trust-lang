// Семантический гейт реализаций builtin-типов (до HIR/кодогена).
#include "semantic/verify/provider_check.hpp"

#include "ast/ast_nodes.hpp"
#include "diag/diag.hpp"
#include "session/context.hpp"
#include "target/target.hpp"
#include "types/group.hpp"
#include "types/names.hpp"
#include "types/typekind.hpp"
#include "types/types.hpp"

#include <set>
#include <string>

namespace trust {

namespace {

/// Группы-маркеры: не материализуются как C++-значения, реализации не требуют.
constexpr bool isMarkerGroup(Group group) noexcept {
    switch (group) {
    case Group::kVoid:
    case Group::kAuto:
    case Group::kStructured:
    case Group::kCallable:
    case Group::kClasses:
    case Group::kIterators:
    case Group::kDateTime:
    case Group::kAsync:
    case Group::kSync:
    case Group::kEllipsis:
    case Group::kNative:
    case Group::kArithmetics:
    case Group::kTemplateParam:
        return true;
    default:
        return false;
    }
}

class ProviderChecker {
  public:
    ProviderChecker(Context& ctx, const TypeReadModel& types)
    : m_ctx(ctx)
    , m_types(types) {}

    void walk(const AstNodePtr& node) {
        if (!node) {
            return;
        }
        visit(*node);
        for (const AstNodePtr& child : node->children()) {
            walk(child);
        }
    }

    void visit(const AstNodeBase& node) {
        if (const auto* lit = node.is<Literal>() ? node.as<Literal>() : nullptr) {
            checkState(lit->typeState, node.range());
        }
        if (const auto* b = node.is<Binary>() ? node.as<Binary>() : nullptr) {
            checkState(b->lhsType, node.range());
            checkState(b->rhsType, node.range());
            checkState(b->resultType, node.range());
            checkState(b->commonType, node.range());
            for (const TypeId t : b->argCasts) {
                checkType(t, node.range());
            }
        }
        if (const auto* call = node.is<CallExpr>() ? node.as<CallExpr>() : nullptr) {
            checkState(call->resultState, node.range());
            checkType(call->resultType, node.range());
            checkType(call->resolvedSignature, node.range());
            for (const TypeId t : call->argCasts) {
                checkType(t, node.range());
            }
            for (const TypeId t : call->resolvedTemplateArgs) {
                checkType(t, node.range());
            }
        }
        if (const auto* var = node.is<VarDecl>() ? node.as<VarDecl>() : nullptr) {
            checkState(var->inferredType, node.range());
        }
        if (const auto* destr = node.is<DestructureDecl>() ? node.as<DestructureDecl>() : nullptr) {
            checkState(destr->m_sourceType, node.range());
            for (const DestructureDecl::Target& target : destr->m_targets) {
                checkType(target.anyCastType, node.range());
            }
            for (const DestructureDecl::Target& target : destr->m_targets) {
                checkType(target.declaredType, node.range());
            }
        }
        if (const auto* take = node.is<RefTakeExpr>() ? node.as<RefTakeExpr>() : nullptr) {
            checkState(take->m_resultType, node.range());
        }
        if (const auto* make = node.is<RefMakeExpr>() ? node.as<RefMakeExpr>() : nullptr) {
            checkState(make->m_resultType, node.range());
        }
        if (const auto* dl = node.is<DictLiteralNode>() ? node.as<DictLiteralNode>() : nullptr) {
            checkState(dl->arrayType, node.range());
            checkState(dl->tensorType, node.range());
            checkState(dl->castSourceType, node.range());
            checkState(dl->resultState, node.range());
        }
        if (const auto* range = node.is<RangeExpr>() ? node.as<RangeExpr>() : nullptr) {
            checkState(range->resultState, node.range());
            checkType(range->elementType.getTypeId(), node.range());
        }
        if (const auto* name = node.is<IdentName>() ? node.as<IdentName>() : nullptr) {
            checkType(name->resolvedTypeId, node.range());
            checkState(name->valueType, node.range());
        }
        if (const auto* arg = node.is<ArgNode>() ? node.as<ArgNode>() : nullptr) {
            checkState(arg->resultType, node.range());
        }
        if (node.is<TryCatchStmt>()) {
            // Вид перехвата хранится как TryKind (НЕ TypeId), поэтому codegen-injected классы
            // прерываний (IntAny/IntPlus/IntMinus) гейту по типу не видны: требуется явная
            // проверка конструкции (реализация всех трёх — в `.src` модуля exceptions).
            requireBuiltin(m_types.getType(type::IntAny), node.range());
        }
    }

  private:
    void checkState(QualType state, MapperRange range) {
        if (state.isValid()) {
            checkType(state.getTypeId(), range);
        }
    }

    void checkType(TypeId id, MapperRange range) {
        if (id == INVALID_TYPE_ID) {
            return;
        }
        const TypeId canonical = m_types.store().desugaredTypeId(id);
        if (canonical == INVALID_TYPE_ID || !m_visitedTypes.insert(canonical).second) {
            return; // циклы ссылочных типов (self-referencing Node) не обходим повторно
        }
        // Ссылочные обёртки (unique/shared/weak/locker) выбирает КОДОГЕН по виду ссылки
        // (RefType в TypeKind), а не по имени: их C++-типы (StaticUnique/AccessShared/…) реализует
        // модуль stdlib `mem` (`\mem();`). Вид ссылки читается прямо из TypeId через биты RefType.
        switch (typeRefOf(canonical)) {
        case RefType::kUnique:
            requireBuiltin(m_types.getType(type::StaticUnique), range);
            break;
        case RefType::kShared:
        case RefType::kWeak:
        case RefType::kLocker:
            requireBuiltin(m_types.getType(type::AccessShared), range);
            break;
        default:
            break;
        }
        // Структурные контейнеры не имеют собственной реализации: требуют реализацию
        // абстрактного builtin-типа (единый маппинг — BuiltinRegistry::abstractBuiltinOf).
        if (const TypeId abstractId = m_types.builtins().abstractBuiltinOf(canonical); abstractId != INVALID_TYPE_ID) {
            requireBuiltin(abstractId, range);
        } else if (m_types.store().isBuiltinType(canonical)) {
            requireBuiltin(canonical, range);
        }
        // Составные типы: рекурсия по типовым полям (цикл-защита — m_visitedTypes).
        if (const auto data = m_types.store().getTypeData(canonical); data.has_value()) {
            forEachTypeChild(*data, ChildChecker{*this, range});
        }
    }

    void requireBuiltin(TypeId id, MapperRange range) {
        if (id == INVALID_TYPE_ID) {
            return;
        }
        const TypeId canonical = m_types.store().desugaredTypeId(id);
        // Ссылочные обёртки (ptr/ref/unique/shared/…) реализацию не несут: для unique/shared
        // обёртку проверяет ветка RefType в checkType, а pointee-значение не материализуется.
        if (typeRefOf(canonical) != RefType::kValue) {
            return;
        }
        // Обобщённые группы-алиасы (Integers/Numbers/Strings/Tensors) — только статические
        // сравнения типов, реализации не требуют.
        if (canonical == m_types.getType(type_generic::Integers) || canonical == m_types.getType(type_generic::Numbers) ||
            canonical == m_types.getType(type_generic::Strings) || canonical == m_types.getType(type_generic::Tensors)) {
            return;
        }
        // Тип с загруженной реализацией (`=` из `.src`) или собственной C++-репрезентацией
        // (интринсик: Bool/Int*/UInt*/Float32/64, CString) покрыт.
        const TypeId factsKey = m_types.store().cppFactsKeyOf(canonical);
        if (m_ctx.target().typeCppFacts().hasUser(factsKey)) {
            return;
        }
        if (!m_ctx.target().typeCppFacts().cppNameOf(factsKey).empty()) {
            return;
        }
        // Метки групп/sentinel'ы (Void/None/Auto/категории) как значения не материализуются.
        if (isMarkerGroup(typeGroupOf(canonical))) {
            return;
        }
        if (!m_checkedBuiltins.insert(canonical).second) {
            return;
        }
        m_ctx.diag().report(Severity::Error, range, "implementation of built-in type '{}' is not defined", m_types.printer().typeNameOf(canonical));
    }

    struct ChildChecker {
        ProviderChecker& checker;
        MapperRange range;

        void operator()(TypeId child) const { checker.checkType(child, range); }
    };

    Context& m_ctx;
    const TypeReadModel& m_types;
    std::set<TypeId> m_checkedBuiltins;
    std::set<TypeId> m_visitedTypes;
};

} // namespace

bool verifyBuiltinProviders(Context& ctx, const TypeReadModel& types, const std::vector<AstNodePtr>& roots) {
    const std::size_t errorsBefore = ctx.diag().errorCount();
    ProviderChecker checker(ctx, types);
    for (const AstNodePtr& root : roots) {
        checker.walk(root);
    }
    return ctx.diag().errorCount() == errorsBefore;
}

bool verifySemanticProviders(Context& ctx, const std::vector<AstNodePtr>& roots) {
    return verifyBuiltinProviders(ctx, ctx.types(), roots);
}

} // namespace trust