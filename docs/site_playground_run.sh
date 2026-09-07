#!/usr/bin/env bash
# site_playground_run.sh - локальная среда: запуск песочницы (балансировщик + воркер) и hugo server.
# Контент сайта (фрагмент playground + статику) генерирует ЕДИНЫЙ скрипт site_build.sh,
# вызываемый здесь с настройками для локальной машины разработчика: server-url локальной
# песочницы и out-dir в отдельный каталог ../_build/dev-site (прод-каталог gh-pages при
# этом не трогается). Контракт: запуск ТОЛЬКО из каталога docs/ (проверяется ИМЯ текущего каталога).
# Пути - явные литералы, без переменных-путей и поисков.
# Временные файлы (конфиг, логи) - в ../_build/dev-playground.
#
# Использование (из каталога docs/):
#   ./site_playground_run.sh [backend_port] [web_port]
#     backend_port (default 18080) - порт балансировщика;
#     web_port     (default 1313)  - порт веб-сервера; "no" - не запускать веб-сервер.
#
# Остановка:
#   pkill -x trust-playground
#   pkill -f 'hugo server --source .'
set -euo pipefail

# Проверка места запуска по имени текущего каталога.
if [ "$(basename "$PWD")" != "docs" ]; then
    echo "error: run this script ONLY from the docs/ directory" >&2
    echo "       usage: cd docs && ./site_playground_run.sh" >&2
    exit 1
fi

PORT="${1:-18080}"
WEB_PORT="${2:-1313}"
HUGO_BIN="$(command -v hugo || true)"

if [ ! -x ../_build/trust-playground ] || [ ! -x ../_build/trust-lsp ]; then
    echo "error: build first: cmake --build ../_build --target trust-playground trust-lsp" >&2
    exit 1
fi

# Extended hugo? (нужен для SCSS сайта).
HUGO_EXTENDED=0
if [ -n "$HUGO_BIN" ] && "$HUGO_BIN" version 2>/dev/null | grep -q extended; then
    HUGO_EXTENDED=1
fi

# -- 1. Конфиг бэкенда (в ../_build/dev-playground) --
TOKEN="$(openssl rand -hex 32 2>/dev/null || head -c 32 /dev/urandom | od -An -tx1 | tr -d ' \n')"
mkdir -p ../_build/dev-playground
cat > ../_build/dev-playground/trust-playground.conf <<CONF
playground.listen=127.0.0.1
playground.port=$PORT
playground.poll_timeout=30
playground.job_timeout=30
playground.rate_limit_per_ip=100000
playground.stats_token=$TOKEN
local-dev=$TOKEN
worker.playground_url=http://127.0.0.1:$PORT
worker.token=$TOKEN
worker.lsp_bin=../_build/trust-lsp
worker.max_parallel=$(nproc)
CONF

# -- 2. Бэкенд: балансировщик + воркер --
echo "[site_playground_run] starting balancer on http://127.0.0.1:$PORT"
../_build/trust-playground --playground --config ../_build/dev-playground/trust-playground.conf \
    > ../_build/dev-playground/server.log 2>&1 &
SERVER_PID=$!
echo "[site_playground_run] starting worker (lsp=../_build/trust-lsp)"
../_build/trust-playground --config ../_build/dev-playground/trust-playground.conf \
    > ../_build/dev-playground/worker.log 2>&1 &
WORKER_PID=$!

# -- 3. Генерация контента сайта (единый site_build.sh) + запуск hugo server --
WEB_UP=0
MODE=""
if [ "$WEB_PORT" = "no" ]; then
    echo "[site_playground_run] web server skipped"
elif [ "$HUGO_EXTENDED" -eq 1 ]; then
    MODE="hugo"
    # Контент (фрагмент playground с ЛОКАЛЬНЫМ URL + полная статика) генерирует ЕДИНЫЙ
    # site_build.sh с настройками для локальной машины разработчика: server-url локальной
    # песочницы и out-dir в отдельный каталог ../_build/dev-site (прод gh-pages не трогаем).
    # Фрагмент пишется в docs/fragments/playground.html (его читает shortcode), поэтому
    # hugo server ниже отдаёт именно локальный фрагмент, а не прод-сборку прошлого прогона.
    echo "[site_playground_run] generating local site via site_build.sh (server-url=http://127.0.0.1:$PORT/run)"
    export HUGO="$HUGO_BIN"
    ./site_build.sh --server-url "http://127.0.0.1:$PORT/run" --out-dir "../_build/dev-site" \
        || echo "  warning: site_build.sh exit=$?; continuing (hugo server may use stale fragment)" >&2
    # Очистка выходного каталога hugo server (по умолчанию ./public — производный,
    # см. .gitignore): остатки прошлых/прерванных сборок (например, обычный файл
    # на месте каталога страницы hierarchy) иначе роняют рендер ошибкой "not a directory".
    rm -rf public
    echo "[site_playground_run] starting hugo server on :$WEB_PORT"
    "$HUGO_BIN" server --source . --port "$WEB_PORT" --bind 127.0.0.1 \
        > ../_build/dev-playground/hugo.log 2>&1 &
    WEB_PID=$!
    WEB_UP=1
else
    echo "[site_playground_run] warning: extended hugo not found; cannot serve site" >&2
fi

sleep 2

echo
echo "=========================================================================="
echo " trust-playground local dev is UP"
echo "=========================================================================="
if [ "$WEB_UP" -eq 1 ] && [ "$MODE" = "hugo" ]; then
    echo "  Local site: http://127.0.0.1:$WEB_PORT/"
    echo "    Playground: http://127.0.0.1:$WEB_PORT/ru/playground/  /en/playground/"
fi
echo "  Backend SERVER_URL (glue-JS uses it): http://127.0.0.1:$PORT/run"
# Статистика: токен НЕ передаётся в URL (см. main.cpp). Браузер - через /stats/login
# (cookie-сессия), скрипты/curl - через заголовок X-Stats-Token.
echo "  Stats (browser login): http://127.0.0.1:$PORT/stats/login   # введите stats_token"
echo "  Stats (JSON): curl -H \"X-Stats-Token: $TOKEN\" http://127.0.0.1:$PORT/stats"
echo "  Stats (HTML): curl -H \"X-Stats-Token: $TOKEN\" \"http://127.0.0.1:$PORT/stats?format=html\""
echo "  Logs: ../_build/dev-playground/{server,worker,hugo}.log"
echo "  Config: ../_build/dev-playground/trust-playground.conf"
echo "  Stop: press any key below"
echo "=========================================================================="

# -- 4. Ожидание нажатия любой клавиши → остановка серверов --
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
echo "  Stopped. (logs preserved in ../_build/dev-playground)"
