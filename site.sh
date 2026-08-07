#!/bin/bash
# site.sh — сборка документационного сайта (docs/) с godbolt-playground.
# Список примеров берётся из examples/*.src в корне проекта (источник истины),
# синхронизируется в docs/content/{ru,en}/playground, генерирует HTML-фрагмент
# playground (trust-lsp --html), при необходимости скачивает self-hosted Monaco
# и собирает сайт через hugo.
#
# trust-lsp обязателен — фрагменты генерируются из актуальных примеров на момент
# сборки. Если бинарник отсутствует, скрипт завершается ошибкой с требованием
# собрать проект (устаревшие закоммиченные docs/fragments/*.html не используются).
# Monaco при отсутствии скачивается (нужна сеть).
#
# Переменные окружения:
#   SERVER_URL  — endpoint живого запуска для фрагмента (по умолчанию
#                 https://playground.trust-lang.net/run)
#   MONACO_URL  — базовый путь сборки Monaco (по умолчанию /monaco/vs)
#   INITIAL     — начальный пример в редакторе (по умолчанию rational.src)
#   HUGO        — команда hugo (по умолчанию hugo)
#   NO_MONACO=1 — не скачивать Monaco (если уже завендорен)
set -uo pipefail

root="."
if [ -n "${1:-}" ]; then
    root="$1"
fi
cd "$root" || { echo "error: cannot cd to $root" >&2; exit 1; }

# Live-only: статический сайт на trust-lang.net (GitHub Pages), а пере-транспиляция
# идёт через JS fetch на ВНЕШНИЙ балансировщик playground (playground.trust-lang.net),
# который диспетчеризует задачи на исполнительные VPS-воркеры.
# URL можно переопределить через env SERVER_URL при сборке.
SERVER_URL="${SERVER_URL:-https://playground.trust-lang.net/run}"
MONACO_URL="${MONACO_URL:-/monaco/vs}"
INITIAL="${INITIAL:-rational.src}"
HUGO="${HUGO:-hugo}"
site="$root/docs"
trust_lsp="$root/_build/trust-lsp"
examples_src="$root/examples"
monaco_dest="$site/static/monaco/vs"

# trust-lsp обязателен: фрагменты генерируются только из актуальных примеров
# на момент сборки. Если бинарник отсутствует — ошибка с требованием собрать
# проект (не переиспользуем потенциально устаревшие закоммиченные фрагменты).
if [ ! -x "$trust_lsp" ]; then
    echo "error: trust-lsp binary not found at $trust_lsp" >&2
    echo "       build the project first: cmake --build _build --target trust-lsp" >&2
    exit 1
fi

# ── 1. Синхронизация примеров (examples/ → обе языковые папки playground) ──
echo "[1/5] sync examples"
for lang in ru en; do
    dir="$site/content/$lang/playground"
    mkdir -p "$dir"
    rm -f "$dir"/*.src
    cp -f "$examples_src"/*.src "$dir/" 2>/dev/null || true
done

# ── 2. Self-hosted Monaco (скачиваем, если отсутствует) ──
echo "[2/5] ensure Monaco"
if [ "${NO_MONACO:-0}" != "1" ] && [ ! -f "$monaco_dest/loader.js" ]; then
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' EXIT
    mkdir -p "$site/static/monaco"
    curl -fsSL "https://registry.npmjs.org/monaco-editor/-/monaco-editor-0.52.2.tgz" -o "$tmp/monaco.tgz" \
        || { echo "error: failed to download monaco-editor" >&2; exit 1; }
    tar -xzf "$tmp/monaco.tgz" -C "$tmp" package/min/vs \
        || { echo "error: failed to extract monaco-editor" >&2; exit 1; }
    rm -rf "$monaco_dest"
    mv "$tmp/package/min/vs" "$monaco_dest"
    rm -rf "$tmp/package"
    echo "  Monaco downloaded to $monaco_dest"
else
    echo "  Monaco already present at $monaco_dest"
fi

# ── 3. Генерация HTML-фрагмента playground ──
# Единый фрагмент playground.html (языконезависимый виджет) для страниц /ru/playground/ и
# /en/playground/ (обе встраивают его через shortcode `{{< playground >}}`). Примеры берутся
# из en/playground (зеркало корневого examples/). Фрагмент лежит вне content/, чтобы hugo не
# трактовал его как страницу и не парсил JS как Go-шаблон.
echo "[3/5] generate playground fragment"
mkdir -p "$site/fragments"
frag="$site/fragments/playground.html"
dir="$site/content/en/playground"
# trust-lsp может вернуть ненулевой код при ошибке транспиляции примера; это не должно
# ронять сборку сайта.
"$trust_lsp" --html "$dir/$INITIAL" --examples-dir "$dir" \
    --server-url "$SERVER_URL" --monaco-url "$MONACO_URL" > "$frag" \
    || echo "  warning: trust-lsp --html exit=$?"
echo "  generated $frag"

# ── 4. Метаданные сайта в public ──
echo "[4/5] metadata"
mkdir -p "$site/public"
cp -f "$site/content/CNAME" "$site/public/CNAME" 2>/dev/null || true
cp -f "$site/content/README.txt" "$site/public/README.txt" 2>/dev/null || true

# ── 5. Сборка hugo ──
# Примечание: --minify не используем — встроенный JS-минификатор hugo не
# разбирает inline-JS playground (современный синтаксис) и роняет сборку.
echo "[5/5] hugo build"
cd "$site" || exit 1
"$HUGO" --cleanDestinationDir -d public || exit 1
