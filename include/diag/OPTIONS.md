# Система группировки опций и флагов

Документ описывает, как в TrustLang организованы опции/флаги, их **группировка** и
**принципы использования**. Две отдельные области:

1. **Реестр диагностик анализа кода** — `include/diag/options.hpp` (компонент `diag`).
2. **Опции драйвера (CLI)** — `include/pipeline/cli.hpp` (компонент `pipeline`).

Они **не смешиваются**: реестр диагностик — для анализа кода (анализаторы, проверки),
опции драйвера — для CLI конкретных приложений (trust, trust-lsp, trust-dap).

**Нейминг привязан к назначению (без пересекающихся имён):** `CliOpt` — вид значения опции CLI
(арность), `CliCategory` — категория группировки `--help`. Символы `cli.hpp` лежат прямо в
`namespace trust` (без `namespace cli`).

**Пер-компонентные id диагностик:** единого глобального `DiagId` НЕТ. Каждая компонента
объявляет СВОЙ enum (`trust::semantic::DiagId`, `trust::syntax::DiagId`, `trust::diag::DiagId`,
`trust::semantic::FlagKind`, `trust::transpiler::FlagKind`) через переиспользуемый механизм
`TRUST_DIAG_SET`/`TRUST_FLAG_SET` (определён один раз в `diag/diag_set.hpp`). Обобщённые
`Options::add<T>`/`report<T>` в diag работают с любым пер-компонентным id через ADL
(`diagName`/`flagName`/... из namespace компоненты), поэтому diag остаётся листом.

---

## 1. Реестр диагностик (`Options`)

### 1.1. Две категории сущностей

| Сущность | Пер-компонентная декларация | Регистрация (единый API) |
|---|---|---|
| Severity-диагностика | `TRUST_DIAG_SET(NS, DiagId, LIST)` | `Options::add<T>(T id)` — метаданные через ADL |
| Feature-флаг | `TRUST_FLAG_SET(NS, FlagKind, LIST)` | `Options::add_flag<T>(T id)` — метаданные через ADL |

- **Severity-диагностика** — уровень настраивается (`-W<name>=<severity>`), привязывается
  к группам-агрегатам (маска `WarnGroup`). Метаданные задаются в пер-компонентной декларации.
- **Feature-флаг** — булев переключатель анализатора/кодогенерации, группами не управляется,
  имеет категорию для справки (`DiagGroup`). Метаданные задаются в пер-компонентной декларации.

> **Конвенция (GCC/Clang): severity-диагностики и поведенческие флаги не смешиваются.**
> Severity-диагностики используют фиксированный набор значений `-W<name>=ignore|warning|error`
> (напр. `-Wsolver=`, `-Wsolver-loop=`). Поведенческий флаг с произвольным значением не выносится
> под `-W` (и наоборот): value-флаги, управляющие поведением, задаются как `--<name>=<value>`
> (напр. `--keywords=`, `--solver-mode=`), булевы — `-f<name>`/`-fno-<name>` (напр.
> `-fsolver-loop-unroll`). Severity-значения (`ignore|warning|error`) не являются значениями
> поведенческих флагов.

Каждая компонента объявляет СВОИ диагностики/флаги в своём заголовке (`semantic/diag.hpp`,
`syntax/diag.hpp`, `transpiler/diag.hpp`, `diag/base_diags.hpp`) через `TRUST_DIAG_SET`/
`TRUST_FLAG_SET` — это **пер-компонентный единственный источник данных** (enum + имя + help +
severity + группы + категория). Регистрация `Options::add<T>`/`add_flag<T>` подтягивает метаданные
через ADL; справочная информация выводится централизованно через `-Whelp`.

### 1.2. `DiagGroup` — категории для справки `-Whelp`

`DiagGroup { Diagnostics, Analysis, Codegen }` — категория, по которой **группируется вывод
`-Whelp`** (не управляет включением/выключением). Категория задаётся в пер-компонентной
декларации (у severity-диагностик всегда `Diagnostics`).

### 1.3. `WarnGroup` — группы-агрегаты (стиль clang)

