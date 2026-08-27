---
title: SMT-решатель и верификация (примеры)
weight: 30
tags: [diagnostics, verification]
---

Примеры использования SMT-решателя (Z3) для формальной верификации trust-контрактов.
Подробное описание опций `-Wsolver` и `--solver-mode` - в разделе
[Trust-условия и верификация](trust-conditions/).

## Контракты функций

Предусловие `trust_pre`, постусловие `trust_post`, утверждение `trust_assert`:

```trust
func(x:Int32):Int32
    trust_pre( x > 0 )          # предусловие
    trust_post( func > x )      # постусловие; func - возвращаемое значение
:= { x + 1 };
```

## Циклы и инварианты

Цикл верифицируется индукцией по явному инварианту:

```trust
@{ invariant: i >= 0 @};        # инвариант перед циклом
@while ( i < n ) { ... };
```

Ограниченное разворачивание цикла - глобально флагом `-fsolver-loop-unroll` или
пер-циклово термином `z3_unroll(N)` внутри контракта инварианта:

```trust
@{ invariant: z3_unroll(3) @};  # развернуть цикл на 3 итерации
@while ( cond ) { ... };
```

## Кванторы

`z3_forall(i, P)` / `z3_exists(i, P)` - кванторы по связанной переменной `i`
(объявленной ранее); `z3_old(x)` - начальное значение, `z3_result` - результат:

```trust
trust_post( z3_forall(i, 0 <= i and i < count) => arr[i] > 0 )
```

## Запуск

```sh
trust --solver-mode=export file.src      # сгенерировать SMT-LIB 2 файл (.smt2)
trust --solver-mode=calculate file.src   # выполнить Z3 и сообщить контрпример (WITH_SOLVER=ON)
```

## Ссылки

- [Trust-условия и верификация](trust-conditions/)
- [Доказательство корректности](../syntax/proof/)
