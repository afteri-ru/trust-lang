#!/usr/bin/env bash
# Упаковка VS Code расширения (VSIX).
# Версия и лицензия для пакета берутся АВТОМАТИЧЕСКИ из файла VERSION (проекта) и LGPL,
# и подставляются в package.json и package-lock.json ВРЕМЕННОЙ копии (_build/extension_pkg),
# поэтому имя VSIX всегда соответствует актуальной версии проекта - править include/*.json вручную НЕ нужно.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
[ -n "$VERSION" ] || { echo "[package-extension] VERSION is empty"; exit 1; }

# Короткий хэш коммита для имени артефакта (как в CMake: trust-lang-<ver>-<hash>.vsix).
GIT_HASH="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo 'unknown')"

EXT="$ROOT/include/vscode/extensions/trust-lang"
STAGE="$ROOT/_build/extension_pkg"

echo "[package-extension] project version = $VERSION (git $GIT_HASH)"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -r "$EXT/." "$STAGE/"

# Скопировать TextMate-грамматики (на них ссылается package.json: ./syntaxes/...).
# Без них в VSIX нет подсветки синтаксиса .src.
mkdir -p "$STAGE/syntaxes"
cp -r "$ROOT/include/vscode/syntaxes/." "$STAGE/syntaxes/"

# Тема собирается из базы Default Light+ + Trust-переопределений (самодостаточная, без
# зависимости от `extends` - иначе C++/другие языки монохромны). См. merge_theme.py.
python3 "$ROOT/include/vscode/packager/merge_theme.py" \
    "$EXT/themes/trust-base-light.json" \
    "$EXT/themes/trust-color-theme.src.json" \
    "$STAGE/themes/trust-color-theme.json"

# Подставить актуальную версию/лицензию в package.json и package-lock.json копии.
# include/... не трогаем (они могут быть в любой версии - важна версия из VERSION).
python3 - "$VERSION" "$GIT_HASH" "$STAGE/package.json" "$STAGE/package-lock.json" <<'PY'
import json, sys
ver, hashv, json_path, lock_path = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
full_ver = f"{ver}-{hashv}" if hashv and hashv != 'unknown' else ver

for path in (json_path, lock_path):
    d = json.load(open(path, encoding='utf-8'))
    d['version'] = full_ver
    if '' in d.get('packages', {}):
        d['packages']['']['version'] = full_ver
        d['packages']['']['license'] = 'LGPL'
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(d, f, indent=2, ensure_ascii=False)
        f.write('\n')
PY

# Перенести .vscodeignore, если он есть.
if [ -f "$EXT/.vscodeignore" ]; then
    cp "$EXT/.vscodeignore" "$STAGE/"
fi

# Установить прод-зависимости (vscode-languageclient) в staging-копии, чтобы они попали
# в VSIX. --omit=dev исключает тестовые/инструментальные пакеты.
( cd "$STAGE" && npm install --omit=dev --no-audit --no-fund )

# Паковать БЕЗ --no-dependencies, чтобы node_modules был включён в VSIX.
( cd "$STAGE" && npx --yes @vscode/vsce package )

VSIX="$(ls "$STAGE"/*.vsix 2>/dev/null | head -1)"
if [ -n "$VSIX" ]; then
    # Артефакт остаётся только в build-каталоге (_build/dist, gitignored) и НЕ копируется
    # в include/ - чтобы бинарный VSIX не коммитился в git.
    mkdir -p "$ROOT/_build/dist"
    OUT="$ROOT/_build/dist/trust-lang-${VERSION}-${GIT_HASH}.vsix"
    cp "$VSIX" "$OUT"
    echo "[package-extension] -> $OUT"
fi
