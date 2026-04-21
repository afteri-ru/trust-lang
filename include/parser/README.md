# Parser Module

Токенайзер и парсер исходного кода программы с построением AST.

## Архитектура

Последовательность обработки разделена на независимые этапы:

```
tokens.def → gen_tokens.py ┬→ token.gen.*    (C++ X-macros для enum)
                             └→ parser.gen.y  (Bison %token / %nterm)

Входной текст → Lexer (Flex) → LexemSequence → MMProc → ASTNodeSequence
              → Validator → ParserAST (Bison) → ASTNodeSequence (финальный AST)
```

### Этапы обработки

1. **Генерация токенов** — `tokens.def` является единым источником токенов. Скрипт `gen_tokens.py` генерирует заголовочные файлы для Flex, Bison и C++ enum.

2. **Лексер (Flex)** — токенизирует входной текст, создавая `LexemSequence`.
   - Каждая лексема `Lexeme` содержит: `std::string_view` (текст), `ParserToken::Kind` (тип), `SourceLoc` (позиция).
   - Лексер не содержит токенов по умолчанию — все токены определены в `tokens.def`.
   - Фатальные ошибки (незакрытые строки, комментарии) бросают `ParserError`.

3. **MMProc (Macros & Module Processor)** — *[планируемый компонент]*
   - Раскрывает макросы и обрабатывает макро-аргументы.
   - Конкатенирует строковые литералы одного типа.
   - Загружает внешние модули.
   - Возвращает `ASTNodeSequence`, где каждый узел содержит `TokenInfo`.

4. **ASTNodeSequenceValidator** — *[планируемый компонент]*
   - Проверяет корректность `ASTNodeSequence` перед передачей в парсер.

5. **Парсер (Bison)** — грамматический разбор на основе `parser.y`.
   - Возвращает `ASTNodeSequence` с полной структурой AST.
   - Грамматика не содержит неоднозначностей (LALR(1)).
   - Ошибки разбора создают не-фатальные диагностические сообщения с указанием позиции.

## Структуры данных

### Lexeme

Лексема — результат работы лексера. Хранит сырые данные из входного потока:

```cpp
struct Lexeme : std::string_view {
    ParserToken::Kind kind;   // Тип токена
    SourceLoc pos;            // Позиция в исходном файле
};
```

### TokenInfo

Информация о токене для AST-узлов. Заполняется после обработки MMProc или парсером. Содержит `SourceRange`, вычисленный из начальной и конечной позиции исходных `Lexeme`.

> **TODO:** Текущая реализация использует `SourceLoc` (единичная позиция). Требуется исправить на `SourceRange`.

```cpp
struct TokenInfo {
    ParserToken::Kind kind;   // Тип токена
    SourceRange range;        // Диапазон в исходном файле
    std::string text;         // Оригинальный текст
};
```

### ASTNode

Узел AST содержит опциональный `TokenInfo`:
- У сырых лексем из `Lexeme` — `TokenInfo` отсутствует.
- У узлов, прошедших MMProc или парсер — `TokenInfo` должен быть заполнен.

## Формат tokens.def

Полное описание формата, флагов и конвенций именования: [TOKEN_FORMAT.md](TOKEN_FORMAT.md)

## Генерация

Токены определяются в `tokens.def` и генерируются скриптом `scripts/gen_tokens.py`:

```
tokens.def → gen_tokens.py ┬→ token.gen.hash     (hash для проверки синхронизации)
                             ├→ token.gen.all     (все токены: TK(name, val, flags))
                             ├→ token.gen.name_all (TCASE для name() switch)
                             ├→ token.gen.flags_all (FCASE для flags() switch)
                             ├→ token.gen.lookup_all (LOOKUP для from_name())
                             ├→ token.gen.def     (legacy формат)
                             └→ parser.gen.y      (Bison %token / %nterm из parser.y)
```

Скрипт выполняет:
- Валидацию `tokens.def` (уникальность значений, END=0, один Root токен).
- Кросс-валидацию с `lexer.l` (все TK() в лексере должны иметь FLEX_LEXEME).
- Привязку значений через `tokens.lock`.
- Генерацию `parser.gen.y` путём подстановки `@@TOKENS@@` и `@@HEADER@@` в `parser.y`.

## Lexer States

Лексер использует start-состояния для обработки многострочных конструкций:

| Состояние | Описание |
|-----------|----------|
| `state_COMMENT` | Вложенные комментарии `/* ... */` |
| `state_DOC_BEFORE` | Документирующие комментарии `/** ... */` |
| `state_EMBED` | Встроенный код `{% ... %}` |
| `state_MACRO_STR` | Макро-строки `@@@ ... @@@` |
| `state_STRWIDE` | Строковые литералы `" ... "` |
| `state_STRCHAR` | Символьные литералы `' ... '` |
| `state_STRWIDE_RAW` | Сырые строки `r" ... "` |
| `state_STRCHAR_RAW` | Сырые символы `r' ... '` |
| `state_EVAL` | Eval-строки `` ` ... ` `` |
| `state_ATTRIBUTE` | Атрибуты `@[ ... ]@` |