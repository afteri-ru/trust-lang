#!/bin/bash
# gh-pages_build.sh - сборка статического сайта (docs/) в docs/gh-pages.
# Контракт: запуск ТОЛЬКО из каталога docs/ (проверяется ИМЯ текущего каталога).
# Пути - только явные литералы, без переменных-путей и поисков.
#
# trust-lsp обязателен - фрагменты генерируются из актуальных примеров на момент
# сборки. Если бинарник отсутствует, скрипт завершается ошибкой с требованием
# собрать проект (устаревшие закоммиченные docs/fragments/*.html не используются).
# Monaco при отсутствии скачивается (нужна сеть).
#
# Переменные окружения:
#   HUGO  - команда hugo (по умолчанию hugo)
#   NO_MONACO=1 - не скачивать Monaco (если уже завендорен)
set -uo pipefail

# Проверка места запуска по имени текущего каталога.
if [ "$(basename "$PWD")" != "docs" ]; then
    echo "error: run this script ONLY from the docs/ directory" >&2
    echo "       usage: cd docs && ./gh-pages_build.sh" >&2
    exit 1
fi

# trust-lsp обязателен: фрагменты генерируются только из актуальных примеров
# на момент сборки. Если бинарник отсутствует - ошибка с требованием собрать
# проект (не переиспользуем потенциально устаревшие закоммиченные фрагменты).
if [ ! -x ../_build/trust-lsp ]; then
    echo "error: trust-lsp binary not found at ../_build/trust-lsp" >&2
    echo "       build the project first: cmake --build ../_build --target trust-lsp" >&2
    exit 1
fi

# -- 1. Self-hosted Monaco (скачиваем, если отсутствует) --
echo "[1/4] ensure Monaco"
if [ "${NO_MONACO:-0}" != "1" ] && [ ! -f static/monaco/vs/loader.js ]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p static/monaco
    curl -fsSL "https://registry.npmjs.org/monaco-editor/-/monaco-editor-0.52.2.tgz" -o "$tmp/monaco.tgz" \
        || { echo "error: failed to download monaco-editor" >&2; exit 1; }
    tar -xzf "$tmp/monaco.tgz" -C "$tmp" package/min/vs \
        || { echo "error: failed to extract monaco-editor" >&2; exit 1; }
    rm -rf static/monaco/vs
    mv "$tmp/package/min/vs" static/monaco/vs
    rm -rf "$tmp/package"
    echo "  Monaco downloaded to static/monaco/vs"
else
    echo "  Monaco already present at static/monaco/vs"
fi

# -- 2. Генерация HTML-фрагмента playground --
# Единый фрагмент playground.html (языконезависимый виджет) для страниц /ru/playground/ и
# /en/playground/ (обе встраивают его через shortcode `{{< playground >}}`). Примеры берутся
# напрямую из корневого ../examples (trust-lsp встраивает их в фрагмент при генерации -
# отдельные копии в content/ не нужны). Фрагмент лежит вне content/, чтобы hugo не
# трактовал его как страницу и не парсил JS как Go-шаблон.
echo "[2/4] generate playground fragment"
mkdir -p fragments
frag="fragments/playground.html"
dir="../examples"
# trust-lsp может вернуть ненулевой код при ошибке транспиляции примера; это не должно
# ронять сборку сайта.
../_build/trust-lsp --html "$dir/hello.src" --examples-dir "$dir" \
    --server-url "https://playground.trust-lang.net/run" --monaco-url "/monaco/vs" > "$frag" \
    || echo "  warning: trust-lsp --html exit=$?"
echo "  generated $frag"

# -- 3. Очистка предыдущей версии сайта + метаданные --
# .git - скрытый, glob `gh-pages/*` его не трогает; CNAME пересоздаём из content/CNAME.
echo "[3/4] clean output dir"
if [ -d gh-pages ]; then
    rm -rf gh-pages/*
fi
mkdir -p gh-pages
cp -f content/CNAME gh-pages/CNAME 2>/dev/null || true

# -- 4. Сборка hugo --
# Примечание: --minify не используем - встроенный JS-минификатор hugo не
# разбирает inline-JS playground (современный синтаксис) и роняет сборку.
echo "[4/4] hugo build"
hugo -d gh-pages || exit 1
