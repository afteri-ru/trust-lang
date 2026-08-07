#!/usr/bin/env bash
# dev_playground.sh — локальная среда для разработки/отладки playground.
#
# Делает всё за один запуск:
#   1) запускает локальный бэкенд: балансировщик (--playground) + один воркер
#      (trust-lsp из _build) на http://127.0.0.1:PORT;
#   2) показывает локальную страницу playground и печатает её HTTP-адрес:
#      - если установлен EXTENDED hugo — собирает сайт (site.sh) и запускает
#        `hugo server`, адреса /ru/playground/ и /en/playground/;
#      - иначе — генерирует автономную страницу (trust-lsp --html-full) и раздаёт
#        её через python3 http.server, адрес /playground.html.
# Временные файлы (конфиг, страница, логи) — в _build/dev-playground.
#
# Использование (из корня проекта):
#   ./src/playground/deploy/dev_playground.sh [backend_port] [web_port]
#     backend_port (default 18080) — порт балансировщика;
#     web_port     (default 1313)  — порт веб-сервера; "no" — не запускать веб-сервер.
#
# Остановка:
#   pkill -x trust-playground
#   pkill -f 'hugo server --source docs'
#   pkill -f 'http.server'        # fallback-веб-сервер
set -euo pipefail

PORT="${1:-18080}"
WEB_PORT="${2:-1313}"
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
PG="$ROOT/_build/trust-playground"
LSP="$ROOT/_build/trust-lsp"
HUGO_BIN="$(command -v hugo || true)"

if [ ! -x "$PG" ] || [ ! -x "$LSP" ]; then
    echo "error: build first: cmake --build _build --target trust-playground trust-lsp" >&2
    exit 1
fi

# Extended hugo? (нужен для SCSS сайта).
HUGO_EXTENDED=0
if [ -n "$HUGO_BIN" ] && "$HUGO_BIN" version 2>/dev/null | grep -q extended; then
    HUGO_EXTENDED=1
fi

# ── 1. Конфиг бэкенда (в _build/dev-playground) ──
TOKEN="$(openssl rand -hex 32 2>/dev/null || head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
DEV_DIR="$ROOT/_build/dev-playground"
mkdir -p "$DEV_DIR"
CONF="$DEV_DIR/trust-playground.conf"
cat > "$CONF" <<CONF
playground.listen=127.0.0.1
playground.port=$PORT
playground.poll_timeout=30
playground.job_timeout=30
playground.rate_limit_per_ip=100000
playground.stats_token=$TOKEN
local-dev=$TOKEN
worker.playground_url=http://127.0.0.1:$PORT
worker.token=$TOKEN
worker.lsp_bin=$LSP
worker.max_parallel=$(nproc)
CONF

# ── 2. Бэкенд: балансировщик + воркер ──
echo "[dev_playground] starting balancer on http://127.0.0.1:$PORT"
"$PG" --playground --config "$CONF" > "$DEV_DIR/server.log" 2>&1 &
SERVER_PID=$!
echo "[dev_playground] starting worker (lsp=$LSP)"
"$PG" --config "$CONF" > "$DEV_DIR/worker.log" 2>&1 &
WORKER_PID=$!

# ── 3. Страница + веб-сервер ──
WEB_UP=0
MODE=""
if [ "$WEB_PORT" = "no" ]; then
    echo "[dev_playground] web server skipped"
elif [ "$HUGO_EXTENDED" -eq 1 ]; then
    MODE="hugo"
    echo "[dev_playground] extended hugo detected; building site with local SERVER_URL"
    ( cd "$ROOT" && SERVER_URL="http://127.0.0.1:$PORT/run" NO_MONACO=1 ./site.sh > "$DEV_DIR/site.log" 2>&1 ) \
        || echo "[dev_playground] warning: site.sh failed; see $DEV_DIR/site.log"
    echo "[dev_playground] starting hugo server on :$WEB_PORT"
    "$HUGO_BIN" server --source "$ROOT/docs" --port "$WEB_PORT" --bind 127.0.0.1 \
        > "$DEV_DIR/hugo.log" 2>&1 &
    WEB_PID=$!
    WEB_UP=1
elif command -v python3 >/dev/null 2>&1; then
    MODE="standalone"
    WWW="$DEV_DIR/www"
    rm -rf "$WWW"
    mkdir -p "$WWW"
    ln -sfn "$ROOT/docs/static/monaco" "$WWW/monaco"
    INITIAL="$(ls "$ROOT"/examples/*.src | head -1)"
    echo "[dev_playground] generating standalone page (server-url=$PORT)"
    "$LSP" --html "$INITIAL" --html-full --server-url "http://127.0.0.1:$PORT/run" \
        --monaco-url /monaco/vs --examples-dir "$ROOT/examples" \
        > "$WWW/playground.html" 2>"$DEV_DIR/html.log" || true
    echo "[dev_playground] serving www via python3 http.server on :$WEB_PORT"
    ( cd "$WWW" && exec python3 -m http.server "$WEB_PORT" --bind 127.0.0.1 ) > "$DEV_DIR/web.log" 2>&1 &
    WEB_PID=$!
    WEB_UP=1
else
    echo "[dev_playground] warning: neither extended hugo nor python3 found" >&2
fi

sleep 2

echo
echo "=========================================================================="
echo " trust-playground local dev is UP"
echo "=========================================================================="
if [ "$WEB_UP" -eq 1 ]; then
    echo "  Open in browser:"
    if [ "$MODE" = "hugo" ]; then
        echo "    http://127.0.0.1:$WEB_PORT/ru/playground/"
        echo "    http://127.0.0.1:$WEB_PORT/en/playground/"
    else
        echo "    http://127.0.0.1:$WEB_PORT/playground.html"
    fi
fi
echo "  Backend SERVER_URL (glue-JS uses it): http://127.0.0.1:$PORT/run"
# HTML-страница и JSON статистики (обязателен stats_token — см. конфиг).
echo "  Stats page (HTML): http://127.0.0.1:$PORT/stats?token=$TOKEN&format=html"
echo "  Stats page (JSON): http://127.0.0.1:$PORT/stats?token=$TOKEN"
echo "  Logs: $DEV_DIR/{server,worker,site,hugo,web,html}.log"
echo "  Config: $CONF"
echo "  Stop: press any key below"
echo "=========================================================================="

# ── 5. Ожидание нажатия любой клавиши → остановка серверов ──
echo
echo "  Press any key to stop the servers..."
read -r -n 1 -s -p ""
echo
kill "$SERVER_PID" "$WORKER_PID" 2>/dev/null
if [ -n "${WEB_PID:-}" ]; then
    kill "$WEB_PID" 2>/dev/null
fi
sleep 1
kill -9 "$SERVER_PID" "$WORKER_PID" 2>/dev/null || true
if [ -n "${WEB_PID:-}" ]; then
    kill -9 "$WEB_PID" 2>/dev/null || true
fi
echo "  Stopped. (logs preserved in $DEV_DIR)"
