#!/bin/bash
#
# selftest.sh - автономная проверка реализации хуков .clinerules/hooks
#
# Запуск:  bash test/hooks/selftest.sh
#
# Проверка изолирована: хуки пишут в TASKLOG_DIR внутри временного каталога,
# реальный .tasklog/ не затрагивается.
#
# Покрываемые случаи:
#   1. TaskStart идемпотентен (не перезаписывает существующий файл задачи)
#   2. UserPromptSubmit дописывает промпты, не теряя историю
#   3. PostToolUse пишет строки в <taskId>.tools.log
#   4. TaskComplete агрегирует ## Completion report по именам инструментов
#      обоих поколений и считает failures по полю статуса
#   5. PreToolUse: правила git/cmake/grep работают для текущих имён инструментов
#      и для формы parameters.commands
#   6. PreCompact не логирует отсутствующие поля как "null"
#   7. TaskResume создаёт файл задачи, если его нет

set -u

command -v jq >/dev/null 2>&1 || { echo "FATAL: jq не найден (нужен для хуков и теста)"; exit 1; }

HOOKS_DIR="$(cd "$(dirname "$0")/../../.clinerules/hooks" && pwd)"
WORK_DIR="$(mktemp -d)"
export TASKLOG_DIR="${WORK_DIR}/tasklog"
mkdir -p "${TASKLOG_DIR}"

cleanup() { rm -rf "${WORK_DIR}"; }
trap cleanup EXIT

FAILED=0
PASSED=0

pass() { PASSED=$((PASSED + 1)); echo "PASS: $1"; }
fail() { FAILED=$((FAILED + 1)); echo "FAIL: $1"; }

# check_eq <описание> <ожидаемое> <фактическое>
check_eq() {
  if [ "$2" = "$3" ]; then
    pass "$1"
  else
    fail "$1 (ожидалось: '$2', получено: '$3')"
  fi
}

# check_nonempty <описание> <значение>
check_nonempty() {
  if [ -n "$2" ]; then
    pass "$1"
  else
    fail "$1 (значение пустое)"
  fi
}

# check_empty <описание> <значение>
check_empty() {
  if [ -z "$2" ]; then
    pass "$1"
  else
    fail "$1 (ожидалось пусто, получено: '$2')"
  fi
}

# check_contains <описание> <текст> <подстрока>
check_contains() {
  case "$2" in
    *"$3"*) pass "$1" ;;
    *) fail "$1 (нет подстроки '$3')" ;;
  esac
}

# run_hook <имя хука> <json>
run_hook() {
  printf '%s' "$2" | timeout 30 "${HOOKS_DIR}/$1"
}

ID="1700000000000"
TASK_FILE="${TASKLOG_DIR}/${ID}.md"

# --- 1. TaskStart идемпотентен ---------------------------------------------
run_hook TaskStart "{\"taskId\":\"${ID}\"}" >/dev/null
SIZE_BEFORE=$(wc -c < "${TASK_FILE}")
run_hook TaskStart "{\"taskId\":\"${ID}\"}" >/dev/null
SIZE_AFTER=$(wc -c < "${TASK_FILE}")
check_eq "TaskStart идемпотентен: размер файла не меняется" "${SIZE_BEFORE}" "${SIZE_AFTER}"
check_eq "TaskStart: заголовок ровно один" "1" "$(grep -c ' plan taskId=' "${TASK_FILE}")"

# 1b. Пустой существующий файл - заголовок должен быть создан
ID3="1700000000002"
EMPTY_FILE="${TASKLOG_DIR}/${ID3}.md"
: > "${EMPTY_FILE}"
run_hook TaskStart "{\"taskId\":\"${ID3}\"}" >/dev/null
check_eq "TaskStart: пустой файл получает заголовок" "1" "$(grep -c ' plan taskId=' "${EMPTY_FILE}")"

# --- 2. UserPromptSubmit дописывает, не теряя историю ----------------------
run_hook UserPromptSubmit "{\"taskId\":\"${ID}\",\"userPromptSubmit\":{\"prompt\":\"первый промпт\"}}" >/dev/null
run_hook UserPromptSubmit "{\"taskId\":\"${ID}\",\"userPromptSubmit\":{\"prompt\":\"второй промпт\"}}" >/dev/null
check_eq "UserPromptSubmit: оба промпта в логе" "2" "$(grep -c 'UserPromptSubmit:' "${TASK_FILE}")"
check_eq "UserPromptSubmit: 1-й промпт" "первый промпт" \
  "$(grep 'UserPromptSubmit:' "${TASK_FILE}" | sed -n 1p | sed 's/.*UserPromptSubmit: //')"
check_eq "UserPromptSubmit: 2-й промпт" "второй промпт" \
  "$(grep 'UserPromptSubmit:' "${TASK_FILE}" | sed -n 2p | sed 's/.*UserPromptSubmit: //')"
check_eq "UserPromptSubmit: заголовок не потерян" "1" "$(grep -c ' plan taskId=' "${TASK_FILE}")"

# --- 3. PostToolUse пишет .tools.log ---------------------------------------
TOOLS_FILE="${TASKLOG_DIR}/${ID}.tools.log"
for tool in read_files editor run_commands; do
  run_hook PostToolUse "{\"taskId\":\"${ID}\",\"postToolUse\":{\"toolName\":\"${tool}\",\"parameters\":{},\"result\":\"\",\"success\":true,\"executionTimeMs\":5}}" >/dev/null
