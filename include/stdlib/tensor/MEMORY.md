# MEMORY.md
> scope: include/stdlib/tensor
> role: persistent-memory
> last_reviewed: 2026-10-20
> review_period: 30
> max_size: 4200

## Architecture

Провайдер-пакет `tensor.trust` (toolchain resource, `share/trust/tensor.trust`): реализация
предобъявленного Core-типа `Tensor` (`tensor.src`: `Tensor = %trust::Tensor { };`) + контракт
линковки с toolchain-плагином `tensor_cpu`. Публичный заголовок `trust/tensor.hpp`
(`trust::Tensor<Elem>`, `TensorHandle`) — self-contained, БЕЗ torch-заголовков; C ABI — 
`trust/tensor_backend.hpp`; оба доставляются пакетом `stdlib.trust` как `include/trust/*.hpp`.

`trust.toml`: `[[artifact]] provides=["tensor"] kind="toolchain" libs=["tensor_cpu"]`
`capabilities=["with-torch"]`; `[[link.headers]] trust/tensor.hpp -> tensor`.

## Facts and invariants

- `@[link("tensor")]` — логическое имя; артефакт превращает его в `-ltensor_cpu`.
- Без `WITH_TORCH` (`TRUST_HAS_TORCH` не определён) использование пакета даёт диагностику
  «requires capability 'with-torch'» ДО линковки; это не linker error.
- Приложение линкует только плагин `tensor_cpu`; libtorch — транзитивная зависимость плагина и в
  публичный интерфейс не попадает.
- `Tensor` — предобъявленный тип: без провайдера кодогенерация даёт «implementation of built-in type
  'Tensor' is not defined; load provider 'tensor'».
- Сырой libtorch API в этот пакет не входит (для него нужен отдельный opt-in `torch` provider).

## Decisions

- Тяжёлые/ABI-нестабильные библиотеки (libtorch) не пакуются в `.trust`; пакет объявляет
  toolchain-артефакт, а реализация инкапсулируется в плагине.

## Relations

- Модель артефактов — `include/package/MEMORY.md`; резолвер — `include/pipeline/MEMORY.md`.
- Self-contained заголовок — `include/runtime/MEMORY.md`.
