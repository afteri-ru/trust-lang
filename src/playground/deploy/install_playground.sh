#!/usr/bin/env bash
# install_playground.sh - установка/переустановка/удаление балансировщика trust-playground (--playground).
# Идемпотентный. Использование:
#   sudo ./install_playground.sh [options]
#     --domain <d>       Домен для nginx (default: playground.trust-lang.net)
#     --user <u>         Пользователь-владелец сервиса (default: playground)
#     --port <p>         Порт балансировщика на 127.0.0.1 (default: 8080)
#     --dist <dir>       Каталог с дистрибутивом (default: _build/dist)
#     --body-limit <kb>  Лимит тела запроса (default: 256)
#     --timeout <s>      Таймаут задачи (default: 30)
#     --no-nginx         Не настраивать nginx
#     --uninstall        Остановить и удалить сервис, конфиг и установочный каталог
#     --reinstall        Удалить текущую установку и установить заново
#     --keep-data        При удалении/переустановке НЕ удалять конфиг и установочный каталог
#     --help             Справка
set -euo pipefail

DOMAIN="playground.trust-lang.net"
APP_USER="playground"
APP_PORT="8080"
DIST_DIR="_build/dist"
BODY_LIMIT="256"
TIMEOUT="30"
WITH_NGINX=1
DO_UNINSTALL=0
DO_REINSTALL=0
KEEP_DATA=0

# Вывод сообщения об ошибке в stderr.
err() { echo "error: $*" >&2; }

# Ловушка: на любом необработанном сбое выводим явное сообщение с номером строки.
trap 'echo "error: script failed at line $LINENO (exit $?)" >&2' ERR

while [ $# -gt 0 ]; do
    case "$1" in
        --domain)      DOMAIN="$2"; shift 2 ;;
        --user)        APP_USER="$2"; shift 2 ;;
        --port)        APP_PORT="$2"; shift 2 ;;
        --dist)        DIST_DIR="$2"; shift 2 ;;
        --body-limit)  BODY_LIMIT="$2"; shift 2 ;;
        --timeout)     TIMEOUT="$2"; shift 2 ;;
        --no-nginx)    WITH_NGINX=0; shift ;;
        --uninstall)   DO_UNINSTALL=1; shift ;;
        --reinstall)   DO_REINSTALL=1; shift ;;
        --keep-data)   KEEP_DATA=1; shift ;;
        --help)        sed -n '2,15p' "$0"; exit 0 ;;
        *) err "unknown option: $1"; exit 1 ;;
    esac
done

if [ "$(id -u)" -ne 0 ]; then
    err "run as root (sudo)"
    exit 1
fi

INSTALL_DIR="/opt/trust-playground"
CONF_DIR="/etc/trust-playground"
CONF="$CONF_DIR/trust-playground.conf"
UNIT="/etc/systemd/system/trust-playground.service"
SITE="/etc/nginx/sites-available/trust-playground"
SITE_LINK="/etc/nginx/sites-enabled/trust-playground"
DEPLOY="$(cd "$(dirname "$0")" && pwd)"

# -- Удаление: не требует дистрибутива --
uninstall() {
    echo "== uninstalling trust-playground =="
    if systemctl is-active --quiet trust-playground 2>/dev/null; then
        systemctl stop trust-playground || { err "cannot stop trust-playground"; return 1; }
        echo "  [ok] service stopped"
    else
        echo "  [--] service not active (nothing to stop)"
    fi
    systemctl disable trust-playground >/dev/null 2>&1 || true
    if [ -f "$UNIT" ]; then
        rm -f "$UNIT" || { err "cannot remove $UNIT"; return 1; }
        systemctl daemon-reload
        echo "  [ok] systemd unit removed"
    fi
    if [ "$KEEP_DATA" -eq 1 ]; then
        echo "  [--] kept config ($CONF) and install dir ($INSTALL_DIR) per --keep-data"
    else
        if [ -f "$CONF" ]; then rm -f "$CONF" && echo "  [ok] config removed: $CONF"; fi
        rmdir "$CONF_DIR" 2>/dev/null || true
        if [ -d "$INSTALL_DIR" ]; then rm -rf "$INSTALL_DIR" && echo "  [ok] install dir removed: $INSTALL_DIR"; fi
    fi
    if [ -e "$SITE" ]; then
        rm -f "$SITE_LINK" "$SITE"
        echo "  [ok] nginx site removed"
    fi
    echo "== uninstall done =="
}

if [ "$DO_UNINSTALL" -eq 1 ]; then
    uninstall
    exit 0
