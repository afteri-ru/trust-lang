# MEMORY.md

> scope: include/transpiler
> role: persistent-memory
> last_reviewed: 2026-08-30
> review_period: 30
> max_size: 12500

## Architecture

`CppTranspiler` — фасад-драйвер (наследник `KindVisitor`): публичный API, обход/диспетчеризация,
рекурсивные точки (`emitExpr`/`emitSequenceBody`/`emitBodyNode`). Mutable-состояние — в `CppEmitContext`
(`emit_ctx.hpp`); тяжёлая кодогенерация — по связным эмиттерам (Type/Decl/Stmt/Expr/Contract), разделяющим
контекст, через ссылку на драйвер. Транспилятор — ТОЛЬКО кодогенерация по «низкоуровневому» AST: всё,
что требует анализа структуры (метки goto, `;`, break/continue), зашито в AST проходом lowering
(semantic/MEMORY.md).

## Facts and invariants

- **⚠ trap (макросы: диапазоны раскрытия):** при МНОГОСТРОЧНОМ макросе (тело с `;`) call_range ставят
  ТОЛЬКО первый и последний вставляемые токены; промежуточные — invalid (НЕ регистрируются в source-map).
  Правило — ко ВСЕМ токенам, включая реальные аргументы: тело может переставлять аргументы
  (`@$cm = @$cd` при обратном порядке даёт begin > end в ASSIGN-правиле). Одиночный (однострочный) макрос
  получает call_range ВСЕМ токенам (инвалидизация промежуточных сломала бы source-map `print`). Валидность
  проверяется ДО маппера; маппер остаётся строгим (EXPECT/FAULT). Полувалидные и кросс-файловые диапазоны
  — легитимные invalid раскрытия, маппер их пропускает. Подстановка `@$...` — КЛОНОМ токена (общий TermPtr
  при повторном использовании аргумента ломал разрешение типов параметров). Комментарий
  `## expanded macro ...` — на ПЕРВОМ термине через `m_docs` (не отдельным Document-узлом — рвалась связь
  атрибута `@[ stack_check @]`); перенос через вложенные макросы и правило `assign_seq`.
- **Манглинг trust-имён — `utils::name_to_cpp`:** ASCII → префикс `c_`; юникод транслит/HEX; `::` →
  `cpp_std$$vector`. **Нативные имена с ведущим `%`** — уже C++-символы: `%` срезается без изменений
  (иначе ломается линковка). `cpp_to_name` восстанавливает `%` (round-trip).
- **Две формы нативных функций:** forward-decl (`%name(...):T := ...;` — линковщик ищет по имени; без
  `::` → extern "C", с `::` → C++-линковка) vs импорт (`name(...):T := %native...;` — алиас, C++-функция
  не эмитится, вызовы переписываются в `native(...)`). `@[link]` — на импорт, не на forward-decl.
- **Заголовки:** `@include("name")` — атрибут (без `;`); пишет в `m_requiredIncludes` напрямую; эмитится
  ТОЛЬКО с объявлением (не глобально). Для нативных типов-шаблонов (structural) в Этапе B → preprocIncludes
  (механизм on-use); для простых алиасов `::=` этот путь НЕ работает (канонизация теряет инклуды;
  types/MEMORY.md).
- **Source map для forward-decl подавляется** (`SourceMapWriter::suppressMapping`): синтетическое
  объявление на сайте импорта не заявляет диапазон модуля (маппится в собственном `.cppt`, иначе коллизия
  trustKey «уже замаплено»). Сопоставление экспортов — по указателям термов.
- **Term-изоляция:** транспилятор не обращается к `m_term` напрямую — диапазоны/имена только через методы
  узлов (`range()`, `nameRange()`, `blockRange()`, `text()`); изменения layout Term не влияют на codegen.
- **Именованные блоки-метки** для break/continue — РУЧНЫМИ goto-метками в lowering, НЕ нативный C++26
  `break label`/`continue label` (P2552) — clang++-22 не поддерживает.
- **⚠ trap (stack-check — инжекция НЕ через SymbolTable.resolve):** на кодогенерации скоуп-стек сброшен к
  глобальному, `m_resolvedTypes->resolve(callee)` функции не находит. `ExprEmitter::stackCheckExpr` ищет
  функцию в `CppEmitContext::m_stackCheck` (заполняет DeclEmitter при эмиссии объявления; declare-before-use).
  Атрибут `@[stack_check]` — ПЕРЕД вызовом; отдельные `%trust_stack_check*` — перехватывает
  `handleStackCheckNative` в visit_CallExpr. Атрибутная вставка оборачивает callee в comma-выражение
  `(check(), call)` (работает и в выражении, и в statement).
