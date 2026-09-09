---
title: Защита стека от переполнения (stack check)
tags: [safety, stack-check, recursion]
description: >
    Превращение переполнения стека в перехватываемое исключение вместо аварийного завершения
weight: 60
---

В *TrustLang* встроена [защита стека от переполнения](https://github.com/afteri-ru/stack-check). Перед вызовом защищаемой функции проверяется свободное место на стеке потока, и если его недостаточно — вместо аварийного завершения всего приложения (`segmentation fault`) создаётся **программное исключение `stack_overflow`**, которое можно перехватить и обработать внутри приложения, после чего продолжить выполнение программы.

Здесь будет подробное описание реализации и способов использования.

А сейчас можно посмотреть примеры использования: [рекурсивное вычисление чисел Фибоначчи](/ru/playground/?file=fibonacci) и [рекурсивная проверка чисел на простоту](/ru/playground/?file=prime)



<!--

## Атрибут `@[stack_check@]`

Защищаемые функции помечаются атрибутом `@[stack_check@]`:

- `@[stack_check@]` — перед каждым вызовом функции проверяется максимальный размер её стека из
  секции `.stack_sizes` (`check_stack_limit`, требуется сборка с `-fstack-size-section`);
- `@[stack_check(N)@]` — проверяется, что свободно не меньше `N` байт плюс резерв
  (`check_overflow(N)`).

Пример рекурсии, защищённой атрибутом (переполнение ловится, а не роняет программу):

```trust
@[ stack_check @]
%fib(n:BigInteger):Tuple(sum:BigInteger, fib:BigInteger) := {
    if (n < 3) {
        $one:BigInteger := 1;
        return :Tuple($one, $one);
    };
    p := fib(n - 1);
    return (p.0 + p.1, p.sum,):Tuple;
};

@main(argv^:Dict, args^:Dict) := {
    try_error {
        fib(1000000);
    } catch(_) {
        @print('STACK OVERFLOW caught\\n');
    };
    return 0;
};
```

## Режимы контроля (`--stack-check=<mode>`)

Монотонная шкала режимов (`trust --help`):

- `off` — контроль выключен (атрибуты — no-op, рекурсия не анализируется);
- `explicit` (по умолчанию) — проверки только явно размеченных функций и явных вызовов;
- `recursion` — как `explicit` плюс диагностика незащищённых рекурсивных функций
  (`-Wstack-check-infer`);
- `auto` — как `recursion` плюс авто-маркировка рекурсивных функций.

Защитный резерв задаётся `--stack-check-reserve=<байты>` (по умолчанию — рантайм-дефолт `8192`).
Работающие примеры — [песочница fibonacci](/ru/playground/?file=fibonacci); готовые тесты контроля
стека — `test/lit/.../e2e/stack/*`.

## Рантайм-функции контроля

Для явного управления доступны нативные функции `%trust_stack_check(N)`,
`%trust_stack_check_set_reserve(...)`, `%trust_stack_check_get_reserve()`,
`%trust_stack_check_get_limit()` и `%trust_stack_check_set_limit(...)`. Полные сигнатуры — в
справке `trust --help` / `-Whelp`.
-->