fi
if [ "$DO_REINSTALL" -eq 1 ]; then
    uninstall
    echo
    echo "== reinstalling trust-playground =="
fi

# -- 0. Дистрибутив --
archive=$(ls -t "$DIST_DIR"/trust-lang-*.tar.gz 2>/dev/null | head -1)
if [ -z "$archive" ]; then
    err "no trust-lang-*.tar.gz in $DIST_DIR"
    exit 1
fi
echo "[1/5] distribution: $archive"

# -- 1. Пользователь --
if ! id "$APP_USER" &>/dev/null; then
    useradd --system --create-home --shell /usr/sbin/nologin "$APP_USER" \
        || { err "cannot create user $APP_USER"; exit 1; }
    echo "[1/5] created user: $APP_USER"
else
    echo "[1/5] user $APP_USER exists"
fi

# -- 2. Бинарники + runtime --
command -v curl >/dev/null 2>&1 || { apt-get update -y >/dev/null && apt-get install -y curl >/dev/null || { err "cannot install curl"; exit 1; }; }
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
tar -xzf "$archive" -C "$tmp" || { err "cannot unpack $archive"; exit 1; }
pkg_dir=$(find "$tmp" -maxdepth 2 -type d -name bin -printf '%h\n' | head -1)
if [ -z "$pkg_dir" ]; then
    err "no bin/ directory found in $archive"
    exit 1
fi
mkdir -p "$INSTALL_DIR/bin" "$INSTALL_DIR/lib" || { err "cannot create $INSTALL_DIR"; exit 1; }
install -m 0755 "$pkg_dir/bin/trust-playground" "$INSTALL_DIR/bin/trust-playground" || { err "cannot install trust-playground"; exit 1; }
install -m 0644 "$pkg_dir/lib/trust-runtime.so" "$INSTALL_DIR/lib/trust-runtime.so" 2>/dev/null || true
install -m 0644 "$pkg_dir/lib/trust-runtime.a"  "$INSTALL_DIR/lib/trust-runtime.a"  2>/dev/null || true
chown -R "$APP_USER:$APP_USER" "$INSTALL_DIR"
echo "[2/5] installed to $INSTALL_DIR"

# -- 3. Конфиг (playground-секция; токены воркеров добавляются вручную) --
mkdir -p "$CONF_DIR" || { err "cannot create $CONF_DIR"; exit 1; }
if [ ! -f "$CONF" ]; then
    cat > "$CONF" <<CONF
# trust-playground (балансировщик)
playground.listen=127.0.0.1
playground.port=$APP_PORT
playground.max_queue=256
playground.job_timeout=$TIMEOUT
playground.body_limit_kb=$BODY_LIMIT
playground.rate_limit_per_ip=20
playground.poll_timeout=15
playground.retry=1

# Раздельные пулы соединений (защита от само-DoS: воркерские long-poll не выедают клиентский путь).
# Выведены из ресурсов: балансировщик 8 ГБ / 8 ядер; воркер 256 ГБ / 64 ядра.
#   max_conns=2048        - глобальный жёсткий кап (потоки/fd), ~2 ГБ на стеки+буферы
#   max_client_conns=1024 - клиентские эндпоинты
#   max_worker_conns=1024 - воркерские; покрывает Σ(max_parallel воркеров) ≈ 16×64 или 32×32
# playground.max_conns=2048
# playground.max_client_conns=1024
# playground.max_worker_conns=1024

# -- Доступ только с конкретной песочницы (доменная привязка) --
# Разрешённые Origin страницы песочницы (через запятую). Пусто = CORS '*'.
# Заполнить в проде, иначе /run и /download принимаются с любого origin.
# playground.allowed_origins=https://trust-lang.net
# Разрешённые Host балансировщика (через запятую). Пусто = Host не проверяется.
# playground.allowed_hosts=playground.trust-lang.net

# -- PoW (анти-бот на /run и /download). 0 = выключен. При угрозе флуда включить. --
# playground.pow_min_difficulty=0
# playground.pow_max_difficulty=24
# playground.pow_nonce_ttl_sec=60
# playground.pow_max_uses_per_nonce=8

# -- Кеш примеров (/run по имени примера X-Example-Name) --
# playground.cache_max_entries=256
# playground.cache_max_mb=64
# playground.cache_ttl_sec=3600

# -- Админ-сессия /stats (cookie, вместо ?token= в URL) --
# playground.stats_session_ttl_sec=600
# playground.stats_session_max_sec=0

