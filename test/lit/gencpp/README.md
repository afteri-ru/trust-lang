# Упрощённый AST-формат для тестирования

Текстовый формат представления AST, используемый исключительно для тестирования транспайлера. Предназначен только для парсера тестового формата.

## Назначение

Формат описывает структуру AST в текстовом виде с использованием отступов для обозложения вложенности узлов. Парсер (`ast_format_parser`) преобразует текстовое представление во внутреннюю AST-структуру (`ParsedNode`), которая затем строится в полноценное AST (`Program`).

## Структура компонента

| Файл | Описание |
|------|----------|
| `ast_format_parser.hpp` | Заголовок: `ParsedNode`, функции парсинга и построения AST |
| `ast_format_parser.cpp` | Реализация: парсер текстового формата, builder функций |

## Синтаксис

Каждая строка — один узел:
```
<Отступ><NodeName> attr1=value1 attr2=value2
```

- **Отступ** (пробелы/табы) задаёт вложенность
- **NodeName** — имя узла
- **attr=value** — пары "ключ=значение" через пробел
- Строки с `#` игнорируются

## Типы узлов

### Decl (объявления)

| Узел | Атрибуты | Дети |
|------|----------|------|
| `FuncDecl` | `name=X`, `ret=type` | `ParamDecl*`, `BlockStmt` |
| `ParamDecl` | `name=X`, `type=type` | — |
| `VarDecl` | `name=X`, `type=type` (опц.) | `Expr` |
| `EnumDecl` | `name=X` | `EnumMember*` |
| `EnumMember` | `name=X`, `value=N` (опц.) | — |
| `StructDecl` | `name=X` | `StructField*` |
| `StructField` | `name=X`, `type=T` | — |

### Stmt (операторы)

| Узел | Атрибуты | Дети |
|------|----------|------|
| `BlockStmt` | — | `Stmt*`, `Decl*` |
| `AssignmentStmt` | `target=X` | `Expr` |
| `ReturnStmt` | — | `Expr` (опц.) |
| `ExprStmt` | — | `Expr` |
| `IfStmt` | — | `Expr`, `ThenBlock`, `ElseBlock` (опц.) |
| `WhileStmt` | `label=X` (опц.) | `Expr`, `BlockStmt`, `WhileElseBlock` (опц.) |
| `DoWhileStmt` | `label=X` (опц.) | `Stmt*`, `Decl*`, `Expr` (условие) |
| `BreakStmt` | — | — |
| `ContinueStmt` | — | — |
| `TryCatchStmt` | — | тело, `CatchBlock` |

### Expr (выражения)

| Узел | Атрибуты | Дети |
|------|----------|------|
| `IntLiteral` | `value=N` | — |
| `StringLiteral` | `value="X"` | — |
| `VarRef` | `name=X` | — |
| `BinaryOp` | `op=X` | 2 выражения |
| `CallExpr` | `name=X` | `Expr*` |
| `EnumLiteral` | `enum=X`, `member=X` | — |
| `MemberAccess` | `field=X` | `Expr` (объект) |
| `ArrayAccess` | — | массив, индекс |
| `ArrayInit` | `type=T` (опц.) | `Expr*` |
| `CastExpr` | `type=X` | `Expr` |
| `EmbedExpr` | `value="X"` | — |

## Примеры

**Переменная:**
```
VarDecl name=x type=int
  IntLiteral value=42
```

**Функция:**
```
FuncDecl name=main ret=int
  BlockStmt
    VarDecl name=x
      IntLiteral value=10
    ReturnStmt
      IntLiteral value=0
```

**Условный оператор:**
```
IfStmt
  BinaryOp op=>
    VarRef name=x
    IntLiteral value=0
  ThenBlock
    ExprStmt
      CallExpr name=print
        StringLiteral value="positive"
```

## Директивы тестирования

- `# RUN:` — команда запуска (`%trans` → путь к `gencpp_from_ast`)
- `# CHECK:` — ожидаемый фрагмент вывода