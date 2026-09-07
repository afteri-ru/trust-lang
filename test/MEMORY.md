# MEMORY.md

> scope: test
> role: persistent-memory
> last_reviewed: 2026-08-19
> review_period: 30
> max_size: 4915

## Facts and invariants

- Временные файлы создаются ТОЛЬКО в каталоге сборки `_build` и остаются после тестов (не удаляются). Артефакты собираются в `TEST_DATA_DIR`.
- TrustLang игнорирует строки с `#` — метки `# CHECK:` можно размещать в том же файле.

### LIT-тесты: формат RUN строки

```
# RUN: %trust -q [options] %s > %t.out && %FileCheck %s <%t.out
```

- Вывод всегда во временный файл (`> %t.out`), FileCheck читает из него (`<%t.out`).
- Без `2>/dev/null` (вместо этого `-q`), без пайпов, без абсолютных путей (только `%FileCheck`, `%trust`).
- Данные для Trust — непосредственно в файле (без `echo`/`sed`).
