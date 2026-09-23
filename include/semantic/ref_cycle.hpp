#pragma once

// include/semantic/ref_cycle.hpp
// Статический анализатор рекурсивных/циклических ссылок в определениях полей классов -
// гарантия модели памяти (types/REFType.md §11.11: сильные циклические ссылки запрещены).
//
// Строит граф "record-тип держит record-тип" по полям (и базовым классам), где ребро несёт вид
// владения/вложения (inline value, unique-указатель, shared) и сообщает циклы:
//   * есть owning 'shared'-ребро            -> -Wrecursive-shared (refcount-утечка; разрыв - weak);
//   * все рёбра 'unique'-указательные (>=2 типов) -> -Wrecursive-unique (взаимное эксклюзивное
//     владение неконструируемо: каждый обязан быть единственным владельцем другого);
//   * все рёбра inline (value / StaticUnique) -> -Wrecursive-value (бесконечный размер типа).
// Смешанный цикл unique-указатель + value конструктивен (указатель рвёт размер) и НЕ диагностируется.
// Self-ссылка unique-указателем (`Node` владеет другим `Node`) - легальный owned-список, не цикл.
//
// Реализация - finalize-постпроход по реестру типов (все типы уже зарегистрированы, включая
// forward-доопределённые и шаблон-инстанциации). Диагностики - severity-опции (default Error).

#include "semantic/inline_hook.hpp"
#include "semantic/pass.hpp"
#include "ast/ast_nodes.hpp"
#include "types/registry.hpp"
#include "types/type_id.hpp"

#include <map>
#include <string>
#include <vector>

namespace trust {

class RefCycleHook : public InlineAnalysisHook {
  public:
    explicit RefCycleHook(AnalysisContext& actx);
    void finalize() override;

  private:
    /// Вид ребра владения/вложения между record-типами.
    enum class EdgeKind {
        Inline,     ///< вложение по значению (value-поле, `:Array^`/std::array, tuple, база, StaticUnique)
        HeapInline, ///< вложение через heap-контейнер (`:Array`=std::vector): элементы не inline (конечный размер)
        UniquePtr,  ///< эксклюзивное владение через указатель (unique<T,D>)
        Shared,     ///< совместное владение (shared), включая sync-backend
    };

    struct Edge {
        TypeId target;
        EdgeKind kind;
    };

    AnalysisContext& m_actx;
    /// Кэш рёбер по исходному record-типу (строится лениво).
    std::map<TypeId, std::vector<Edge>> m_adj;

    /// Рёбра исходящие из record-типа (кэш).
    const std::vector<Edge>& edgesOf(TypeId rec);
    /// Собирает рёбра из типа поля, рекурсивно спускаясь по структурным обёрткам
    /// (ref-вид, Array/Range/Tuple/native-template). kind - накопленный вид ребра.
    void collectHolds(TypeId type, EdgeKind kind, std::vector<Edge>& out) const;
    /// Плоское имя типа для сообщения.
    std::string typeName(TypeId id) const;
};

} // namespace trust
