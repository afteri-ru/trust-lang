// src/semantic/ellipsis.cpp
// ЕДИНЫЙ примитив семейства «замыкающего» многоточия в списках (аргументы вызова/элементы массива):
// разбор, структурные правила, тип-совместимость и МАТЕРИАЛИЗАЦИЯ (разворот в конкретные элементы).
// Кодогенерация о многоточии не знает: в AST она получает готовый список.
#include "semantic/ellipsis.hpp"

#include "diag/diag.hpp"
#include "semantic/pass.hpp"
#include "types/promotion.hpp"
#include "types/registry.hpp"
#include "types/type_names.hpp"

#include <algorithm>
#include <functional>

namespace trust {

namespace {

/// Классификация ЭЛЕМЕНТА списка по его значению.
struct Classified {
    EllipsisForm form = EllipsisForm::None;
    const AstNodeBase* node = nullptr;
    AstNodePtr operand;
};

Classified classifyValue(const AstNodePtr& value) {
    Classified c;
    if (!value) {
        return c;
    }
    // `... expr ...` (Fill): операнд - единственный ребёнок.
    if (value->kind() == ParserToken::Kind::Filling) {
        c.form = EllipsisForm::Fill;
        c.node = value.get();
        const auto& f = static_cast<const Sequence&>(*value);
        if (!f.m_body.empty()) {
            c.operand = f.m_body[0];
        }
        return c;
    }
    // `...` (Repeat) / `... src` (Spread): пустой m_body - повтор, иначе раскрытие источника.
    if (value->kind() == ParserToken::Kind::Ellipsis) {
        c.node = value.get();
        const auto& seq = static_cast<const Sequence&>(*value);
        if (seq.m_body.empty()) {
            c.form = EllipsisForm::Repeat;
        } else {
            c.form = EllipsisForm::Spread;
            c.operand = seq.m_body.back();
        }
        return c;
    }
    return c;
}

/// Значение элемента списка: массив - ArgNode::m_value, вызов - сам элемент.
const AstNodePtr& arrayCarrier(const AstNodePtr& el) {
    static const AstNodePtr kNone;
    if (!el || el->kind() != ParserToken::Kind::ArgNode) {
        return kNone;
    }
    return static_cast<const ArgNode&>(*el).m_value;
}

const AstNodePtr& callCarrier(const AstNodePtr& el) {
    return el;
}

using Carrier = const AstNodePtr& (*)(const AstNodePtr&);

EllipsisInfo scanList(const std::vector<AstNodePtr>& els, Carrier carrier) {
    EllipsisInfo info;
    for (size_t i = 0; i < els.size(); ++i) {
        const Classified c = classifyValue(carrier(els[i]));
        if (c.form == EllipsisForm::None) {
            ++info.explicitCount;
            continue;
        }
        ++info.count;
        info.form = c.form;
        info.isLast = (i + 1 == els.size());
        if (!info.operand) {
            info.operand = c.operand;
        }
    }
    return info;
}

/// Аксессоры приёмника для материализации.
struct ListAccess {
    /// Список элементов массива (значение в ArgNode::m_value) или аргументов вызова.
    bool arrayElements = false;
    /// Превратить элемент-многоточие в носитель операнда Fill (мутация на месте).
    std::function<void(AstNodePtr& el, const AstNodePtr& operand, TypeId operandType)> makeFillElement;
};

/// ЕДИНЫЙ разворот fill/repeat при известной capacity. targetTypes - целевые типы позиций
/// (проверка совместимости операнда Fill). false - диагностика выдана.
bool expandImpl(std::vector<AstNodePtr>& els, AnalysisContext& actx, const EllipsisInfo& info, const ListAccess& acc, size_t capacity,
                const std::vector<TypeId>& targetTypes, const AstNodeBase& node, std::string_view what) {
    if (info.form == EllipsisForm::None || info.form == EllipsisForm::Spread) {
        return true; // Spread - зона пост-прохода (рантайм-раскрытие не материализуется)
    }
    if (capacity < info.explicitCount) {
        actx.ctx().diag().report(Severity::Error, node.range(), "{}: явных элементов ({}) больше, чем позиций ({})", what, info.explicitCount, capacity);
        discardEllipsisElements(els, acc.arrayElements);
        return false;
    }
    if (info.form == EllipsisForm::Repeat && info.explicitCount == 0) {
        actx.ctx().diag().report(Severity::Error, node.range(), "{}: '...' нечего повторять - перед ним нет явных элементов", what);
        discardEllipsisElements(els, acc.arrayElements);
        return false;
    }
    const TypeRegistry& reg = actx.ctx().types();
    const TypeId opType = info.operand ? actx.exprType(*info.operand) : INVALID_TYPE_ID;
    if (info.form == EllipsisForm::Fill) {
        for (size_t p = info.explicitCount; p < capacity && p < targetTypes.size(); ++p) {
            if (!fillingTypeCompatible(reg, opType, targetTypes[p])) {
                actx.ctx().diag().report(Severity::Error, node.range(), "{}: тип операнда '{}' несовместим с типом позиции '{}'", what,
                                         reg.getFullTypeName(opType), reg.getFullTypeName(targetTypes[p]));
                discardEllipsisElements(els, acc.arrayElements);
                return false;
            }
        }
    }
    // МАТЕРИАЛИЗАЦИЯ: конкретный список (производная величина в AST не хранится).
    const size_t addCount = capacity - info.explicitCount;
    const size_t ellipsisIndex = els.size() - 1; // многоточие - последнее (проверено validateEllipsis)
    std::vector<AstNodePtr> out;
    out.reserve(capacity);
    for (size_t i = 0; i < info.explicitCount; ++i) {
        out.push_back(els[i]);
    }
    if (info.form == EllipsisForm::Fill) {
        AstNodePtr& fillEl = els[ellipsisIndex];
        acc.makeFillElement(fillEl, info.operand, opType);
        for (size_t r = 0; r < addCount; ++r) {
            out.push_back(fillEl); // один узел -> в C++ ПЕРЕВЫЧИСЛЯЕТСЯ на каждую позицию
        }
    } else { // Repeat: циклический повтор явного префикса (последний цикл усекается по capacity)
        for (size_t r = 0; r < addCount; ++r) {
            out.push_back(els[r % info.explicitCount]);
        }
    }
    els = std::move(out);
    return true;
}

} // namespace

bool fillingTypeCompatible(const TypeRegistry& reg, TypeId operand, TypeId target) {
    if (operand == INVALID_TYPE_ID || target == INVALID_TYPE_ID) {
        return true;
    }
    const TypeId oc = reg.getCanonicalTypeId(operand);
    const TypeId tc = reg.getCanonicalTypeId(target);
    if (oc == tc) {
        return true;
    }
    const TypeId anyId = reg.getType(type_generic::Any);
    if (anyId != INVALID_TYPE_ID && tc == reg.getCanonicalTypeId(anyId)) {
        return true;
    }
    const Group og = getGroup(getKindFromId(oc));
    const Group tg = getGroup(getKindFromId(tc));
    const bool oNum = (og == Group::kIntegers || og == Group::kUnsigned);
    const bool tNum = (tg == Group::kIntegers || tg == Group::kUnsigned);
    return oNum && tNum && og == tg && getData(getKindFromId(oc)) <= getData(getKindFromId(tc));
}

EllipsisInfo scanCallEllipsis(const CallExpr& call) {
    static const std::vector<AstNodePtr> kEmpty;
    return scanList(call.m_args ? *call.m_args : kEmpty, callCarrier);
}

EllipsisInfo scanArrayEllipsis(const DictLiteralNode& node) {
    return scanList(node.m_body, arrayCarrier);
}

bool validateEllipsis(const EllipsisInfo& info, const AstNodeBase& node, AnalysisContext& actx, std::string_view what) {
    if (info.form == EllipsisForm::None) {
        return true;
    }
    if (info.count > 1) {
        actx.ctx().diag().report(Severity::Error, node.range(), "{}: многоточие допустимо только один раз", what);
        return false;
    }
    if (!info.isLast) {
        actx.ctx().diag().report(Severity::Error, node.range(), "{}: многоточие обязано быть последним", what);
        return false;
    }
    return true;
}

bool expandCallEllipsis(CallExpr& call, AnalysisContext& actx, const EllipsisInfo& info, const std::vector<TypeId>& paramTypes, bool variadic) {
    if (info.form == EllipsisForm::None || info.form == EllipsisForm::Spread || !call.m_args) {
        return true;
    }
    if (variadic) {
        actx.ctx().diag().report(Severity::Error, call.range(), "аргументы вызова: число позиций неизвестно (вариативный вызываемый)");
        discardEllipsisElements(*call.m_args, false);
        return false;
    }
    ListAccess acc;
    acc.arrayElements = false;
    // Вызов: элемент-многоточие заменяется самим операндом (в т.ч. выход из ArgNode-обёртки).
    acc.makeFillElement = [](AstNodePtr& el, const AstNodePtr& operand, TypeId) { el = operand; };
    return expandImpl(*call.m_args, actx, info, acc, paramTypes.size(), info.form == EllipsisForm::Fill ? paramTypes : std::vector<TypeId>{}, call,
                      "аргументы вызова");
}

bool expandArrayEllipsis(DictLiteralNode& node, AnalysisContext& actx, const EllipsisInfo& info, size_t capacity, TypeId elementType) {
    if (info.form == EllipsisForm::None || info.form == EllipsisForm::Spread) {
        return true;
    }
    ListAccess acc;
    acc.arrayElements = true;
    // Массив: элемент-многоточие - ArgNode; операнд кладётся в его значение (+ тип значения).
    acc.makeFillElement = [](AstNodePtr& el, const AstNodePtr& operand, TypeId operandType) {
        if (el && el->kind() == ParserToken::Kind::ArgNode) {
            auto& a = static_cast<ArgNode&>(*el);
            a.m_value = operand;
            a.resultType = operandType;
        } else {
            el = operand;
        }
    };
    const std::vector<TypeId> targets(capacity, elementType);
    return expandImpl(node.m_body, actx, info, acc, capacity, targets, node, "элементы массива");
}

void reportUnresolvedEllipsis(AnalysisContext& actx, const std::vector<AstNodePtr>& roots) {
    std::function<void(const AstNodePtr&)> walk = [&](const AstNodePtr& n) {
        if (!n) {
            return;
        }
        // Только СПИСКОВЫЕ позиции: аргументы вызова и элементы коллекций/конструкций
        // (в остальных позициях Ellipsis - служебный маркер: forward-класс, rest-цель, тело без
        // реализации, `X []= ... dict` (реализовано), вариативные параметры макросов).
        const bool isCall = (n->kind() == ParserToken::Kind::CallExpr);
        const bool isCollection =
            (n->kind() == ParserToken::Kind::ArrayInit || n->kind() == ParserToken::Kind::DictLiteral || n->kind() == ParserToken::Kind::Tuple);
        if (isCall || isCollection) {
            std::vector<AstNodePtr> els;
            if (isCall) {
                const auto& call = static_cast<const CallExpr&>(*n);
                if (call.m_args) {
                    els = *call.m_args;
                }
            } else {
                els = static_cast<const Sequence&>(*n).m_body;
            }
            for (const auto& el : els) {
                const Classified c = classifyValue(isCall ? callCarrier(el) : arrayCarrier(el));
                if (c.form == EllipsisForm::None || !c.node) {
                    continue;
                }
                switch (c.form) {
                case EllipsisForm::Fill:
                    actx.ctx().diag().report(Severity::Error, c.node->range(),
                                             "FILLING '... expr ...' требует известного числа позиций (укажите размер массива или сигнатуру вызываемого)");
                    break;
                case EllipsisForm::Repeat:
                    actx.ctx().diag().report(Severity::Error, c.node->range(), "'...' требует известного числа позиций (размер массива/сигнатура вызываемого)");
                    break;
                case EllipsisForm::Spread:
                    actx.ctx().diag().report(Severity::Error, c.node->range(), "раскрытие '... <источник>' в этой позиции не реализовано");
                    break;
                case EllipsisForm::None:
                    break;
                }
            }
        }
        std::vector<AstNodePtr*> slots;
        n->collectChildren(slots);
        for (auto* slot : slots) {
            if (slot) {
                walk(*slot);
            }
        }
    };
    for (const auto& r : roots) {
        walk(r);
    }
}

void discardEllipsisElements(std::vector<AstNodePtr>& elements, bool arrayElements) {
    const Carrier carrier = arrayElements ? arrayCarrier : callCarrier;
    elements.erase(
        std::remove_if(elements.begin(), elements.end(), [&](const AstNodePtr& el) { return classifyValue(carrier(el)).form != EllipsisForm::None; }),
        elements.end());
}

} // namespace trust
