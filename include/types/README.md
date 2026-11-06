# Types - Система типов

## Назначение

Закрытая система типов языка TrustLang: идентификаторы типов (`TypeId`/`TypeKind`), реестр типов
(`TypeSystem`) и иерархия типов (примитивные, структуры, классы, тензоры, кортежи, variant). Владелец
публичного контракта системы типов; детали реализации — в `MEMORY.md` подкаталогов.

## Архитектура

- **Закрытая система:** любой пользовательский тип наследуется от существующего; функция без явных
  типов — неявный шаблон.
- **`TypeId` — identity-view, `QualType` — qualifier-view** того же raw; оба определены ОДИН раз в общем
  ABI-заголовке `runtime/trust/type_state.hpp`. Квалификаторы НЕ входят в identity. Параметризованные
  типы интернируются по структуре (`TypeKey`).
- **Реестр** ключуется структурным `StorageKey`; `reset()` на каждый запуск семантики сбрасывает
  пользовательские/структурные типы до builtin-состояния. Встроенный тип — предобъявление + отдельная
  реализация (`=`), для встроенного имени `::=` запрещён.
- **Зоны состояния `TypeSystem`:** мутации — только через зоны-владельцы (`TypeStore`, `TypeNameIndex`,
  `NamedTypeRegistrar`, `MethodTable`, `BuiltinRegistry`, `StructuralInterner`); чтение — через
  `TypeReadModel` (`types/type_read.hpp`). Детали — `registry/MEMORY.md`.
- **Идентичность сигнатур** — `FunctionIdentity`; единая точка нормализации identity —
  `TypeSystem::identityOf(id, TypeIdentityScope)`. Разрешение вызова — единый `resolveCall`
  (`types/call.hpp`) для всех форм вызова.

- **Один владелец формы record:** поля объявленного record пишет только `defineRecord`; читаются через
  аксессоры владельца (`TypeStore::fieldsOf`/`basesOf`/`templateParamsOf`/`instanceOf`; поля payload'а
  приватны); второго хранилища нет (R1: `ARCHITECTURE.md` §3, корневой `MEMORY.md`).

## Документация

- [MEMORY.md](MEMORY.md) — публичный контракт и таблица internal-подкаталогов (`call/`, `func/`, `names/`,
  `registry/`, `subst/`, `type/`).
- [type/TYPE.md](type/TYPE.md) — семантика системы типов; [type/CONST.md](type/CONST.md) — модель
  константности; [type/REFType.md](type/REFType.md) — модель ссылок.