# Токен доступа к GET /stats (статистика балансировщика). Сгенерировать:
#   /opt/trust-playground/bin/trust-playground --gen-token
# Пусто - статистика закрыта (403).
# playground.stats_token=

# Реестр воркеров: label=token (токен = 64 hex-символа).
# Сгенерировать токен: /opt/trust-playground/bin/trust-playground --gen-token
# Отозвать воркера = удалить строку и systemctl restart trust-playground.
# worker1=0000000000000000000000000000000000000000000000000000000000000000
CONF
    chown "$APP_USER:$APP_USER" "$CONF" || { err "cannot chown $CONF"; exit 1; }
    echo "[3/5] config written to $CONF (add worker tokens manually)"
else
    echo "[3/5] config already present"
fi

# -- 4. systemd --
if [ ! -f "$UNIT" ]; then
    sed -e "s/@USER@/$APP_USER/g" "$DEPLOY/trust-playground.service.in" > "$UNIT" || { err "cannot write $UNIT"; exit 1; }
    systemctl daemon-reload
    echo "[4/5] installed systemd unit"
else
    echo "[4/5] systemd unit already present"
fi
systemctl enable trust-playground >/dev/null 2>&1 || true
if ! systemctl restart trust-playground; then
    err "systemctl restart trust-playground failed (see 'systemctl status trust-playground')"
    exit 1
fi
echo "[4/5] service restarted"

# -- 5. nginx --
# TLS (HTTPS): балансировщик слушает 127.0.0.1:@APP_PORT@ (loopback), наружу его
# отдаёт nginx по HTTPS. ПЕРЕД установкой nginx-сайта получите сертификат Let's Encrypt:
#   sudo apt-get install -y certbot
#   sudo certbot certonly --webroot -w /var/www/html -d <DOMAIN>
#   # или (если nginx остановлен):  sudo certbot certonly --standalone -d <DOMAIN>
# Сертификат появится в /etc/letsencrypt/live/<DOMAIN>/{fullchain.pem,privkey.pem} -
# именно на эти пути ссылается nginx-balancer.conf.in:
#   ssl_certificate     /etc/letsencrypt/live/<DOMAIN>/fullchain.pem;
#   ssl_certificate_key /etc/letsencrypt/live/<DOMAIN>/privkey.pem;
#   ssl_protocols TLSv1.2 TLSv1.3;
# HTTP (80) отдаёт ACME-челлендж /.well-known/acme-challenge/ и редиректит на HTTPS (443).
# Авто-обновление сертификата:  sudo certbot renew --dry-run
if [ "$WITH_NGINX" -eq 1 ] && [ -n "$DOMAIN" ]; then
    command -v nginx >/dev/null 2>&1 || { apt-get install -y nginx >/dev/null || { err "cannot install nginx"; exit 1; }; }
    CERT_DIR="/etc/letsencrypt/live/$DOMAIN"
    if [ ! -f "$CERT_DIR/fullchain.pem" ]; then
        echo
        echo "TLS: сертификат для $DOMAIN не найден ($CERT_DIR/fullchain.pem)."
        echo "Получите его (Let's Encrypt) и перезапустите скрипт:"
        echo "  sudo apt-get install -y certbot"
        echo "  sudo certbot certonly --webroot -w /var/www/html -d $DOMAIN"
        echo "  # или (если nginx остановлен):  sudo certbot certonly --standalone -d $DOMAIN"
        err "obtain the TLS certificate first, then re-run"
        exit 1
    fi
    if [ ! -f "$SITE" ]; then
        sed -e "s/@DOMAIN@/$DOMAIN/g" -e "s/@PORT@/$APP_PORT/g" \
            -e "s/@BODY_LIMIT@/$BODY_LIMIT/g" -e "s/@TIMEOUT@/$TIMEOUT/g" \
            "$DEPLOY/nginx-balancer.conf.in" > "$SITE" || { err "cannot write $SITE"; exit 1; }
        ln -sf "$SITE" "$SITE_LINK"
        echo "[5/5] nginx site installed for $DOMAIN"
    else
        echo "[5/5] nginx site already present"
    fi
    if ! nginx -t; then
        err "nginx -t failed"
        exit 1
    fi
    systemctl reload nginx || true
else
    echo "[5/5] nginx skipped"
fi

echo "Done. Balancer: /opt/trust-playground/bin/trust-playground --playground (systemd: trust-playground)."
echo "Add worker tokens to $CONF, then: systemctl restart trust-playground"
echo "Remove/reinstall: sudo $0 --uninstall | --reinstall"