Группы-агрегаты (`Wall`, `Wextra`, `Wpedantic`, `Wunused`, `Wdeprecated`, `Wformat`,
`Wconversion`) определены центрально единым X-macro **`WARN_GROUPS`** (одна строка на группу:
CLI-суффикс, отображаемое имя, битовый индекс). Из него генерируются: enum `WarnGroup`
(битовая маска), `warnGroupName()`/`warnGroupCli()` и список `kAllWarnGroups`. Добавление
группы = одна строка, остальное подхватывается автоматически.

**Центрально определены только группы/агрегаты.** Отдельные диагностики объявляются в
пер-компонентном наборе (TRUST_DIAG_SET) и привязываются к **одной или нескольким** группам
(битовая маска `warn_groups`); одна диагностика может входить сразу в несколько групп
(например, `unused-variable` — в `Wall`, `Wextra` и `Wunused`).

### 1.4. Семантика `-W` на CLI

| Синтаксис | Действие |
|---|---|
| `-W<name>` | включить диагностику на уровень по умолчанию |
| `-Wno-<name>` | выключить (ignore) |
| `-W<name>=<severity>` | задать уровень (`fatal/error/warning/remark/note/ignore`) |
| `-Wall` / `-Wextra` / `-Wpedantic` | включить все диагностики соответствующей группы |
| `-W<group>` (напр. `-Wunused`) | включить семантическую группу |
| `-Wno-<group>` (напр. `-Wno-Wall`) | выключить группу (регистронезависимо) |
| `-Werror` / `-Wno-error` | повысить предупреждения до ошибок / вернуть |
| `-Whelp` | справка по диагностикам (группы + принадлежность) |

Группа обрабатывается **только без `=value`** (иначе `-Wdeprecated=ignore` — это установка
severity диагностики, а не включение группы; `"deprecated"` — и группа, и диагностика).

> **Про feature-флаги `-W<flag>`** (`-Wlint`, `-Weffect`, `-Wtrust`, ...): включение анализаторов
> через `-W` — стиль **rustc**. В clang/gcc анализаторы включаются через `-fanalyzer`; здесь
> осознанно используется `-W`-форма для единообразия с диагностиками.

---

## 2. Опции драйвера

### 2.1. Категории (группировка справки `--help`)

`CliCategory { General, InputOutput, CompileModel, Linking, Toolchain, ProjectSpecific }` —
категория, по которой **группируется справка** драйверных бинарников.

> Категории `Diagnostics` **нет**: диагностики — отдельная таблица (diag/Options), справка по
> ним выводится отдельной командой `-Whelp` (см. §3).

### 2.2. Таблица опций

Каждый драйверный бинарник объявляет СВОЮ таблицу единообразно: ручной `enum XxxOptId` +
`vector<DriverOption>` + switch-связывание (для trust — `enum DriverOptId` + `buildTrustTable` +
`applyOption` в `cli.cpp`; для lsp/dap/playground — `lspTable`/`dapTable`/`playgroundTable`).
Формат строки: `{int(Id), cli_name, short, CliOpt::Kind, type, help, CliCategory}`.
- `kind` — `CliOpt::{Flag, Value, ValueList, OptionalValue}` (точная арность; `OptionalValue` — необязательное значение, ТОЛЬКО через `=`, следующий токен не потребляется, напр. `--gen-token` / `--gen-token=10`).
- `type` — плейсхолдер значения для справки (`<file>`, `<dir>`, `<lib>`, ...).

### 2.3. Общий парсер драйвера (header-only, символы в `namespace trust`)

`DriverOption` + `parseDriverArgs` + `driverHelp` (generic-часть инлайн в `cli.hpp`; обёртки
для trust — в `cli.cpp`):
- работает с произвольной таблицей `DriverOption` и колбэком `ApplyOptionFn(int id, const std::string& value)`
  (для `CliOpt::Flag` value пуст; потребитель различает флаг/значение по `id`/`kind`);
- позиционные аргументы собираются отдельно (не «доедаются» опциями-списками);
- `-W...` собираются в `diag_args` и применяются позже через единую точку `applyDiagnostics` → `Options::parse_argv`;
- `driverHelp` генерирует сгруппированную справку `--help`.

Используется `trust` (таблица `buildTrustTable()` из `cli.cpp`), `trust-lsp`, `trust-dap`,
`trust-playground` (свои таблицы). Подкоманда `server[=<port>]` у lsp/dap обрабатывается общим
helper `extractServerCommand` в `cli.hpp`.

---

## 3. Двухсправочная модель (явно)

