#pragma once

// include/types/subst/write_context.hpp
// Контекст ПОДСТАНОВКИ типов: структура читается через read-модель, результат подстановки
// интернируется зоной StructuralInterner. Параметр-контекст (агрегат двух ссылок), а не
// обёртка с логикой: он нужен, чтобы рекурсивные вызовы не протаскивали пару аргументов.
//
// Время жизни: `read` — ссылка на существующую модель. Хранить контекст дольше выражения, в
// котором создана модель (напр. временная `ctx.readTypes()`), нельзя.

namespace trust {

class StructuralInterner;
class TypeReadModel;

struct TypeWriteContext {
    const TypeReadModel& read;
    StructuralInterner& interner;
};

} // namespace trust
