# Types - Система типов

## Назначение

Закрытая система типов языка TrustLang: идентификаторы типов (TypeId/TypeKind), реестр типов (TypeSystem) и иерархия типов (примитивные, структуры, классы, тензоры, кортежи, variant).

## Особенности реализации

- **Закрытая система** - любой пользовательский тип наследуется от существующего; функция без явных типов - неявный шаблон.
- **TypeId / TypeKind** - идентификаторы типов: `TypeKind` (группа/данные/ref-вид/флаги) + `registry_index` (24 бита); `TypeId` — identity-value 64-битного значения (`operator==`/`operator<=>`/`std::hash` — только по identity), `QualType` — qualifier-view того же raw (identity + const/inferred). Оба класса определены ОДИН раз в `runtime/trust/type_state.hpp` (общий ABI-заголовок компилятора и рантайма; `types/type_id.hpp` добавляет только `makeTypeId` с EXPECT и `isTrustedType`). Qualifier-биты не входят в identity и снимаются `getUnqualifiedType`/`getRegistryIndex`; `raw()` — полное сырое значение, не ключ сравнения. На границе `trust::TypedValue` identity встроенных типов нормализуется к kind-only. Структурные типы интернируются по `TypeKey`.
- **Registry** - глобальный реестр типов для runtime-диспетчеризации (boxing/unboxing, dynamic_cast). Имена типов ключуются структурным `StorageKey` (`types/names.hpp`): видимость/модуль — поля, namespace-путь и имя — views пула строк, интернированные при регистрации (`internStorageKey`); `TypeDescriptor::name` — владеющая display-копия, поэтому `getTypeName()` для пользовательских алиасов возвращает точное имя. `reset()` на каждый запуск семантики сбрасывает пользовательские/структурные типы до builtin-состояния. Структурные типы с пустым именем (функциональные сигнатуры) не регистрируются в name-индексе. `isUserDefinedType()` различает пользовательские алиасы и встроенные (Integer/String/Char…) по `registry_index` (встроенные регистрируются первыми, `index ≤ BuiltinTypeCore::builtinCount`). `isBuiltinType()` — признак ПРЕДОБЪЯВЛЕНИЯ (реализация может отсутствовать); `BuiltinRegistry::define(read, name, …)` — определение реализации предобъявленного типа без создания нового TypeId (см. `MEMORY.md`).
- **Зоны состояния `TypeSystem`** (композиционный корень + владельцы, как `SymbolTable`): `TypeStore` (`types/registry/type_store.hpp`) — дескрипторы пользовательских типов в `std::deque` (стабильные адреса), `declaredRecords`, выдача `registry_index`, проекции identity/полей/namespace; `TypeNameIndex` (`types/registry/type_name_index.hpp`) — привязки `StorageKey → TypeId`, встроенный домен, дубликат-диагностика; `NamedTypeRegistrar` (`types/registry/named_type_registrar.hpp`) — создание/жизненный цикл именованных типов (алиас/второе имя/enum/variant/record forward→definition/native), транзакция reserve→push→bind; `MethodTable` (`types/registry/method_table.hpp`) — наборы сигнатур/алиасы методов/статические члены; `BuiltinRegistry` (`types/registry/builtin_registry.hpp`) — реализации встроенных типов и рантайм-символы; `StructuralInterner` (`types/registry/structural_interner.hpp`) — TypeKey-интернирование и создание структурных/функциональных/ссылочных/контейнерных типов; `TypePrinter` (`types/registry/type_printer.hpp`) — печать Trust/C++ имён; `TypeCodeCache` (`types/registry/type_code_cache.hpp`) — memo кодов; `FunctionIdentity` (`types/func/fn_identity.hpp`) — идентичность сигнатур. Мутирующие примитивы зон — `private` + `friend` (только оркестраторы); чтение — через `TypeReadModel` (`types/type_read.hpp`; запрос одной зоны — через `store()/names()/printer()/builtins()/methods()`, на модели — именной фасад и `identity()`), запись — только через API зон-владельцев.
- **Friend-граф зон записи (единственные владельцы):** `TypeStore` ← `TypeSystem`, `NamedTypeRegistrar`,
  `MethodTable`, `StructuralInterner`; `TypeNameIndex` ← `TypeSystem`, `NamedTypeRegistrar`,
  `StructuralInterner`; приватные помощники `TypeSystem` ← `FunctionIdentity`, `NamedTypeRegistrar`,
  `MethodTable`, `StructuralInterner`; `BuiltinRegistry::setCore` ← `TypeSystem`. Seed-строитель ядра
  встроенных типов — зона `BuiltinCoreBuilder` (`types/registry/builtin_core_builder.hpp`): строит ядро
  над локальным набором зон, корень лишь разделяет результат. Число встроенных дескрипторов
  имеет единственный источник — `BuiltinTypeCore::builtinCount`.
- **Идентичность сигнатур** - `FunctionIdentity` (`types/call.hpp`): одна сегментная форма
  (`signatureSegments`) — единственный источник identity; `sameCppOverload` сравнивает параметры, а
  `compare(..., IdentityScope)` даёт единую точку (`kOverload` / `kInterop` / `kDeclaration`) с
  tri-state результатом (`kDifferent` / `kEquivalent` / `kTailMismatch`) — «половину» контракта
  объявления снаружи проверить нельзя. В позиции возврата отсутствие аннотации, `Void` и `None` —
  один C++-тип (`void`). C++-суффикс имени — проекция того же массива сегментов
  (имя == identity объявления). Отношение
  канонизации — `TypeIdentityScope { kStructural, kSemantic, kCanonical }`. Пользовательский алиас (`::=`) — отдельный
  номинальный тип Trust (в C++ — `using`); встроенные алиасы прозрачны (`TypeSystem::semanticTypeId`).
  Интернирование (`TypeId`) — отдельное as-written отношение. Единая точка нормализации identity —
  `TypeSystem::identityOf(id, TypeIdentityScope{kStructural|kSemantic|kCanonical})`. Для нативных
  C++-символов — FFI-идентичность
  (`IdentityScope::kInterop`), т.к. внешний C++ не различает `using`.
- **Разрешение вызова** - единый `OverloadCandidate` + `resolveCall` (`types/call.hpp`) для всех форм вызова (free/method/operator/`[]`/`()`). `:Any` (`std::any`) — конкретный тип: `Any→Any` exact, `Any→concrete` нежизнеспособен (явная конверсия — `:T(expr)` → `trust::any_to<T>`); неизвестный (`INVALID`) аргумент остаётся Any-like (без каскада). При равенстве рангов фиксированная арность специфичнее variadic/pack (C++ partial ordering).

## Документация

- [MEMORY.md](MEMORY.md) - публичный контракт системы типов и таблица internal-подкаталогов; детали — в
  `MEMORY.md` подкаталогов (`call/`, `func/`, `names/`, `registry/`, `subst/`, `type/`).
- [TYPE.md](type/TYPE.md) - семантика системы типов (иерархия, группы, категории).
- [CONST.md](type/CONST.md) - модель константности объектов (два уровня: «в типе» и «пер-переменная»),
  сравнение подходов в языках, кодогенерация `const`/`const_cast<>`.
- [REFType.md](type/REFType.md) - модель ссылок: плоский `RefType`, кодирование (бит/узел),
  атрибут `@[reftype("...")]`, причины выбора и ограничения.