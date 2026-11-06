#pragma once
// Исполнитель класс-таблицы слотов (`ast/slots/node_slots_x.hpp`): классификация слота, visitor-интерфейс
// и генерация метода `forEachSlot` класса. Данные — таблица; здесь ТОЛЬКО механика.
// Три потребителя (по одному visitor на задачу):
//   - обход        -> собирает `Traversed`-слоты (children()/collectChildren);
//   - клон         -> копирует ВСЕ владеемые слоты (`Traversed` + `Internal`);
//   - ремап ссылок -> переписывает невладеющие ссылки (`NonOwn*`).
// Порядок вызовов visitor'а = порядок строк таблицы (контракт).

#include "ast/token.hpp"

#include <bitset>
#include <cstddef>
#include <initializer_list>

namespace trust {

class AstNodeBase;
class FuncDecl;

/// Участие владеемого слота в generic-обходе (`children()`/семантика).
enum class SlotWalk {
    Traversed, ///< виден обходу
    Internal,  ///< скрыт от обхода; нужен только клону
};

/// Фильтр слота, зависящий от KIND узла (политика задаётся ДАННЫМИ строки таблицы).
struct SlotFilter {
    std::bitset<kParserKindSlotCount> skipKinds{};

    [[nodiscard]] bool skipsKind(ParserToken::Kind kind) const noexcept { return skipKinds.test(kindIndex(kind)); }
};

[[nodiscard]] constexpr SlotFilter skipKinds(std::initializer_list<ParserToken::Kind> kinds) noexcept {
    SlotFilter filter;
    for (const ParserToken::Kind kind : kinds) {
        filter.skipKinds.set(kindIndex(kind));
    }
    return filter;
}

/// Приёмник слотов узла. Реализации — у потребителей (обход/клон/ремап);
/// `nonOwningFunc` отделён, т.к. `JumpStmt::m_funcDecl` типизирован (`const FuncDecl*`).
class AstSlotVisitor {
  public:
    AstSlotVisitor() = default;
    virtual ~AstSlotVisitor() = default;
    AstSlotVisitor(const AstSlotVisitor&) = delete;
    AstSlotVisitor& operator=(const AstSlotVisitor&) = delete;

    virtual void owned(AstNodePtr& slot, SlotWalk walk) = 0;
    virtual void nonOwning(const AstNodeBase*& slot) = 0;
    virtual void nonOwningFunc(const FuncDecl*& slot) = 0;
};


#define AST_SLOT_FILTER_None \
    SlotFilter {             \
    }
#define AST_SLOT_FILTER_SkipKinds(...) skipKinds({__VA_ARGS__})
#define AST_WALK_Traversed SlotWalk::Traversed
#define AST_WALK_Internal SlotWalk::Internal

/// Фильтр по kind узла: слот не визитируется у перечисленных kinds (политика = данные).
/// `kind()` вызывается через базовый `AstNodeBase`, т.к. у части узлов есть СВОЁ поле `kind`
/// (TrustContract/TrustElem), которое затеняет метод. Фильтр `constexpr`: у строк `None`
/// проверка сворачивается компилятором.
#define AST_SLOT_IF_KIND(filter)                                                                                      \
    if (constexpr SlotFilter slot_kind_filter_ = AST_SLOT_FILTER_##filter;                                            \
        !slot_kind_filter_.skipsKind(static_cast<const AstNodeBase*>(this)->kind()))

#define AST_ROW_Ptr(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {           \
        if (field) {                     \
            v.owned(field, walk);        \
        }                                \
    }

#define AST_ROW_List(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {            \
        for (AstNodePtr& slot_ : field) { \
            v.owned(slot_, walk);         \
        }                                 \
    }

#define AST_ROW_OptList(field, walk, filter)   \
    AST_SLOT_IF_KIND(filter) {                 \
        if (field) {                           \
            for (AstNodePtr& slot_ : *field) { \
                v.owned(slot_, walk);          \
            }                                  \
        }                                      \
    }

/// `std::optional<std::vector<TemplateArg>>`: владеемый узел `record_.node` внутри struct-элемента
/// (признак `isPack` — данные аргумента, ребёнком не является).
#define AST_ROW_OptRecordList(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {                     \
        if (field) {                               \
            for (auto& record_ : *field) {         \
                if (record_.node) {                \
                    v.owned(record_.node, walk);   \
                }                                  \
            }                                      \
        }                                          \
    }

