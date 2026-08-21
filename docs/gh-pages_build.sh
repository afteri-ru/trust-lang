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

# -- 1. Синхронизация примеров (../examples → обе языковые папки playground) --
echo "[1/5] sync examples"
for lang in ru en; do
    dir="content/$lang/playground"
    mkdir -p "$dir"
    rm -f "$dir"/*.src
    cp -f ../examples/*.src "$dir/" 2>/dev/null || true
done

# -- 2. Self-hosted Monaco (скачиваем, если отсутствует) --
echo "[2/5] ensure Monaco"
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

# -- 3. Генерация HTML-фрагмента playground --
# Единый фрагмент playground.html (языконезависимый виджет) для страниц /ru/playground/ и
# /en/playground/ (обе встраивают его через shortcode `{{< playground >}}`). Примеры берутся
# из en/playground (зеркало корневого ../examples). Фрагмент лежит вне content/, чтобы hugo не
# трактовал его как страницу и не парсил JS как Go-шаблон.
echo "[3/5] generate playground fragment"
mkdir -p fragments
frag="fragments/playground.html"
dir="content/en/playground"
# trust-lsp может вернуть ненулевой код при ошибке транспиляции примера; это не должно
# ронять сборку сайта.
../_build/trust-lsp --html "$dir/hello.src" --examples-dir "$dir" \
    --server-url "https://playground.trust-lang.net/run" --monaco-url "/monaco/vs" > "$frag" \
    || echo "  warning: trust-lsp --html exit=$?"
echo "  generated $frag"

# -- 4. Очистка предыдущей версии сайта + метаданные --
# .git - скрытый, glob `gh-pages/*` его не трогает; CNAME пересоздаём из content/CNAME.
echo "[4/5] clean output dir"
if [ -d gh-pages ]; then
    rm -rf gh-pages/*
fi
mkdir -p gh-pages
cp -f content/CNAME gh-pages/CNAME 2>/dev/null || true

# -- 5. Сборка hugo --
# Примечание: --minify не используем - встроенный JS-минификатор hugo не
# разбирает inline-JS playground (современный синтаксис) и роняет сборку.
echo "[5/5] hugo build"
hugo -d gh-pages || exit 1
