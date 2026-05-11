# trust — C++20 Diagnostic & Options Library

Библиотека для управления диагностическими сообщениями и опциями компилятора/анализатора.

## Компоненты

- **Context** — фасад, объединяющий DiagnosticEngine, Options и Source Manager. Детальное описание API — в `include/diag/ARCH.md`.
- **DiagnosticEngine** — вывод диагностики с форматированием, подсчётом ошибок/предупреждений и визуальным подчёркиванием диапазонов.
- **Options** — система именованных опций, определяемых через X-макросы. Поддерживает парсинг CLI аргументов и стековый push/pop для временных изменений.
- **Source Manager** — хранение исходных файлов, конвертация offset ↔ line:column с LRU-кешем.

## Сборка

```bash
mkdir build && cd build
cmake ..
cmake --build .
./test/unit