#define AST_ROW_PairList(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {                \
        for (auto& pair_ : field) {           \
            if (pair_.first) {                \
                v.owned(pair_.first, walk);   \
            }                                 \
            if (pair_.second) {               \
                v.owned(pair_.second, walk);  \
            }                                 \
        }                                     \
    }

#define AST_ROW_CaseList(field, walk, filter)          \
    AST_SLOT_IF_KIND(filter) {                         \
        for (auto& case_ : field) {                    \
            for (AstNodePtr& slot_ : case_.patterns) { \
                if (slot_) {                           \
                    v.owned(slot_, walk);              \
                }                                      \
            }                                          \
            if (case_.body) {                          \
                v.owned(case_.body, walk);             \
            }                                          \
        }                                              \
    }

/// `std::vector<DestructureDecl::Target>`: владеемые узлы внутри struct-элемента (name/typeNode).
#define AST_ROW_TargetList(field, walk, filter)    \
    AST_SLOT_IF_KIND(filter) {                     \
        for (auto& target_ : field) {              \
            if (target_.name) {                    \
                v.owned(target_.name, walk);       \
            }                                      \
            if (target_.typeNode) {                \
                v.owned(target_.typeNode, walk);   \
            }                                      \
        }                                          \
    }

/// `std::vector<TypeBase>`: владеемый узел `type` внутри struct-элемента (вариант наследования — данные).
#define AST_ROW_BaseList(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {                \
        for (auto& base_ : field) {           \
            if (base_.type) {                 \
                v.owned(base_.type, walk);    \
            }                                 \
        }                                     \
    }

/// `other` — другое поле этого узла: элемент, совпадающий с ним по УЗЛУ, не визитируется
/// (ArrayAccess: `m_right` дублирует первый индекс `m_indices`).
#define AST_ROW_ListDeduped(field, walk, other)        \
    AST_SLOT_IF_KIND(None) {                           \
        for (AstNodePtr& slot_ : field) {              \
            if (slot_ && slot_.get() != other.get()) { \
                v.owned(slot_, walk);                  \
            }                                          \
        }                                              \
    }

#define AST_ROW_NonOwnPtr(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {                 \
        v.nonOwning(field);                    \
    }

#define AST_ROW_NonOwnFuncPtr(field, walk, filter) \
    AST_SLOT_IF_KIND(filter) {                     \
        v.nonOwningFunc(field);                    \
    }

#define AST_ROW_NonOwnList(field, walk, filter)   \
    AST_SLOT_IF_KIND(filter) {                    \
        for (const AstNodeBase*& slot_ : field) { \
            v.nonOwning(slot_);                   \
        }                                         \
    }

#define AST_ROW_NonOwnOptList(field, walk, filter)     \
    AST_SLOT_IF_KIND(filter) {                         \
        if (field) {                                   \
            for (const AstNodeBase*& slot_ : *field) { \
                v.nonOwning(slot_);                    \
            }                                          \
        }                                              \
    }

/// Тело из `Definition m_definition` (`field.body()`, optional-вектор): обходим только когда
/// определение — тело; переадресация (строка) детьми не является. Совместимо с клоном.
#define AST_ROW_DefBody(field, walk, filter)                \
    AST_SLOT_IF_KIND(filter) {                              \
        if (auto* slot_body_ = field.body()) {              \
            for (AstNodePtr& slot_ : *slot_body_) {         \
                v.owned(slot_, walk);                       \
            }                                               \
        }                                                   \
    }

#define AST_SLOT_ROW(shape, field, walk, filter) AST_ROW_##shape(field, AST_WALK_##walk, filter)

/// Объявление `forEachSlot` класса по его строкам таблицы: сначала слоты БАЗЫ (порядок
/// обхода задаётся базой), затем собственные строки. `m_trust` эмитит `AstNodeBase`.
#define AST_SLOT_METHOD(BASE, ROWS)                \
    void forEachSlot(AstSlotVisitor& v) override { \
        BASE::forEachSlot(v);                      \
        ROWS(AST_SLOT_ROW)                         \
    }

} // namespace trust
