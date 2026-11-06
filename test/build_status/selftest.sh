# test/build_status/selftest.sh <build_dir> <source_dir>
#
# Контракт обёртки сборки (`trust-build.sh`, шаблон `cmake/trust-build.sh.in`):
#   * именованный сеанс `<name>` кладёт `<name>.{pid,status,log}` в ПОДКАТАЛОГ каталога сборки
#     (`<build>/.trust-build/`), а не в дерево проекта и не в /tmp;
#   * `TRUST_BUILD_STATUS_DIR` переносит каталог статуса;
#   * `--wait <name>` удаляет pid/status и возвращает код сборки;
#   * некорректное имя сеанса и неизвестный сеанс — exit 2;
#   * корень каталога сборки и корень проекта не пополняются файлами сеанса.
#
# Имена сеансов фиксированные (без getpid/счётчиков), временные файлы — только в каталоге сборки
# (см. test/MEMORY.md).

set -euo pipefail

BUILD_DIR="$1"
SOURCE_DIR="$2"
WRAPPER="${BUILD_DIR}/trust-build.sh"

# Каталог статуса по умолчанию проверяем именно как дефолт — внешний override не учитываем.
unset TRUST_BUILD_STATUS_DIR
# Тест запускается изнутри `run_tests`, который уже держит блокировку каталога сборки:
# вложенный вызов обёртки использует СВОЮ блокировку (иначе дедлок).
export TRUST_BUILD_LOCK_FILE="${BUILD_DIR}/test_data/build_status.lock"

fail() {
    echo "FAIL: $*" >&2
    exit 1
}
pass() {
    echo "PASS: $*"
}

[[ -x "${WRAPPER}" ]] || fail "wrapper not found or not executable: ${WRAPPER}"
# Внешний (пользовательский) режим обёртки: внутренние переменные фонового сеанса, унаследованные
# от `run_tests`, заставляют обёртку работать в inner-режиме и глотать вывод.
unset TRUST_BUILD_INNER TRUST_BUILD_LOG TRUST_BUILD_STATUS_FILE

# Намеренно без конвейера: `grep -q` в пайпе с `pipefail` даёт SIGPIPE и ложное падение.
help_output="$("${WRAPPER}" --help 2>&1 || true)"
case "${help_output}" in
    *"--background <name>"*) ;;
    *) fail "--help does not document named sessions (output: ${help_output})" ;;
esac

# Снимок «загрязнения»: файлы сеанса в корне каталога сборки и в корне проекта.
snapshot() {
    {
        find "${BUILD_DIR}" -maxdepth 1 -type f \( -name '*.pid' -o -name '*.status' \) -printf 'build:%f\n'
        find "${SOURCE_DIR}" -maxdepth 1 -type f \( -name '*.pid' -o -name '*.status' \) -printf 'src:%f\n'
    } | sort
}
before="$(snapshot)"

default_dir="${BUILD_DIR}/.trust-build"
override_dir="${BUILD_DIR}/test_data/build_status"
rm -rf "${override_dir}"
# Чистим ТОЛЬКО свои файлы сеанса: каталог `.trust-build` общий — в нём живут сеансы внешней
# сборки (`--background`), и его удаление рвёт их статус/лог. Каталог снимаем лишь если создали его сами.
default_dir_existed=0
[[ -d "${default_dir}" ]] && default_dir_existed=1
cleanup() {
    rm -f "${default_dir}/${session}.pid" "${default_dir}/${session}.status" "${default_dir}/${session}.log"
    if [[ ${default_dir_existed} -eq 0 ]]; then
        rmdir "${default_dir}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

session="build_status_selftest"
out="$("${WRAPPER}" --background "${session}" --target help 2>&1)" || fail "--background failed: ${out}"
[[ -f "${default_dir}/${session}.pid" ]] || fail "pid file is not in ${default_dir}: ${out}"
[[ -f "${default_dir}/${session}.log" ]] || fail "log file is not in ${default_dir}: ${out}"
pass "default status dir is ${default_dir}"

"${WRAPPER}" --wait "${session}" >/dev/null 2>&1 || fail "--wait failed for session '${session}'"
[[ ! -f "${default_dir}/${session}.pid" ]] || fail "--wait did not remove ${session}.pid"
[[ ! -f "${default_dir}/${session}.status" ]] || fail "--wait did not remove ${session}.status"
pass "--wait removes session files"

override_session="build_status_override"
TRUST_BUILD_STATUS_DIR="${override_dir}" "${WRAPPER}" --background "${override_session}" --target help >/dev/null 2>&1 ||
    fail "override: --background failed"
[[ -f "${override_dir}/${override_session}.pid" ]] || fail "override: pid file is not in ${override_dir}"
TRUST_BUILD_STATUS_DIR="${override_dir}" "${WRAPPER}" --wait "${override_session}" >/dev/null 2>&1 ||
    fail "override: --wait failed"
[[ -z "$(find "${override_dir}" -maxdepth 1 \( -name '*.pid' -o -name '*.status' \) -print -quit)" ]] ||
    fail "override: session files left behind in ${override_dir}"
pass "TRUST_BUILD_STATUS_DIR override"

if "${WRAPPER}" --background "bad/name" --target help >/dev/null 2>&1; then
    fail "invalid session name accepted"
fi
pass "invalid session name rejected"

set +e
"${WRAPPER}" --wait "no_such_session" >/dev/null 2>&1
rc=$?
set -e
[[ ${rc} -eq 2 ]] || fail "unknown session must exit 2 (got ${rc})"
pass "unknown session exits 2"

[[ "$(snapshot)" == "${before}" ]] || fail "project/build root polluted by session files"
pass "no session files outside the status dir"

echo "build_status_tests: OK"