В TrustLang **две раздельные справки**:

| Команда | Что выводит | Группировка | Источник |
|---|---|---|---|
| `--help` | верхнеуровневая справка, программно-специфичные опции драйвера | `CliCategory` | `driverHelp()` из таблицы `DriverOption` |
| `-Whelp` | единая справка по диагностикам (отдельной командой, чтобы не засорять `--help`) | `DiagGroup` + группы-агрегаты `WarnGroup` | `Options::printHelp()` из реестра |

Обе строятся из центральных таблиц/реестра, но показывают разные срезы. `--help` — для опций
конкретного приложения; `-Whelp` — единая для всех диагностик.

---

## 4. Принципы использования

1. **Единая регистрация — только у групп/агрегатов.** Группы (`WARN_GROUPS`, `DiagGroup`,
   `CliCategory`) определены центрально. Индивидуальные диагностики/флаги объявляются в
   **пер-компонентных наборах** (`TRUST_DIAG_SET`/`TRUST_FLAG_SET`, `semantic/diag.hpp` и т.п.)
   и регистрируются компонентом-владельцем через `Options::add<T>`/`add_flag<T>`.

2. **Пер-компонентный единственный источник + пер-компонентная регистрация.** Каждая компонента
   объявляет СВОИ диагностики/флаги (enum + имя + help + severity + группы + категория) в своём
   заголовке через `TRUST_DIAG_SET`/`TRUST_FLAG_SET` и регистрирует их через `Options::add<T>` /
   `Options::add_flag<T>` на static-init через `registerDiagnostics()`. `Context` при создании
   `Options` применяет их через `applyRegisteredDiagnostics()` (плюс базовые diag-владения:
   `Deprecated`, `ParseError` — общая, используется и transpiler, и types). Компоненты-примеры:
   `syntax` → `MacroRedefined`; `semantic` → `UnusedVariable`, `UnusedParameter`, `Embed`, `NoSigil`,
   `Format`, `WidenAny` + флаги `Lint/Effect/Trust/Extended/Symbols`; `transpiler` → флаги
   `Comments/Assert/Backtrace` (включая дефолты: assert/backtrace/comments включены). Обобщённые
   `report<T>`/`add<T>` используют ADL (`diagName`/`flagName`), поэтому diag остаётся листом.

3. **Две таблицы не смешиваются.** `diag` (диагностики анализа кода) и драйвер (опции CLI) —
   разные механизмы. Драйверные опции не попадают в реестр диагностик. Объединяет их только
   инфраструктура (парсер, справка, применение `-W`) и двухсправочная модель (§3).

4. **Добавление опции = одна строка в пер-компонентном наборе + одна регистрация.** Для
   диагностики: строка `M(Enum, "cli", Sev, "help", WGmask)` в `TRUST_DIAG_SET` компоненты + одна
   регистрация опции — парсер/справка/группировка генерируются централизованно. Для драйвера:
   строка в таблице драйвера (`enum DriverOptId` + `buildTrustTable` в `cli.cpp`) + строка-связывание
   в едином `applyOption`. Не надо писать отдельные switch/if для имён.

5. **Конвертации централизованы.** Имена групп/severity генерируются из X-macro и таблиц
   (`SEVERITIES` → enum `Severity`, `kSeverityNames`, `kSeverityToLsp`, `severityName`/
   `severityFromName`; `WARN_GROUPS` → `WarnGroup`, `warnGroupName`/`warnGroupCli`), а не
   дублируются руками.

6. **Справка генерируется централизованно из реестра/таблиц:** `--help` — опции драйвера по
   `CliCategory`; `-Whelp` — диагностики по `DiagGroup` (подсказки/группы/severity — из
   пер-компонентных деклараций, сохранённых в реестре) + группы-агрегаты.

7. **Строковый ключ диагностик (осознанный трейд-офф).** В отличие от clang/LLVM (единый
   каталог `Diagnostics.td` с числовыми ID) диагностики Trust ключуются **строковым cli-именем**
   в пер-компонентных списках (`TRUST_DIAG_SET`/`TRUST_FLAG_SET`). Это осознанное решение ради
   того, чтобы `diag` оставался листом (не включал заголовки компонентов). Следствия: уникального
   числового каталога диагностик нет; коллизия cli-имени между компонентами выявляется только в
   рантайм — `Options::add`/`add_flag` бросает `invalid_argument` на дубликате имени.

