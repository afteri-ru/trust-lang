#pragma once
// include/ast/slots/slot_engine.hpp
// Исполнитель класс-таблицы слотов (`ast/slots/node_slots_x.hpp`): классификация слота, visitor-интерфейс
// и генерация метода `forEachSlot` класса. Данные — таблица; здесь ТОЛЬКО механика.
//
// Три потребителя (по одному visitor на задачу):
//   - обход        -> собирает `Traversed`-слоты (children()/collectChildren);
//   - клон         -> копирует ВСЕ владеемые слоты (`Traversed` + `Internal`);
//   - ремап ссылок -> переписывает невладеющие ссылки (`NonOwn*`).
//
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

// -- Эмиссия строк таблицы: shape выбирает макрос AST_ROW_<shape> -------------------------

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

#define AST_SLOT_ROW(shape, field, walk, filter) AST_ROW_##shape(field, AST_WALK_##walk, filter)

/// Объявление `forEachSlot` класса по его строкам таблицы: сначала слоты БАЗЫ (порядок
/// обхода задаётся базой), затем собственные строки. `m_trust` эмитит `AstNodeBase`.
#define AST_SLOT_METHOD(BASE, ROWS)                \
    void forEachSlot(AstSlotVisitor& v) override { \
        BASE::forEachSlot(v);                      \
        ROWS(AST_SLOT_ROW)                         \
    }

} // namespace trust
