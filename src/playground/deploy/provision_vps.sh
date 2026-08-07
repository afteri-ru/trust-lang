#!/usr/bin/env bash
# provision_vps.sh — подготовка VPS под trust-playground.
# Идемпотентный: повторные запуски безопасны (проверки перед установкой).
# Использование:
#   ./provision_vps.sh [options]
#     --domain <d>   Домен/публичный IP для nginx-сайта (по умолчанию: пусто, без nginx)
#     --user <u>     Пользователь-владелец сервиса (по умолчанию: trust)
#     --port <p>     Порт trust-playground (по умолчанию: 8080)
#     --dist <dir>   Каталог с дистрибутивом (по умолчанию: _build/dist)
#     --webroot <p>  Корень сайта, который раздаёт nginx (по умолчанию:
#                    /var/www/trust-playground; туда нужно скопировать docs/public)
#     --no-nginx     Не настраивать nginx
#     --help         Справка
set -euo pipefail

DOMAIN=""
APP_USER="trust"
APP_PORT="8080"
DIST_DIR="_build/dist"
WEBROOT="/var/www/trust-playground"
WITH_NGINX=1

while [ $# -gt 0 ]; do
    case "$1" in
        --domain) DOMAIN="$2"; shift 2 ;;
        --user)   APP_USER="$2"; shift 2 ;;
        --port)   APP_PORT="$2"; shift 2 ;;
        --dist)   DIST_DIR="$2"; shift 2 ;;
        --webroot) WEBROOT="$2"; shift 2 ;;
        --no-nginx) WITH_NGINX=0; shift ;;
        --help)   sed -n '2,16p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "error: run as root (sudo)" >&2
    exit 1
fi

INSTALL_DIR="/opt/trust-playground"
UNIT_FILE="/etc/systemd/system/trust-playground.service"
DEPLOY_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── 0. Найти архив дистрибутива ──
archive=$(ls -t "$DIST_DIR"/trust-lang-*.tar.gz 2>/dev/null | head -1)
if [ -z "$archive" ]; then
    echo "error: no trust-lang-*.tar.gz in $DIST_DIR" >&2
    exit 1
fi
echo "[1/5] using distribution archive: $archive"

# ── 1. Пользователь (идемпотентно) ──
if ! id "$APP_USER" &>/dev/null; then
    useradd --system --create-home --shell /usr/sbin/nologin "$APP_USER"
    echo "[1/5] created system user: $APP_USER"
else
    echo "[1/5] user $APP_USER already exists"
fi

# ── 2. Базовые зависимости (идемпотентно) ──
command -v curl >/dev/null 2>&1 || { echo "[2/5] installing curl"; apt-get update -y && apt-get install -y curl; }
command -v systemctl >/dev/null 2>&1 || { echo "error: systemd not found" >&2; exit 1; }

# ── 3. Установка бинарника + runtime (идемпотентно по содержимому) ──
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xzf "$archive" -C "$tmp"
pkg_dir=$(find "$tmp" -maxdepth 2 -type d -name bin -printf '%h\n' | head -1)
mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib"
install -m 0755 "$pkg_dir/bin/trust-playground" "$INSTALL_DIR/bin/trust-playground"
install -m 0644 "$pkg_dir/lib/trust-runtime.so" "$INSTALL_DIR/lib/trust-runtime.so" 2>/dev/null || true
install -m 0644 "$pkg_dir/lib/trust-runtime.a"  "$INSTALL_DIR/lib/trust-runtime.a"  2>/dev/null || true
chown -R "$APP_USER:$APP_USER" "$INSTALL_DIR"
echo "[3/5] installed to $INSTALL_DIR"

# ── 4. systemd-юнит (идемпотентно) ──
if [ ! -f "$UNIT_FILE" ]; then
    sed -e "s/@USER@/$APP_USER/g" -e "s/@PORT@/$APP_PORT/g" \
        "$DEPLOY_DIR/trust-playground.service.in" > "$UNIT_FILE"
    systemctl daemon-reload
    echo "[4/5] installed systemd unit"
else
    echo "[4/5] systemd unit already present"
fi
systemctl enable trust-playground >/dev/null 2>&1 || true
systemctl restart trust-playground || true

# ── 5. nginx (опционально) ──
if [ "$WITH_NGINX" -eq 1 ] && [ -n "$DOMAIN" ]; then
    if ! command -v nginx >/dev/null 2>&1; then
        apt-get install -y nginx
    fi
    site="/etc/nginx/sites-available/trust-playground"
    if [ ! -f "$site" ]; then
        sed -e "s/@DOMAIN@/$DOMAIN/g" -e "s/@PORT@/$APP_PORT/g" -e "s|@WEBROOT@|$WEBROOT|g" \
            "$DEPLOY_DIR/nginx-playground.conf.in" > "$site"
        ln -sf "$site" /etc/nginx/sites-enabled/trust-playground
        echo "[5/5] nginx site installed for $DOMAIN"
    else
        echo "[5/5] nginx site already present"
    fi
    nginx -t && systemctl reload nginx || true
else
    echo "[5/5] nginx skipped"
fi

echo "Done. trust-playground listening on 127.0.0.1:$APP_PORT (systemd: trust-playground)."
