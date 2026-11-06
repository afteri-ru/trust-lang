# Toolchain: C++-сборка

Слой сборки сгенерированного C++: build.conf/Makefile, компиляция и линковка, кеш `--run`,
поиск рантайм-библиотек и план линковки. Не зависит от фронтенда (`semantic`/`transpiler`/`ir`).

- `BuildRequest` — юнит-пути, `RuntimeHeaders`, link-libs, entry (параметры — `PipelineOpts` из `driver`).
- `prepareBuildInputs` → `BuildInputs` (заголовки + `LinkPlan`) вычисляются ОДИН раз.
- `composeBuildConf` — чистая функция; `writeBuildFiles`/`compileAndLink` принимают `BuildRequest`.
- Кеш `--run`: `runCacheValid` (чтение ELF-секции `.debug_trust_hash`; формат записи — `utils/build_record.hpp`, строка опций — `driver/codegenArgsRecord`).

Зоны: `build/` (Makefile/compile), `cache/` (MD5 + запись кеша), `runtime/` (поиск библиотек).