---

## 5. Разграничение флагов справки и диагностик (важно)

Не путать три разных флага/механизма:

| Символ | Слой | Назначение | Где выставляется | Справка |
|---|---|---|---|---|
| `--help` | драйвер | справка по опциям бинарника | `PipelineOpts::help_requested` (в `applyOption`/`apply`-колбэке) | `driverHelp()` |
| `-Whelp` | диагностики | справка по диагностикам анализа кода | `Options::help_requested_` (в `Options::parse_argv`) | `Options::printHelp()` |
| `-Werror`/`-Wno-error` | диагностики | глобальный переключатель warning→error | `Options::m_werror` | — |

**`--help` и `-Whelp` — разные флаги в разных слоях, никогда не смешиваются.** У них даже
разные механизмы справки (таблица драйвера vs реестр диагностик).

### Два флага для `-Whelp` (ранний сигнал vs собственно флаг)

`-Whelp` обрабатывается на двух уровнях:

- `ParseResult::diag_help_requested` — **ранний драйверный сигнал**: выставляется в
  `parseDriverArgs` (детект литерала `-Whelp`). Нужен `Pipeline::parseArgs`/`trust.cpp`, чтобы
  пропустить проверку обязательности входного файла **до** создания `Context`/`Options`.
- `Options::helpRequested()` — **собственно флаг справки диагностик**: выставляется в
  `Options::parse_argv` (при обработке `-Whelp`). Печать справки идёт через него
  (`trust.cpp`: `if (ctx.opts().helpRequested()) ctx.opts().printHelp(...)`).

Это разделение ответственности драйвер/диагностики, а не дублирование: драйвер не знает про
`Options`, а диагностический слой не занимается валидацией входного файла. Не удалять ни один
из них, не сводить в один.

---

## 6. Как добавить опцию (практическое руководство)

Опции объявляются в **одной из двух областей** (см. §1-§2) — никогда не в обеих сразу.

### 6.1 Severity-диагностика (`-W<name>[=status]`, управляется `Options`)

Добавить **одну строку** в пер-компонентный набор и **одну регистрацию**:

1. В заголовке компоненты (напр. `semantic/diag.hpp`) в `TRUST_DIAG_SET`:
   `M(EnumName, "cli-name", Sev, "Help text", WG_mask)` — `Sev` из `severity.hpp`,
   `WG_mask` — битовая маска `WarnGroup` (`WG_None`, `WG_Wall | WG_Wextra | ...`).
   Механизм (`TRUST_DIAG_SET`/`diag_set.hpp`) сам генерирует enum + ADL-доступы.
2. Регистрация компонентом-владельцем на static-init через `registerDiagnostics`:
   `opts.add(Component::DiagId::EnumName);`
3. Готово: `-Wcli-name`, `-Wno-cli-name`, `-Wcli-name=status`, группы `-Wall`/`-Wextra`/...,
   справка `-Whelp` — всё генерируется автоматически.

### 6.2 Feature-флаг (`-Wname` / `-Wno-name`, булев, группами не управляется)

1. В заголовке компоненты в `TRUST_FLAG_SET`:
   `M(EnumName, "cli-name", "Help text", DiagGroup::Analysis|Codegen)`.
2. Регистрация: `opts.add_flag(Component::FlagKind::EnumName);`
3. Справка `-Whelp` и включение/выключение генерируются автоматически.

### 6.3 Опция драйвера бинарника (`--name`, `-x`)

Объявляется в таблице драйвера соответствующего бинарника:

1. Добавить id в enum (`trust::DriverOptId` в `cli.cpp` для trust; `LspOptId`/`DapOptId`/
   `PlaygroundOptId` для остальных).
2. Добавить строку в таблицу (`buildTrustTable` для trust; `lspTable`/`dapTable`/
   `playgroundTable` для остальных) — формат
   `{int(Id), "cli-name", "short", CliOpt::Kind, "type", "help", CliCategory::Cat}`.
3. Добавить `case` в `switch`-связывание (`applyOption` для trust; `apply`-колбэк для lsp/dap/playground).

Парсер (`parseDriverArgs`), grouped-справка `--help` (`driverHelp`) и категории общие —
переписывать их не нужно. Драйверные опции **не** попадают в реестр диагностик (§4.3).
