# MEMORY.md

> scope: include/types
> role: persistent-memory
> last_reviewed: 2026-08-23
> review_period: 30
> max_size: 10983

## Architecture

Закрытая система типов: `TypeKind` (uint32_t) — быстрая идентификация без обращения к реестру +
`TypeRegistry` для метаданных пользовательских/параметризованных типов. `TypeId` = `uint64_t`
`{ TypeKind (upper 32) | registry_index (lower 32) }`. Параметризованные типы (функции/шаблоны/
массивы) интернируются по структуре через `TypeKey` (FoldingSet, `std::unordered_map`).
`TypeData` = `std::variant<SimpleTypeData|FunctionTypeData|TemplateTypeData|ArrayTypeData|...>`;
`data = nullopt` → forward declaration, `SimpleTypeData{}` → полностью определённый без структурных
данных (различие обязано `SimpleTypeData`, иначе нужен отдельный флаг `isComplete`).

## Facts and invariants

- **Краткий инвариант:** `Data=0` → абстрактный тип (группа); `Data≠0` → конкретный встроенный.
  `isBuiltinConcrete(k) == (getData(k) != 0)`.
- **⚠ trap (встроенность):** признак «встроенный» — бит `BuiltinFlag` в TypeKind, а **НЕ**
  `registry_index == 0`: встроенные (машинные) типы регистрируются первыми и занимают `registry_index`
  от 1 до N. `isUserDefinedType(id)` = `index != 0 && index > m_builtinCount` — Бит BuiltinFlag для
  этого не подходит: `registerType()` копирует `kind` из `baseTypeId`, поэтому встроенный алиас
  (`Integer←Int64`) и пользовательский (`MyInt←Int32`) оба имеют BuiltinFlag.
- **Ортогональные квалификаторы-флаги (НЕ часть ключа интернирования):** `kInferredFlag` (тип
  ВЫВЕДЕН) и `kConstFlag` (константность) — биты младшей половины; `const T` и `T` разделяют один
  дескриптор. Все операции идентичности снимают флаг (`getIndexFromId`, `getCanonicalTypeId`),
  сравнения каноникой их не замечают. `getOrCreateStructuralType` выполняет
  `EXPECT(!typeIsInferred(child))`/`!typeIsConst(child)`. Признак различает auto-Bool (продвигается в
  арифметике) и явный Bool (ошибка).
- **kTrustFlag — СЕМАНТИЧЕСКИЙ дифференциатор, НЕ квалификатор вхождения** (в отличие от inferred/
  const): тип с trust-условиями НЕ эквивалентен идентичному без них, поэтому бит входит в `TypeKind`
  и в ключи структурного интернирования (`TypeKey::kind`) — интернируются раздельно.
- **RefType — один признак ссылки на объявление:** `kValue|kShared|kWeak|kUnique|kPtr|kMptr|kRef|
  kRref|kPtrPtr|kTake`. Первая ссылка на тип без признака — fast-path бит; ссылка на уже ссылочный
  тип — составной узел `getOrCreateRefType` (`RefTypeData`, группа `kReftype`).
- **preprocIncludes** — ЕДИНЫЙ список директив без понятия «зависимостей»; первый элемент — основной
  заголовок (определяет C++-имя), остальные — транзитивно требуемые. Элемент с ведущим `@`
  (напр. `"@trust/rational.hpp"`) — **рантайм-заголовок**: путь совпадает с именем ELF-секции внутри
  `trust-runtime.so` (встроено через `#embed`); транслируется в реальный `#include`, Pipeline извлекает
  секции из `.so` во временный каталог и добавляет в `build.conf` — только для реально использованных.
- **Методы хранятся как полный ключ-имя → интернированный функциональный тип** (`TypeDescriptor::methods`:
  `map<string, TypeId>`); ключ кодирует нативность (ведущий `%`) и константность (хвостовой `^`),
  напр. `"%count^"` — нативность/константность ВЫВОДЯТСЯ из ключа, отдельных полей нет. `addMethod`
  EXPECT'ом проверяет, что алиас полностью повторяет семантику цели; `findMethodInfo` для алиаса
  возвращает ключ ЦЕЛИ. const и не-const перегрузки — разные записи (`%get^`/`%get`).
- **Инвариант (рантайм-символ не должен дублировать тип):** `RuntimeSymbol` — ТОЛЬКО не-типовые
  функции. Единый источник описания — `types/runtime_symbols.hpp` (X-macro `TRUST_RUNTIME_SYMBOLS`),
  типизированный `RuntimeSymbolId` исключает опечатку. `registerRuntimeSymbol` содержит EXPECT-проверку
  «ни один зарегистрированный тип не имеет того же C++-имени» — попытка зарегистрировать `trust::Dict`
  как символ — ошибка. Сырая C++-вставка `{% trust::Dict d; %}` без типизированной ссылки НЕ тянет
  заголовок (ожидаемо).
- **Два независимых механизма подключения инклудов** (без сканирования выходного буфера):
  №1 — по типу (`TypeRegistry`): во время обхода собираются только ИСПОЛЬЗОВАННЫЕ канонические типы в
  `CppTranspiler::m_usedTypes` (`recordUsedType` из `resolveCppTypeId`), директивы формируются ПОСЛЕ
  полного обхода (`collectTypeIncludes`); №2 — по рантайм-символу (там, где типа нет: EMBED-узлы).
  ЕДИНСТВЕННЫЙ способ записи заголовков символьного механизма — `recordRuntimeSymbolHeaders(RuntimeSymbolId)`
  (строковой перегрузки нет; для `visit_EmbedExpr` — substring-скан текста вставки).
- **Range:** методы объявлены ОДИН раз на абстрактном `:Range`, элемент-зависимые слоты помечены
  типовым параметром `T`; при резолве `Range<Elem>` `instantiateRangeMethod` подставляет `T→Elem`
  через интернированный функциональный тип. `:Range` (Data=0) → `auto`, конкретный `Range<Elem>` →
  `trust::Range<ElemCpp>`.
