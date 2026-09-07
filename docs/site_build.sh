#!/bin/bash
# site_build.sh - единый генератор контента сайта trust-lang (docs/): собирает
# фрагмент playground и полный статический сайт в указанный каталог.
#
# Это ЕДИНЫЙ скрипт генерации: и прод-сборка в gh-pages (по умолчанию), и локальная
# сборка для разработчика (site_playground_run.sh вызывает его с другими настройками).
#
# По умолчанию (без аргументов) собирает фрагмент и статический сайт в каталог gh-pages
# для домена trust-lang.net (публикация на GitHub Pages / Netlify).
#
# Контракт: запуск ТОЛЬКО из каталога docs/ (проверяется ИМЯ текущего каталога).
#
# trust-lsp обязателен - фрагменты генерируются из актуальных примеров на момент
# сборки. Если бинарник отсутствует, скрипт завершается ошибкой с требованием
# собрать проект (устаревшие закоммиченные docs/fragments/*.html не используются).
# Monaco при отсутствии скачивается (нужна сеть).
#
# Использование (из каталога docs/):
#   ./site_build.sh                       # прод: статика в gh-pages, прод server-url
#   ./site_build.sh --server-url <URL> --out-dir <DIR> [другие опции]
#
# Опции (по умолчанию - продовые):
#   --server-url URL    URL балансировщика playground, встраиваемый во фрагмент
#                       (default: https://playground.trust-lang.net/run)
#   --out-dir DIR       каталог, куда hugo кладёт собранный статический сайт
#                       (default: gh-pages)
#   --monaco-url URL    базовый URL self-hosted Monaco во фрагменте
#                       (default: /monaco/vs)
#   --examples-dir DIR  каталог с примерами *.src для списка примеров песочницы
#                       (default: ../examples)
#   --help              показать эту справку и выйти
#
# Переменные окружения:
#   HUGO        - команда hugo (по умолчанию hugo)
#   NO_MONACO=1 - не скачивать Monaco (если уже завендорен)
set -uo pipefail

# -- Настройки по умолчанию (прод: домен trust-lang.net, каталог gh-pages) --
SERVER_URL="https://playground.trust-lang.net/run"
OUT_DIR="gh-pages"
MONACO_URL="/monaco/vs"
EXAMPLES_DIR="../examples"

usage() {
    echo "usage: ./site_build.sh [--server-url URL] [--out-dir DIR] [--monaco-url URL]" >&2
    echo "                       [--examples-dir DIR] [--help]" >&2
    echo "  (без аргументов - прод-сборка: out-dir=gh-pages," >&2
    echo "   server-url=https://playground.trust-lang.net/run)" >&2
}

# Парсинг аргументов. Поддерживается только форма "--option value".
while [ $# -gt 0 ]; do
    case "$1" in
        --server-url)
            [ $# -ge 2 ] || { echo "error: --server-url requires a value" >&2; usage; exit 1; }
            SERVER_URL="$2"; shift 2 ;;
        --out-dir)
            [ $# -ge 2 ] || { echo "error: --out-dir requires a value" >&2; usage; exit 1; }
            OUT_DIR="$2"; shift 2 ;;
        --monaco-url)
            [ $# -ge 2 ] || { echo "error: --monaco-url requires a value" >&2; usage; exit 1; }
            MONACO_URL="$2"; shift 2 ;;
        --examples-dir)
            [ $# -ge 2 ] || { echo "error: --examples-dir requires a value" >&2; usage; exit 1; }
            EXAMPLES_DIR="$2"; shift 2 ;;
        --help)
            usage; exit 0 ;;
        *)
            echo "error: unknown argument: $1" >&2
            usage
            exit 1 ;;
    esac
done

# Проверка места запуска по имени текущего каталога.
if [ "$(basename "$PWD")" != "docs" ]; then
    echo "error: run this script ONLY from the docs/ directory" >&2
    echo "       usage: cd docs && ./site_build.sh" >&2
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

# hugo обязателен для сборки сайта.
HUGO="${HUGO:-hugo}"
if ! command -v "$HUGO" >/dev/null 2>&1; then
    echo "error: hugo not found (set HUGO to the hugo binary)" >&2
    exit 1
fi

echo "[site_build] server-url=$SERVER_URL out-dir=$OUT_DIR"

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
# из EXAMPLES_DIR (trust-lsp встраивает их в фрагмент при генерации - отдельные копии
# в content/ не нужны). Фрагмент лежит вне content/, чтобы hugo не трактовал его как
# страницу и не парсил JS как Go-шаблон. Путь к фрагменту фиксированный: его читает
# shortcode layouts/shortcodes/playground.html (readFile "fragments/playground.html").
echo "[2/4] generate playground fragment"
mkdir -p fragments
frag="fragments/playground.html"
# trust-lsp может вернуть ненулевой код при ошибке транспиляции примера; это не должно
# ронять сборку сайта.
../_build/trust-lsp --html "$EXAMPLES_DIR/hello.src" --examples-dir "$EXAMPLES_DIR" \
    --server-url "$SERVER_URL" --monaco-url "$MONACO_URL" > "$frag" \
    || echo "  warning: trust-lsp --html exit=$?"
echo "  generated $frag"

# -- 3. Очистка предыдущей версии сайта + метаданные --
# .git - скрытый, glob "$OUT_DIR"/* его не трогает; CNAME пересоздаём из content/CNAME.
# Осторожность: защита от опасного/пустого out-dir (чтобы rm -rf не ушёл в корень).
case "$OUT_DIR" in
    ""|"/"|"."|"..") echo "error: refusing to clean out-dir '$OUT_DIR'" >&2; exit 1 ;;
esac
echo "[3/4] clean output dir ($OUT_DIR)"
if [ -d "$OUT_DIR" ]; then
    rm -rf "$OUT_DIR"/*
fi
mkdir -p "$OUT_DIR"
cp -f content/CNAME "$OUT_DIR/CNAME" 2>/dev/null || true

# -- 4. Сборка hugo --
# Примечание: --minify не используем - встроенный JS-минификатор hugo не
# разбирает inline-JS playground (современный синтаксис) и роняет сборку.
echo "[4/4] hugo build"
"$HUGO" -d "$OUT_DIR" || exit 1
echo "[site_build] done: $OUT_DIR"
