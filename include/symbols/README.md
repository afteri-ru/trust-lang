# Symbols - символы и видимость имён

## Назначение

Таблица символов (объявления, перегрузки, видимость, сайты импорта) и её проекция для LSP. Строится
семантикой, читается LSP; кодоген таблицу НЕ читает — он использует факты, материализованные в полях AST.

## Состав

- **`symbol_table.hpp`** - фасад `SymbolTable`: стек лексических скоупов и маршрутизация к internal-подкаталогам,
  у каждого один владелец:
  - `symbol.hpp` — значения (`Symbol`/`SymbolOverload`/`DeclResult`/`Storage`);
  - `scope_stack.hpp` — `ScopeStack`/`Scope` (объявления `std::map<StorageKey, Symbol>`, неоднозначности,
    индексы записей `AliasRegistry`);
  - `decl_registry.hpp` — `DeclarationRegistry`: регистрация (forward/definition, перегрузки,
    идемпотентность), экспорт-поверхность, привязка сайта импорта (`bindImportedModule`);
  - `name_resolver.hpp` — `NameResolver`: единственная точка резолва (алиасы → модульный уровень →
    плоский ключ; видимость сайта);
  - `module_surface.hpp` — экспорт-поверхность модулей и реэкспорт;
  - `import_sites.hpp` — активный стек и журнал сайтов импорта (RAII-guard);
  - `alias_registry.hpp` — using-области и namespace-алиасы (записи хранятся один раз);
  - `module_context.hpp` — логический модуль, модульный уровень/корень, file-private;
  - `symbol_key_factory.hpp` — единственная точка построения `StorageKey`;
  - `symbol_arena.hpp` — стабильные адреса долгоживущих копий `Symbol`;
  - `scope_path.hpp` — единственная точка разбора текста namespace-области на сегменты;
  - `decl_binders.hpp` — binder-контекст для `FunctionIdentity`;
  - `symbol_index.hpp` / `reference_index.hpp` — проекции для LSP (объявления/ссылки, видимость).

## Принцип

Видимость имени на сайте импорта — НЕ логика таблицы, а правило `types/names.hpp`; таблица
передаёт вход и применяет решение. Инварианты и ловушки — `MEMORY.md`.
