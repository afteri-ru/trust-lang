#!/usr/bin/env python3
"""Structure budget report (warn-only).

Usage: python3 test/tools/structure_budget.py [--max-kb 40] [--max-func-lines 200]

Reports source files above the size budget and functions above the length budget.
Never fails the build: exits 0 even when violations are found (informational gate).

`--self-test` проверяет сам детектор длин функций на встроенных фикстурах (запускается
вручную в DoD этапа; в CTest не регистрируется - в CMake нет конфигурации python3).
"""
from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

SOURCE_ROOTS = ("src", "include")
SOURCE_SUFFIXES = (".cpp", ".hpp", ".h")
SKIP_DIRS = {"_build", ".git", ".tasklog", ".trust", ".cache", "node_modules", "docs"}


def iter_sources(root: Path):
    for base in SOURCE_ROOTS:
        base_path = root / base
        if not base_path.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base_path):
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
            for name in filenames:
                if name.endswith(SOURCE_SUFFIXES):
                    yield Path(dirpath) / name


def strip_code(line: str, in_block: bool) -> tuple[str, bool]:
    out = []
    i = 0
    quote = None
    while i < len(line):
        if in_block:
            end = line.find("*/", i)
            if end < 0:
                return "".join(out), True
            i = end + 2
            in_block = False
            continue
        ch = line[i]
        if quote:
            if ch == "\\":
                i += 2
                continue
            if ch == quote:
                quote = None
            i += 1
            continue
        if line.startswith("//", i):
            break
        if line.startswith("/*", i):
            in_block = True
            i += 2
            continue
        if ch in "\"'":
            quote = ch
            i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out), in_block


# Пределы, внутри которых `{`/`(` открывают кандидата в функции: тела class/struct/enum,
# а также namespace-обёртки не считаются телом функции.
_NON_FUNCTION_PREFIXES = ("namespace", "class ", "struct ", "enum", "union ", "extern ", "template")


def function_lengths(path: Path):
    """Длины определений функций верхнего уровня (включая функции внутри namespace).

    Namespace-скобки образуют БАЗОВЫЙ уровень (`base`), от которого отсчитывается тело
    функции: без этого файл, целиком обёрнутый в `namespace trust { ... }`, не даёт ни одной
    функции (ложный пропуск длинных диспетчеров). Тела class/struct/enum кандидатами не
    являются - их методы не репортятся (известное ограничение отчёта).
    """
    raw = path.read_text(encoding="utf-8", errors="ignore").splitlines()
    depth = 0  # полная глубина скобок
    base = 0  # глубина namespace-обёрток
    start = None
    in_block = False
    for idx, line in enumerate(raw):
        code, in_block = strip_code(line, in_block)
        stripped = code.lstrip()
        if stripped.startswith("namespace"):
            delta = code.count("{") - code.count("}")
            base += delta
            depth += delta
            continue
        if depth == base and start is None and "{" in code and "(" in code and not stripped.startswith(_NON_FUNCTION_PREFIXES):
            start = idx
        depth += code.count("{") - code.count("}")
        if start is not None and depth <= base and idx > start:
            yield idx - start + 1, start + 1, raw[start].strip()
            start = None
            depth = base
        elif depth < base:
            base = depth


def self_test() -> int:
    """Проверка детектора длин функций на встроенных фикстурах."""
    import tempfile

    def spans(path: Path):
        return [(length, line) for length, line, _ in function_lengths(path)]

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "ns.cpp").write_text("namespace t {\nint f() {\n    return 0;\n}\n}\n", encoding="utf-8")

        long_body = ["namespace t {", "int g(int x) {"]
        for i in range(120):
            long_body.append(f"    if (x == {i}) {{ auto l = []() {{ return {i}; }}; x += l(); }}")
        long_body += ["    return x;", "}", "}"]
        (root / "long.cpp").write_text("\n".join(long_body) + "\n", encoding="utf-8")

        (root / "literals.cpp").write_text(
            "namespace t {\nint h() {\n    std::array<int, 3> a{};\n    const char* s = \"{}\";\n"
            "    auto l = [&]() { return a[0]; };\n    return l() + s[0];\n}\n}\n",
            encoding="utf-8",
        )
        (root / "member.cpp").write_text("namespace t {\nstruct S {\n    int m() { return 1; }\n};\n}\n", encoding="utf-8")

        checks = [
            ("ns.cpp: функция внутри namespace найдена", spans(root / "ns.cpp") == [(3, 2)]),
            ("long.cpp: длинная функция с лямбдами найдена", spans(root / "long.cpp") == [(123, 2)]),
            ("literals.cpp: скобки в литералах не ломают границы", spans(root / "literals.cpp") == [(6, 2)]),
            ("member.cpp: метод класса не репортится (ограничение)", spans(root / "member.cpp") == []),
        ]
    failed = [name for name, ok in checks if not ok]
    for name, ok in checks:
        print(f"  [{'ok' if ok else 'FAIL'}] {name}")
    return 0 if not failed else 1


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--max-kb", type=int, default=40)
    parser.add_argument("--max-func-lines", type=int, default=200)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    root = Path(__file__).resolve().parents[2]

    big_files = []
    long_funcs = []
    for path in iter_sources(root):
        size_kb = path.stat().st_size / 1024.0
        if size_kb > args.max_kb:
            big_files.append((size_kb, path.relative_to(root)))
        for length, line_no, signature in function_lengths(path):
            if length > args.max_func_lines:
                long_funcs.append((length, path.relative_to(root), line_no, signature))

    print(f"Files > {args.max_kb} KB: {len(big_files)}")
    for size_kb, rel in sorted(big_files, reverse=True):
        print(f"  {size_kb:7.1f} KB  {rel}")
    print(f"Functions > {args.max_func_lines} lines: {len(long_funcs)}")
    for length, rel, line_no, signature in sorted(long_funcs, reverse=True):
        print(f"  {length:5d} lines  {rel}:{line_no}  {signature[:90]}")
    print("(warn-only: this script never fails the build)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
