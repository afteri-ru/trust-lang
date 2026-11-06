# MEMORY.md

> scope: include/utils/io (+ src/utils)
> role: persistent-memory
> last_reviewed: 2026-11-10
> review_period: 30

## Architecture

Ввод-вывод и транспорт: `io.hpp` (`outs()`/`errs()` и общие потоки), `file_io.hpp` (чтение/запись
файлов), `socket_io.hpp`, `transport.hpp` (фрейминг пакетов), `uri.hpp` (разбор URI),
`zstd_compress.hpp` (сжатие).

## Facts and invariants

- **⚠ Транспортный фрейминг — ОДИН reader:** `transport::readContentLengthPacket(fd)` обслуживает и
  `StdioTransport`, и `TcpTransport`; `utils::writeAllToFd` — единственная запись в fd. Второй
  реализации разбора фрейма быть не должно.
- **⚠ Формат zstd-блока:** `[orig_size LE4][dict_size LE4][dict][data][md5 LE8]` с обученным на
  буфер словарём; старый zlib-формат не читается — менять раскладку без версии нельзя.

## Decisions

## Relations

- `debug`/`lsp`/`playground` (`transport`), `sourcemap` (`zstd_compress`), `utils/utils.hpp` (umbrella).
