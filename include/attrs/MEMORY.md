# MEMORY.md

> scope: include/attrs
> role: persistent-memory
> last_reviewed: 2026-09-16
> review_period: 30
> max_size: 5000

## Architecture

Чистый слой атрибутов: value-тип `AttrId` (`uint32_t`) + дескриптор `Attr` + мутабельный реестр
`AttrPool` + канонические имена встроенных атрибутов (`attr::*`). Отдельного `AttrType`-enum НЕТ:
встроенные и пользовательские атрибуты хранятся единообразно и идентифицируются по имени. `AttrPool`
— единственная точка регистрации (без `AttrPoolView`); built-in регистрируются конструктором
(`registerBuiltinAttrs`). Парсер `@[...]` (`parse_attr`) живёт в `ast` (ему нужен `Context`/
диагностика) и только ИЩЕТ атрибут в пуле, регистрацию не выполняет.

## Facts and invariants

- **⚠ `AttrId` — битовая маска:** index в `AttrPool` — bits 0–23; флаги обработки — bits 24–31
  (builtin=24, analyzer=25, codegen=26, manual=31; 27–30 reserved). Атрибут без analyzer- и
  codegen-флага — «необработанный» → `-Wunhandled-attr` при любом использовании в AST (проверка после
  `term_to_ast` и после `generateToFile`). `-Wunknown-attributes` — незарегистрированное имя в `@[...]`.
- **Признак built-in выводится из `MapperRange` регистрации:** валидный диапазон = пользовательский,
  невалидный = встроенный (отдельного bool-поля нет).
- **Уникальность по имени:** повторная регистрация возвращает существующий `AttrId`; при совпадении
  имени параметры сверяются. Атрибут не создаётся «на лету» при разборе.
- Дескриптор хранит параметры как `string_view` (сырой текст источника) — владение текстом у
  SourceMapper, дескриптор не копирует.
- `manual`-бит ставится при привязке атрибута к узлу (`add_attr(..., manual=true)`), а не при
  регистрации.

## Decisions

- Атрибуты выделены в отдельный компонент (`attrs_lib`), а не остаются в `ast`: `Attr`/`AttrPool`
  не зависят от AST и нужны `diag::Context` (владение пулом) и `types` (атрибуты на типе).
  Разбор `@[...]` (`attr_parser`) оставлен в `ast`, чтобы не создавать цикл `attrs ⇄ diag`.

## Relations

- Разбор `@[...]` → `AttrId` и привязка к узлам AST — `include/ast` (`attr_parser`, `token_base`).
- Владелец `AttrPool` — фасад `Context` (`include/diag`).