done
run_hook PostToolUse "{\"taskId\":\"${ID}\",\"postToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{},\"result\":\"boom\",\"success\":false,\"executionTimeMs\":7}}" >/dev/null
check_eq "PostToolUse: 4 строки в .tools.log" "4" "$(wc -l < "${TOOLS_FILE}")"

# --- 4. TaskComplete: агрегация ## Completion report -----------------------
run_hook TaskComplete "{\"taskId\":\"${ID}\",\"taskComplete\":{\"taskMetadata\":{\"taskId\":\"${ID}\",\"ulid\":\"\",\"result\":\"итог\",\"command\":\"\"}}}" >/dev/null
check_eq "TaskComplete: tool_calls" "4" "$(grep -m1 '^- tool_calls:' "${TASK_FILE}" | tr -dc '0-9')"
check_eq "TaskComplete: reads" "1" "$(grep -m1 '^- reads:' "${TASK_FILE}" | tr -dc '0-9')"
check_eq "TaskComplete: writes" "0" "$(grep -m1 '^- writes:' "${TASK_FILE}" | tr -dc '0-9')"
check_eq "TaskComplete: edits" "1" "$(grep -m1 '^- edits:' "${TASK_FILE}" | tr -dc '0-9')"
check_eq "TaskComplete: execs" "2" "$(grep -m1 '^- execs:' "${TASK_FILE}" | tr -dc '0-9')"
check_eq "TaskComplete: failures" "1" "$(grep -m1 '^- failures:' "${TASK_FILE}" | tr -dc '0-9')"
if [ ! -f "${TOOLS_FILE}" ]; then pass "TaskComplete: .tools.log удалён"; else fail "TaskComplete: .tools.log удалён"; fi

# --- 5. PreToolUse ---------------------------------------------------------
out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":[\"git status\"]}}}")
check_eq "PreToolUse: git блокируется (commands, массив)" "true" "$(echo "$out" | jq -r '.cancel')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":\"[\\\"git log\\\"]\"}}}")
check_eq "PreToolUse: git блокируется (commands, JSON-строка)" "true" "$(echo "$out" | jq -r '.cancel')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"execute_command\",\"parameters\":{\"command\":\"git status\"}}}")
check_eq "PreToolUse: git блокируется (старое имя инструмента)" "true" "$(echo "$out" | jq -r '.cancel')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":[\"grep -rn foo src --exclude-dir=.git\"]}}}")
check_eq "PreToolUse: defensive grep с --exclude-dir=.git не блокируется" "false" "$(echo "$out" | jq -r '.cancel')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":[\"cmake --build build\"]}}}")
check_eq "PreToolUse: cmake с посторонним каталогом не блокируется" "false" "$(echo "$out" | jq -r '.cancel')"
check_nonempty "PreToolUse: cmake - предупреждение в errorMessage" "$(echo "$out" | jq -r '.errorMessage')"
check_nonempty "PreToolUse: cmake - контекст CMakeLists.txt добавлен" "$(echo "$out" | jq -r '.contextModification')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":[\"cmake --build _build\"]}}}")
check_eq "PreToolUse: cmake --build _build разрешён" "false" "$(echo "$out" | jq -r '.cancel')"
check_empty "PreToolUse: cmake --build _build без предупреждений" "$(echo "$out" | jq -r '.contextModification')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":[\"grep -rn foo src\"]}}}")
check_eq "PreToolUse: grep без --exclude-dir не блокируется" "false" "$(echo "$out" | jq -r '.cancel')"
check_nonempty "PreToolUse: grep без --exclude-dir - предупреждение" "$(echo "$out" | jq -r '.contextModification')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"run_commands\",\"parameters\":{\"commands\":[\"grep -rn foo src --exclude-dir=_build\"]}}}")
check_empty "PreToolUse: grep с --exclude-dir без предупреждений" "$(echo "$out" | jq -r '.contextModification')"

out=$(run_hook PreToolUse "{\"taskId\":\"${ID}\",\"preToolUse\":{\"toolName\":\"read_files\",\"parameters\":{\"files\":[\"a\"]}}}")
check_eq "PreToolUse: read_files не блокируется" "false" "$(echo "$out" | jq -r '.cancel')"

# --- 6. PreCompact: отсутствующие поля -------------------------------------
run_hook PreCompact "{\"taskId\":\"${ID}\",\"preCompact\":{\"tokensIn\":1500}}" >/dev/null
PC_LINE="$(grep 'PreCompact' "${TASK_FILE}" | tail -1)"
check_contains "PreCompact: tokensIn записан" "${PC_LINE}" "tokensIn=1500"
case "${PC_LINE}" in
  *null*) fail "PreCompact: отсутствующие поля не логируются как null" ;;
  *) pass "PreCompact: отсутствующие поля не логируются как null" ;;
esac

# --- 7. TaskResume ---------------------------------------------------------
ID2="1700000000001"
run_hook TaskResume "{\"taskId\":\"${ID2}\",\"taskResume\":{\"taskMetadata\":{\"taskId\":\"${ID2}\"},\"previousState\":{\"messageCount\":\"7\"}}}" >/dev/null
check_eq "TaskResume: файл создан с заголовком" "1" "$(grep -c ' plan taskId=' "${TASKLOG_DIR}/${ID2}.md")"
check_contains "TaskResume: событие записано" "$(cat "${TASKLOG_DIR}/${ID2}.md")" "Task resumed"

# --- Итог ------------------------------------------------------------------
echo
echo "-----------------------------------------"
echo "PASSED: ${PASSED}, FAILED: ${FAILED}"
if [ "${FAILED}" -ne 0 ]; then
  echo "Проверки хуков НЕ пройдены."
  exit 1
fi
echo "Все проверки хуков пройдены."

