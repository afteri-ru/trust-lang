#!/usr/bin/env bash
# install_playground.sh — установка балансировщика trust-playground (режим --playground).
# Идемпотентный. Использование:
#   sudo ./install_playground.sh [options]
#     --domain <d>       Домен для nginx (default: пусто, без nginx)
#     --user <u>         Пользователь-владелец сервиса (default: trust)
#     --port <p>         Порт балансировщика на 127.0.0.1 (default: 8080)
#     --dist <dir>       Каталог с дистрибутивом (default: _build/dist)
#     --body-limit <kb>  Лимит тела запроса (default: 256)
#     --timeout <s>      Таймаут задачи (default: 30)
#     --no-nginx         Не настраивать nginx
#     --help             Справка
set -euo pipefail

DOMAIN=""
APP_USER="trust"
APP_PORT="8080"
DIST_DIR="_build/dist"
BODY_LIMIT="256"
TIMEOUT="30"
WITH_NGINX=1

while [ $# -gt 0 ]; do
    case "$1" in
        --domain)      DOMAIN="$2"; shift 2 ;;
        --user)        APP_USER="$2"; shift 2 ;;
        --port)        APP_PORT="$2"; shift 2 ;;
        --dist)        DIST_DIR="$2"; shift 2 ;;
        --body-limit)  BODY_LIMIT="$2"; shift 2 ;;
        --timeout)     TIMEOUT="$2"; shift 2 ;;
        --no-nginx)    WITH_NGINX=0; shift ;;
        --help)        sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 1 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    echo "error: run as root (sudo)" >&2
    exit 1
fi

INSTALL_DIR="/opt/trust-playground"
CONF_DIR="/etc/trust-playground"
CONF="$CONF_DIR/trust-playground.conf"
UNIT="/etc/systemd/system/trust-playground.service"
DEPLOY="$(cd "$(dirname "$0")" && pwd)"

# ── 0. Дистрибутив ──
archive=$(ls -t "$DIST_DIR"/trust-lang-*.tar.gz 2>/dev/null | head -1)
if [ -z "$archive" ]; then
    echo "error: no trust-lang-*.tar.gz in $DIST_DIR" >&2
    exit 1
fi
echo "[1/5] distribution: $archive"

# ── 1. Пользователь ──
if ! id "$APP_USER" &>/dev/null; then
    useradd --system --create-home --shell /usr/sbin/nologin "$APP_USER"
    echo "[1/5] created user: $APP_USER"
else
    echo "[1/5] user $APP_USER exists"
fi

# ── 2. Бинарники + runtime ──
command -v curl >/dev/null 2>&1 || { apt-get update -y >/dev/null && apt-get install -y curl >/dev/null; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xzf "$archive" -C "$tmp"
pkg_dir=$(find "$tmp" -maxdepth 2 -type d -name bin -printf '%h\n' | head -1)
mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib"
install -m 0755 "$pkg_dir/bin/trust-playground" "$INSTALL_DIR/bin/trust-playground"
install -m 0644 "$pkg_dir/lib/trust-runtime.so" "$INSTALL_DIR/lib/trust-runtime.so" 2>/dev/null || true
install -m 0644 "$pkg_dir/lib/trust-runtime.a"  "$INSTALL_DIR/lib/trust-runtime.a"  2>/dev/null || true
chown -R "$APP_USER:$APP_USER" "$INSTALL_DIR"
echo "[2/5] installed to $INSTALL_DIR"

# ── 3. Конфиг (playground-секция; токены воркеров добавляются вручную) ──
mkdir -p "$CONF_DIR"
if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<CONF
# trust-playground (балансировщик)
playground.listen=127.0.0.1
playground.port=$APP_PORT
playground.max_queue=256
playground.job_timeout=$TIMEOUT
playground.body_limit_kb=$BODY_LIMIT
playground.rate_limit_per_ip=20
playground.poll_timeout=30
playground.retry=1

# Реестр воркеров: label=token (токен = 64 hex-символа).
# Отозвать воркера = удалить строку и systemctl reload trust-playground.
# worker1=0000000000000000000000000000000000000000000000000000000000000000
CONF
    chown "$APP_USER:$APP_USER" "$CONF"
    echo "[3/5] config written to $CONF (add worker tokens manually)"
else
    echo "[3/5] config already present"
fi

# ── 4. systemd ──
if [ ! -f "$UNIT" ]; then
    sed -e "s/@USER@/$APP_USER/g" "$DEPLOY/trust-playground.service.in" > "$UNIT"
    systemctl daemon-reload
    echo "[4/5] installed systemd unit"
else
    echo "[4/5] systemd unit already present"
fi
systemctl enable trust-playground >/dev/null 2>&1 || true
systemctl restart trust-playground || true

# ── 5. nginx ──
if [ "$WITH_NGINX" -eq 1 ] && [ -n "$DOMAIN" ]; then
    command -v nginx >/dev/null 2>&1 || apt-get install -y nginx >/dev/null
    site="/etc/nginx/sites-available/trust-playground"
    if [ ! -f "$site" ]; then
        sed -e "s/@DOMAIN@/$DOMAIN/g" -e "s/@PORT@/$APP_PORT/g" \
            -e "s/@BODY_LIMIT@/$BODY_LIMIT/g" -e "s/@TIMEOUT@/$TIMEOUT/g" \
            "$DEPLOY/nginx-balancer.conf.in" > "$site"
        ln -sf "$site" /etc/nginx/sites-enabled/trust-playground
        echo "[5/5] nginx site installed for $DOMAIN"
    else
        echo "[5/5] nginx site already present"
    fi
    nginx -t && systemctl reload nginx || true
else
    echo "[5/5] nginx skipped"
fi

echo "Done. Balancer: /opt/trust-playground/bin/trust-playground --playground (systemd: trust-playground)."
echo "Add worker tokens to $CONF, then: systemctl reload trust-playground"
