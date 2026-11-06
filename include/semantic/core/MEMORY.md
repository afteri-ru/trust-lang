# MEMORY.md

> scope: include/semantic/core
> role: persistent-memory
> last_reviewed: 2026-11-12
> review_period: 30

## Architecture

Ядро обхода: `NameResolutionPass` владеет обходом и скоуп-стеком и издаёт события; анализаторы подключаются
через `InlineAnalysisHook`, `SemanticPassRunner` — упорядоченный реестр хуков. `AnalysisContext` — агрегат
без логики: `SymbolTable` + `NamespaceScope`, `TypeResolution` (резолв типов/модульных ссылок), `ExprFacts`
(type-state), `AnalysisState` (LSP-проекция, module-набор). `captureLastResult` — пре-семантический проход.
Детали зон — в `MEMORY.md` подкаталогов `decl/`, `typer/`, `ref/`, `verify/`.

## Facts and invariants

- **⚠ `resolveTypeRef` разрешает и МАТЕРИАЛИЗУЕТ факт в узле аннотации:** identity пишется в
  `IdentName::resolvedTypeId`, полный тип с квалификаторами — в `IdentType::resolvedState` (ОДИН раз, кэш).
  Анализаторы не переприменяют attrs аннотации — только attrs узла объявления (`applyRefAttrs`). Кодоген
  читает тип из узла и не резолвит имя. Тип-имена в позициях `Ident` материализует `recordTypeName`.
- **`captureLastResult` — ПРЕ-семантический проход** (`$^` переписывается до ядра).
- **⚠ WRITER-доступ к реестру `types` — capability зоны записи (E1/R12):** `AnalysisContext(ctx, TypeSystem&)`
  и `TypeResolution(ctx, TypeSystem&)` получают `TypeSystem&` (владелец — `Pipeline`) и отдают его через
  `registry()`; фасад `Context` mutable-реестр не предоставляет (`types()` — только `const`). Новые писатели
  берут реестр через `registry()`, а не через `ctx.types()`.
- **⚠ `@__CHECK_AREA__` удаляется В семантике:** узел обрабатывается по единому скоуп-стеку создателей
  областей и заменяется пустым узлом (иначе FAULT в транспиляторе).
- **⚠ whole-if/whole-match:** `IfStmt`/`MatchingStmt` открывают скоуп ХВОСТОМ (`doBranch`), НЕ в
  `doScopeContainer` — та возвращается до `typeExpr` и теряет scrutinee-пост-обработку match. Отдельные
  elseif/else/case НЕ заявляются.
- **⚠ Оператор `... = X` (using) — регистрация областей ПОИСКА, а не импорт:** пути накапливаются до конца
  области (`... = _` — ошибка); голое `n` пробует `prefix::n`, текст ссылки НОРМАЛИЗУЕТСЯ на `prefix::n`
  (иначе кодоген эмитит плоский C++-символ). Область проверяется на ВЫХОДЕ из скоупа (порядок not важен).
  Область — ТОЛЬКО namespace (тип владеет членским скоупом и отклоняется); только TRUST-имена.
- **⚠ Файлы/импорт и маски:** сайт импорта SCOPE-TRANSPARENT; файл не namespace-сегмент. Объявления
  импортированного файла регистрируются ПОЛНОСТЬЮ, исключённое маской имя — file-local ключ. Импорт НЕ
  транзитивен; повтор под тем же ключом — дубль.
- **⚠ Имя из импортированного модуля — через область имён/короткое имя:** импорт регистрирует
  объявления владельца в экспорт-поверхности; тип берётся из РАЗРЕШЁННОГО символа (не из текста),
  иначе аннотация без типа → ICE в HIR. Ссылочная форма `\path::name` удалена — модуль не префикс.
- **⚠ Имя-алиас (`f :- ...;`) — объявление ФАЙЛА, не безусловный экспорт:** file-private и локальный/блочный
  алиас в экспорт-поверхность НЕ входят. Модульный алиас (`\m :- \lib;`) — spelling→ModuleIndex, новой
  идентичности нет.
- **⚠ Неоднозначное голое имя** диагностируется на сайте ИСПОЛЬЗОВАНИЯ (`reportAmbiguousName`); `resolve` НЕ
  возвращает неоднозначную плоскую привязку. Материализация `resolvedTypeId` у ОБЪЯВЛЕНИЯ — через
  `lookupTypeName`.
- **⚠ duplicate-import:** ключ `(файл, depth, импорт, нормализованные маски)`; ОДНО сообщение на сайт импорта;
  разные маски/scope допустимы; ключи чистятся при exit scope.
- **⚠ Namespace/private:** при выходе из `::`/`name::` объявления переносятся в корневой файловый скоуп под
  `namespacePath()`. `__::` — file-private (ключ `(moduleIndex,name)`, виден всему файлу); `resolve` пробует
  private текущего файла, затем exported — private не протекает в импортёр. `_::` (protected) — плоская
  привязка ТОЛЬКО при доступе группы (`canAccessProtected`, правило `types/names.hpp`; доступ вычисляется ОДИН
  раз на часть сайта и передаётся и в `pushImportScope`, и в `bindImportedModule`). `namespacePath()` кэшируется
  по `(depth, creator скоупа)`.
- **⚠ Инварианты хуков/опций:** `declareOrComplete` НЕ перемещает `Symbol` (копирует) — `onDeclare` видит
  полноценный Symbol; `-q` подавляет только Warning/Note; feature-флаг и DiagId не делят cli-имя.
- **⚠ Класс-скоуп открывается/закрывается ТОЛЬКО через `NameResolutionPass::enterScope/exitScope`** (фреймы
  синхронны со скоуп-стеком). Прямой `symbols().push/pop` ЗАПРЕЩЁН (рассинхронизирует хуки).
- **⚠ Лямбда — особый путь в `analyzeNode`, НЕ `analyzeFuncDecl`:** имя НЕ регистрируется в объёмлющем скоупе;
  захваты резолвятся ДО открытия скоупа лямбды; локальные имена нормализованы в `$name`. Захват только по
  значению; есть синтетическая метка-имя → `@return v;` валидируется по `currentFunc()` и понижается в `return`.
- **Ловушка ContextMacroExpander:** обход детей — через `collectChildren` (ссылки на слоты для замены);
  мутирующий `onNode` возвращает true, если заменён.
- **⚠ trust-контракты:** `collectChildren()` их не отдают — ядро обрабатывает явно; severity `-Wsolver`
  действует только когда `--solver-mode` НЕ задан. Правила Pre/Post и защита от авто-вывода — в
  `verify/MEMORY.md` (авторитет).
- **⚠ `bareLocalName` (срезает `$`) ≠ `utils::bareName` (срезает `%`/`^`):** разные домены имён (локальные
  переменные vs ключ метода); не смешивать.
- **⚠ `1:Bool < 5` парсится как СРАВНЕНИЕ** (ловушка шаблон-аргументов, учитывается в резолве типов).

## Relations

- `semantic/decl`, `semantic/typer`, `semantic/ref`, `semantic/verify` (зоны-потребители), `symbols`
  (`SymbolTable`), `types` (`TypeResolution`), `module_loader` (экспорт-набор/видимость), `ast` (узлы/слоты).
