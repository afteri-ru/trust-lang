#!/usr/bin/env bash
# install_worker.sh - установка исполнительного VPS (режим worker).
# Идемпотентный. Требует token (выдаётся админом балансировщика); playground_url
# по умолчанию - публичный https://playground.trust-lang.net.
# Использование:
#   sudo ./install_worker.sh [options]
#     --playground-url <u>   URL балансировщика (default: https://playground.trust-lang.net)
#     --token <t>        Токен воркера (64 hex) - ОБЯЗАТЕЛЕН
#     --user <u>         Пользователь (default: trust)
#     --dist <dir>       Каталог с дистрибутивом (default: _build/dist)
#     --lsp-bin <p>      Путь к trust-lsp (default: из дистрибутива)
#     --max-parallel <n> Параллельных задач (default: число ядер)
#     --project-dir <p>  Рабочий каталог для trust-lsp (default: /opt/trust-playground)
#     --help             Справка
set -euo pipefail

PLAYGROUND_URL="https://playground.trust-lang.net"
TOKEN=""
APP_USER="trust"
DIST_DIR="_build/dist"
LSP_BIN=""
MAX_PARALLEL="$(nproc)"
PROJECT_DIR="/opt/trust-playground"

while [ $# -gt 0 ]; do
    case "$1" in
        --playground-url)   PLAYGROUND_URL="$2"; shift 2 ;;
        --token)        TOKEN="$2"; shift 2 ;;
        --user)         APP_USER="$2"; shift 2 ;;
        --dist)         DIST_DIR="$2"; shift 2 ;;
        --lsp-bin)      LSP_BIN="$2"; shift 2 ;;
        --max-parallel) MAX_PARALLEL="$2"; shift 2 ;;
        --project-dir)  PROJECT_DIR="$2"; shift 2 ;;
        --help)         sed -n '2,14p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ -z "$TOKEN" ]; then
    echo "error: --token is required (выдаётся админом балансировщика)" >&2
    exit 1
fi
if [ "$(id -u)" -ne 0 ]; then
    echo "error: run as root (sudo)" >&2
    exit 1
fi

INSTALL_DIR="/opt/trust-playground"
CONF_DIR="/etc/trust-playground"
CONF="$CONF_DIR/trust-playground.conf"

# -- 1. Пользователь --
if ! id "$APP_USER" &>/dev/null; then
    useradd --system --create-home --shell /usr/sbin/nologin "$APP_USER"
    echo "[1/4] created user: $APP_USER"
else
    echo "[1/4] user $APP_USER exists"
fi

# -- 2. Бинарники + runtime --
archive=$(ls -t "$DIST_DIR"/trust-lang-*.tar.gz 2>/dev/null | head -1)
if [ -z "$archive" ]; then
    echo "error: no trust-lang-*.tar.gz in $DIST_DIR" >&2
    exit 1
fi
echo "[2/4] distribution: $archive"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xzf "$archive" -C "$tmp"
pkg_dir=$(find "$tmp" -maxdepth 2 -type d -name bin -printf '%h\n' | head -1)
mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib"
install -m 0755 "$pkg_dir/bin/trust-playground" "$INSTALL_DIR/bin/trust-playground"
install -m 0755 "$pkg_dir/bin/trust-lsp" "$INSTALL_DIR/bin/trust-lsp"
install -m 0644 "$pkg_dir/lib/trust-runtime.so" "$INSTALL_DIR/lib/trust-runtime.so" 2>/dev/null || true
install -m 0644 "$pkg_dir/lib/trust-runtime.a"  "$INSTALL_DIR/lib/trust-runtime.a"  2>/dev/null || true
[ -n "$LSP_BIN" ] || LSP_BIN="$INSTALL_DIR/bin/trust-lsp"
chown -R "$APP_USER:$APP_USER" "$INSTALL_DIR"
echo "[2/4] installed to $INSTALL_DIR (lsp_bin=$LSP_BIN)"

# -- 3. Конфиг (worker-секция) --
mkdir -p "$CONF_DIR"
if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<CONF
# trust-playground (воркер)
worker.playground_url=$PLAYGROUND_URL
worker.token=$TOKEN
worker.lsp_bin=$LSP_BIN
worker.max_parallel=$MAX_PARALLEL
worker.max_memory_mb=512
worker.max_output_kb=2048
worker.job_timeout=30
worker.poll_interval_ms=200
worker.project_dir=$PROJECT_DIR
# Имя build-архива формирует trust-lsp: trust-lang-<версия>-generated.tar.gz.
# worker.lsp_opts=-Wsigil=ignore   # доп. опции, всегда передаваемые в trust-lsp
CONF
    chown "$APP_USER:$APP_USER" "$CONF"
    echo "[3/4] config written to $CONF"
else
    echo "[3/4] config already present (edit it manually if needed)"
fi

# -- 4. Запуск в консоли (не сервис) --
echo "[4/4] worker installed. Запуск:"
echo "   В консоли (с выводом статистики):  /opt/trust-playground/bin/trust-playground --config $CONF"
echo "   В фоне:                            nohup /opt/trust-playground/bin/trust-playground --config $CONF >> /var/log/trust-playground-worker.log 2>&1 &"
echo "   Остановка (фон):                   pkill -x trust-playground"

echo "Done. Worker config: $CONF (systemd не используется; запуск в консоли/фоне)."
echo "Note: https playground_url requires curl on this host."