- **⚠ trap (типы на кодогенерации — ТОЛЬКО через поля узлов):** скоуп-стек сброшен, `resolveTypeIdByName`
  переменные НЕ находит (резолвятся только ИМЕНА ТИПОВ через findType-fallback). Тип выражения/значения
  обязана доставлять семантика на узле (Binary::resultType, Literal::typeId, VarDecl::inferredType; тип
  match-scrutinee — через синтезированный СЕМАНТИКОЙ `MatchStmt::m_tempDecl`). Иначе транспилятор не знает
  тип и не может выбрать тип-зависимый код.
- **Нативные шаблоны-типы:** `resolveCppTypeId` ветка Group::kNativeTemplate → `cppTemplate<argsCpp>`
  (рекурсивно) + инклуд on-use из preprocIncludes; vector/array (объединены с `:Array`) — Array-ветка.
  `emitTypeNameForNode` для isTemplate резолвит ПОЛНУЮ инстанциацию. Тип-конструктор (m_isNativeTemplateCtor)
  НЕ эмитится как C++-функция.
- **Forward-объявление нативных классов/шаблон-классов** (`String ::= %std::string {...};`,
  `<T1,T2> Pair ::= %std::pair<T1,T2> {...};`): C++-struct НЕ эмитится (класс определён в заголовке);
  `resolveCppTypeId` ветка Group::kNativeClass → `cppName` (NativeClassTypeData), шаблон-класс — через
  kNativeTemplate. Инклуд on-use из preprocIncludes (@[include]). Доступ `obj.%method` → `obj.method`
  (нативное имя из ключа метода, существующий механизм MemberAccess); поле `obj.%field` → `(obj).field`
  (ветка в visit_MemberAccess при lhsType-нативный класс); конструктор `MyStr(...)` → `std::string(...)`
  (ветка в visit_CallExpr при callee-нативный класс); **статический член `Cls.field`/`Cls.st(...)` →
  `cppName::name`/`cppName::name(...)`** (ветка в visit_MemberAccess при left = имя нативного класса;
  статический ключ содержит `::`, C++-имя = последний сегмент; `TypeRegistry::findStaticMethod`).
- **⚠ untyped-`_` кодоген — конкретный тип по записям, НЕ std::any:** локальная `x := _; x = <v>;` эмитит
  `int8_t c_x;` (VarDecl::inferredType заполняет widenInferredTarget; тип литерала как у `x := 5`); декларация
  `_` без записей либо чтение до записи — уже Error в семантике (сюда не доходит). Сброс `x = _;` НЕ эмитит запись
  значения (остаётся пустой оператор/пробел, нет `c_x = c__;`). Типизированный путь `x:Type := _;` / `x:Any := _;` →
  определение без инициализатора `T c_x;` / `std::any c_x;` (не extern).

## Decisions

- **Временные переменные создаёт ТОЛЬКО анализатор (проход lowering)** — как реальные узлы AST
  (`const VarDecl`) с уникальным именем и выведенным типом (`inferredType`). Транспилятор — буквальный
  перевод AST в C++ и НЕ синтезирует временные сам (scrutinee match, while-else, источник деструктуризации,
  hoist возврата — обязаны иметь её в AST). Самостоятельное создание временной в транспиляторе — грубое
  архитектурное нарушение.
- **Исключение — синтез entry в однофайловом «скрипт»-режиме (`-fsingle-file` без `__main__`):**
  DeclEmitter/CppTranspiler оборачивает УЖЕ существующие, семантически размеченные top-level операторы
  модуля (VarDecl/вызовы/управление) в синтезируемую `int <модуль>__main__()` (python-подобно; переменные
  скрипта — локальные main). Это НЕ создание временных и НЕ новый тип/вывод: операторы приходят из AST
  готовыми. Функции/типы/области/`{% %}`-embed остаются на namespace. Режим активен только когда задан
  `CppEmitContext::m_singleFileMode` (runPipeline для корневого модуля главного файла); иначе — прежний
  перевод без изменений. Имя entry задаётся pipeline (`configureSingleFile`) и совпадает с entry_func_name.
