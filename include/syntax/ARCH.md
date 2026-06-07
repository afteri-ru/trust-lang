# Компонент syntax

Синтаксический компонент преобразует исходный текст программы в синтаксическое дерево (`Term`) — универсальный узел, из которого компонент `pipeline` строит AST.

## Поток данных

```
Исходный текст
    │
    ▼
Scanner (Flex, lexer.l)      — лексический анализ, выдаёт термины (TermPtr)
    │
    ▼
Macro (macro.h/cpp)          — раскрытие макросов и обработка прагм
    │                          (встроен в Parser::GetNextToken)
    ▼
Parser (Bison, parser.y)     — синтаксический анализ, построение Term-дерева
    │
    ▼
Term → term_to_ast (pipeline) → AstNodeBase (ast)
```

Раскрытие макросов не является отдельной стадией: лексер подаёт термины в буфер `Parser::m_macro_analisys_buff`, после чего `Macro` раскрывает макросы до передачи термина в грамматику Bison.

## Основные сущности

- **`Term`** (`term.h`) — универсальный узел синтаксического дерева
- **`TermID`** (`term_types.h`) — перечисление типов узлов (имя, число, оператор, блок, макро-токены и т.д.).
- **`Scanner`** (`lexer.h`) — Flex-лексер, наследник `yyFlexLexer`; читает исходный
  текст через `Context`/`SourceMapper`, выдаёт термины.
- **`Macro`** (`macro.h`) — макропроцессор: определение/раскрытие/удаление макросов,   стек скоупов по модулям, предопределённые макросы. 
- **`Parser`** (`parser.h`, `parser.y`) — Bison-парсер: связывает лексер и макропроцессор, строит дерево `Term`, обрабатывает прагмы.

## Создание терминов (`Term::Create`)

`Term::Create` имеет две перегрузки:

```cpp
// Копирует текст — ручное создание (безопасно для локальных std::string и литералов).
// lex_type — последний с дефолтом, для ручных вызовов не нужен.
static TermPtr Create(TermID id, std::string text,
                      trust::MapperRange mapperRange = {},
                      parser::token_type lex_type = parser::token_type::END);

// View из (text, len) = std::string_view(text, len) — НЕ копирует.
// Данные должны пережить Term (лексер: исходный текст SourceMapper).
// lex_type — второй аргумент (сразу после id), только для lexer.l / parser.y.
static TermPtr Create(TermID id, parser::token_type lex_type,
                      const char* text, size_t len,
                      trust::MapperRange mapperRange = {});
```

- Перегрузка с `std::string` копирует текст — безопасна для локальных строк и литералов.
- Перегрузка `(id, lex_type, const char*, size_t len, range)` строит `std::string_view(text, len)` без копирования — используется в лексере (`lexer.l`), где текст токена живёт в `SourceMapper`.
- Для намеренного обрезания в `parser.y` (копия первых N символов) используется явная копия: `Create(id, std::string(data(), N), range, token)`.

