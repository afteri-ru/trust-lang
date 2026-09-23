// src/semantic/ref_cycle.cpp
// Реализация анализатора рекурсивных/циклических ссылок в полях классов (см. ref_cycle.hpp).
#include "semantic/ref_cycle.hpp"
#include "semantic/diag.hpp"
#include "session/context.hpp"
#include "diag/diag.hpp"
#include "types/group.hpp"

#include <algorithm>
#include <functional>
#include <set>

namespace trust {

namespace {
// Компоненты сильной связности (Tarjan) над ориентированным графом по индексам. Возвращает
// список компонент; вершины одной компоненты взаимно достижимы (каждая лежит на цикле).
std::vector<std::vector<int>> tarjanScc(int n, const std::vector<std::vector<int>>& adj) {
    std::vector<int> num(static_cast<size_t>(n), -1);
    std::vector<int> low(static_cast<size_t>(n), 0);
    std::vector<bool> onStk(static_cast<size_t>(n), false);
    std::vector<int> stk;
    std::vector<std::vector<int>> sccs;
    int counter = 0;
    std::function<void(int)> strongconnect = [&](int v) {
        num[static_cast<size_t>(v)] = low[static_cast<size_t>(v)] = counter++;
        stk.push_back(v);
        onStk[static_cast<size_t>(v)] = true;
        for (const int w : adj[static_cast<size_t>(v)]) {
            if (num[static_cast<size_t>(w)] == -1) {
                strongconnect(w);
                low[static_cast<size_t>(v)] = std::min(low[static_cast<size_t>(v)], low[static_cast<size_t>(w)]);
            } else if (onStk[static_cast<size_t>(w)]) {
                low[static_cast<size_t>(v)] = std::min(low[static_cast<size_t>(v)], num[static_cast<size_t>(w)]);
            }
        }
        if (low[static_cast<size_t>(v)] == num[static_cast<size_t>(v)]) {
            std::vector<int> comp;
            int w = 0;
            do {
                w = stk.back();
                stk.pop_back();
                onStk[static_cast<size_t>(w)] = false;
                comp.push_back(w);
            } while (w != v);
            sccs.push_back(std::move(comp));
        }
    };
    for (int i = 0; i < n; ++i) {
        if (num[static_cast<size_t>(i)] == -1) {
            strongconnect(i);
        }
    }
    return sccs;
}
} // namespace

RefCycleHook::RefCycleHook(AnalysisContext& actx)
: m_actx(actx) {
}

void RefCycleHook::finalize() {
    TypeRegistry& reg = m_actx.ctx().types();
    // Корни - все именованные пользовательские record-типы (обычные и абстрактные шаблоны).
    std::vector<TypeId> roots;
    reg.forEachType([&](std::string_view name, bool user) {
        if (!user) {
            return;
        }
        if (auto id = reg.findType(name); id.has_value() && reg.isRecordType(*id)) {
            roots.push_back(*id);
        }
    });
    // Все достижимые узлы графа (устойчиво к циклам).
    std::vector<TypeId> nodes;
    {
        std::set<TypeId> seen;
        std::vector<TypeId> work(roots.begin(), roots.end());
        while (!work.empty()) {
            const TypeId id = work.back();
            work.pop_back();
            if (!seen.insert(id).second) {
                continue;
            }
            nodes.push_back(id);
            for (const Edge& e : edgesOf(id)) {
                work.push_back(e.target);
            }
        }
    }
    const int n = static_cast<int>(nodes.size());
    std::map<TypeId, int> indexOf;
    for (int i = 0; i < n; ++i) {
        indexOf.emplace(nodes[static_cast<size_t>(i)], i);
    }
    // Рёбра по индексам, раздельно по видам (для анализа трёх подмножеств).
    std::vector<std::vector<int>> adjAll(static_cast<size_t>(n));
    std::vector<std::vector<int>> adjInline(static_cast<size_t>(n));
    std::vector<std::vector<int>> adjUnique(static_cast<size_t>(n));
    std::vector<std::pair<int, int>> sharedEdges;
    for (int i = 0; i < n; ++i) {
        for (const Edge& e : edgesOf(nodes[static_cast<size_t>(i)])) {
            const int w = indexOf.at(e.target);
            adjAll[static_cast<size_t>(i)].push_back(w);
            if (e.kind == EdgeKind::Inline) {
                adjInline[static_cast<size_t>(i)].push_back(w);
            } else if (e.kind == EdgeKind::UniquePtr) {
                adjUnique[static_cast<size_t>(i)].push_back(w);
            } else if (e.kind == EdgeKind::Shared) {
                sharedEdges.emplace_back(i, w);
            }
        }
    }
    const auto reportScc = [&](const std::vector<int>& comp, semantic::DiagId diag, const char* msg) {
        std::vector<TypeId> ids;
        ids.reserve(comp.size());
        for (const int v : comp) {
            ids.push_back(nodes[static_cast<size_t>(v)]);
        }
        std::sort(ids.begin(), ids.end());
        std::string list;
        MapperRange range{};
        for (const TypeId t : ids) {
            if (!list.empty()) {
                list += ", ";
            }
            list += typeName(t);
            if (range.isInvalid()) {
                range = reg.getTypeSourceRange(t);
            }
        }
        m_actx.ctx().report(range, diag, "{}: {}", msg, list);
    };
    // Полнота: цикл лежит в SCC своего ПОДМНОЖЕСТВА рёбер, поэтому три анализа независимы.
    // (1) shared: shared-ребро внутри SCC полного графа лежит на цикле -> refcount-утечка.
    {
        const auto sccs = tarjanScc(n, adjAll);
        std::vector<int> compOf(static_cast<size_t>(n), -1);
        for (int c = 0; c < static_cast<int>(sccs.size()); ++c) {
            for (const int v : sccs[static_cast<size_t>(c)]) {
                compOf[static_cast<size_t>(v)] = c;
            }
        }
        std::set<int> bad;
        for (const auto& [u, w] : sharedEdges) {
            if (compOf[static_cast<size_t>(u)] == compOf[static_cast<size_t>(w)]) {
                bad.insert(compOf[static_cast<size_t>(u)]);
            }
        }
        for (const int c : bad) {
            reportScc(sccs[static_cast<size_t>(c)], semantic::DiagId::RecursiveShared,
                      "recursive strong (shared) reference cycle in class fields (ownership loop; break it with 'weak')");
        }
    }
    // (2) value: цикл ТОЛЬКО из inline-рёбер (вложение по значению) -> бесконечный размер.
    {
        const auto sccs = tarjanScc(n, adjInline);
        for (const auto& comp : sccs) {
            const int v = comp.front();
            const auto& outs = adjInline[static_cast<size_t>(v)];
            const bool selfLoop = comp.size() == 1 && std::find(outs.begin(), outs.end(), v) != outs.end();
            if (comp.size() >= 2 || selfLoop) {
                reportScc(comp, semantic::DiagId::RecursiveValue, "recursive value containment in class fields (the type would have infinite size)");
            }
        }
    }
    // (3) unique: цикл ТОЛЬКО из unique-указательных рёбер с >=2 типами = взаимное монопольное
    //     владение (неконструируемо). Self-loop unique-указателя - легальный owned-список (size 1).
    {
        const auto sccs = tarjanScc(n, adjUnique);
        for (const auto& comp : sccs) {
            if (comp.size() >= 2) {
                reportScc(comp, semantic::DiagId::RecursiveUnique,
                          "mutually exclusive ('unique') ownership cycle in class fields (exclusive ownership cannot be mutual)");
            }
        }
    }
}

const std::vector<RefCycleHook::Edge>& RefCycleHook::edgesOf(TypeId rec) {
    if (auto it = m_adj.find(rec); it != m_adj.end()) {
        return it->second;
    }
    TypeRegistry& reg = m_actx.ctx().types();
    std::vector<Edge> out;
    if (const RecordTypeData* rd = reg.recordData(rec); rd != nullptr) {
        for (const auto& f : rd->fields) {
            collectHolds(f.type, EdgeKind::Inline, out);
        }
    }
    for (const TypeId b : reg.baseClasses(rec)) {
        collectHolds(b, EdgeKind::Inline, out);
    }
    // Self-инстанциация абстрактного шаблона: поле `shared<Box<T>>` внутри `Box` ссылается на
    // инстанциацию с templateOf == rec. Это ребро НА САМ шаблон (поля инстанциации на момент
    // анализа тела ещё не заполнены, и её собственные рёбра недостоверны).
    for (auto& e : out) {
        if (const RecordTypeData* td = reg.recordData(e.target); td != nullptr && td->templateOf == rec) {
            e.target = rec;
        }
    }
    return m_adj.emplace(rec, std::move(out)).first->second;
}

void RefCycleHook::collectHolds(TypeId type, EdgeKind kind, std::vector<Edge>& out) const {
    if (type == INVALID_TYPE_ID) {
        return;
    }
    TypeRegistry& reg = m_actx.ctx().types();
    const TypeId t = structuralType(type);
    const RefType rt = getRefType(getKindFromId(t));
    if (rt != RefType::kValue) {
        EdgeKind nk = kind;
        switch (rt) {
        case RefType::kShared:
            nk = EdgeKind::Shared;
            break;
        case RefType::kUnique: {
            // Указательная форма (unique<T,D>, есть deleter) - owning-указатель; иначе
            // `trust::StaticUnique<T>` хранит значение inline (вложение по значению).
            const auto* rd = reg.getTypeDataAs<RefTypeData>(t);
            if (rd != nullptr && rd->deleterType != INVALID_TYPE_ID && kind != EdgeKind::Shared) {
                nk = EdgeKind::UniquePtr;
            }
            break;
        }
        case RefType::kWeak:
        case RefType::kPtr:
        case RefType::kRef:
        case RefType::kRref:
        case RefType::kPtrPtr:
        case RefType::kLocker:
        case RefType::kMptr:
            return; // невладеющее/наблюдающее - владельческого/размерного цикла не создаёт
        default:
            break;
        }
        collectHolds(reg.getPointeeType(t), nk, out);
        return;
    }
    // value-вложение.
    if (reg.isRecordType(t)) {
        out.push_back(Edge{t, kind});
        return;
    }
    if (reg.isArrayType(t)) {
        // `:Array` (mutable) → std::vector: элементы в heap, inline-размер НЕ растёт → HeapInline.
        // `:Array^` (kConstFlag) → std::array: элементы inline (участвуют в размере).
        const bool inlineElems = testFlag(type, SymbolFlag::Const);
        const EdgeKind ek = inlineElems ? kind : (kind == EdgeKind::Inline ? EdgeKind::HeapInline : kind);
        collectHolds(reg.arrayElementType(t), ek, out);
        return;
    }
    if (reg.isRangeType(t)) {
        // `:Range` - невладеющий view (не содержит элементы) → владельческого/размерного цикла нет.
        return;
    }
    if (reg.isTypeDataKind(t, TypeDataKind::kTuple)) {
        if (const auto* td = reg.getTypeDataAs<TupleTypeData>(t); td != nullptr) {
            for (const auto& el : td->elements) {
                collectHolds(el.type, kind, out);
            }
        }
        return;
    }
    if (reg.isNativeTemplateType(t)) {
        // Нативный контейнер по значению (`std::pair<Node, ...>`): типовые аргументы - вложены.
        for (const TypeId a : reg.nativeTemplateArgs(t)) {
            collectHolds(a, kind, out);
        }
        return;
    }
    // Function/Dict/Any/... - record-тип владельчески не вложен.
}

std::string RefCycleHook::typeName(TypeId id) const {
    return m_actx.ctx().types().getFullTypeName(id);
}

} // namespace trust
