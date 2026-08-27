#!/bin/bash
#
# lib.sh - общая библиотека для .clinerules/hooks/*
#
# Содержит функции для:
#   - парсинга taskId из JSON-входа (taskId = Unix timestamp ms = start time)
#   - форматирования дат
#   - проверки целостности снапшота tasklog и безопасного добавления данных
#
# ВАЖНО: Функции не используют глобальные переменные для передачи внутреннего
# состояния. Каждая функция возвращает результат через stdout или код возврата.
#
# Использование: source "$(dirname "$0")/lib.sh"

TASKLOG_DIR="${TASKLOG_DIR:-.tasklog}"

# ============================================================
# extract_task_id <json_input> <jq_path>
#   Парсит taskId из JSON по указанному jq-пути и выводит его в stdout.
#   taskId является Unix timestamp в миллисекундах (время старта задачи).
#
#   Параметры:
#     json_input - строка с JSON
#     jq_path   - jq-путь к taskId (например, '.taskId')
#
#   Возвращает:
#     stdout: taskId (числовой Unix timestamp ms)
#     return 0 при успехе, 1 при ошибке
# ============================================================
extract_task_id() {
  local input="$1"
  local jq_path="$2"

  if [ -z "$jq_path" ]; then
    echo "[lib.sh] FATAL: extract_task_id: jq_path is required" >&2
    return 1
  fi

  if ! command -v jq &> /dev/null; then
    echo "[lib.sh] FATAL: extract_task_id: jq is not installed" >&2
    return 1
  fi

  local task_id
  task_id=$(echo "$input" | jq -r "$jq_path" 2>/dev/null)

  if [ -z "$task_id" ] || [ "$task_id" = "null" ]; then
    echo "[lib.sh] FATAL: taskId not found in JSON input at path '${jq_path}'" >&2
    return 1
  fi

  echo "$task_id"
}

# ============================================================
# normalize_task_id <task_id>
#   Приводит taskId к числовому виду (старый формат - Unix timestamp ms).
#
#   Cline передаёт taskId в двух форматах:
#     1. Старый: чистое число (например, 1779889607014)
#     2. Новый:   <prefix>_<timestamp_ms>_<suffix> (например, conv_1786187361706_gf6sygz)
#
#   Для нового формата извлекает числовое ядро (timestamp ms), чтобы имя файла
#   и расчёты времени были единообразны со старым форматом (без префикса/суффикса).
#   Для старого формата возвращает значение без изменений.
#
#   Параметры:
#     task_id - сырое значение taskId из JSON
#
#   Возвращает:
#     stdout: нормализованный числовой taskId
#     return 0 при успехе, 1 при ошибке (пустой вход)
# ============================================================
normalize_task_id() {
  local task_id="$1"

  if [ -z "$task_id" ]; then
    echo "[lib.sh] FATAL: normalize_task_id: task_id is empty" >&2
    return 1
  fi

  # Новый формат <prefix>_<timestamp_ms>_<suffix> (напр. conv_1786187361706_gf6sygz).
  # Извлекаем первое числовое ядро из 10-13 цифр (Unix timestamp ms).
  if [[ "$task_id" =~ ([0-9]{10,13}) ]]; then
    echo "${BASH_REMATCH[1]}"
  else
    # Старый формат - число или что-то иное: возвращаем как есть.
    echo "$task_id"
  fi
  return 0
}

# ============================================================
# timestamp_to_iso <timestamp_ms>
#   Преобразует Unix timestamp в миллисекундах в ISO8601 строку.
#
#   Параметры:
#     timestamp_ms - Unix timestamp в миллисекундах
#
#   Возвращает:
#     stdout: строка в формате ISO8601 (например, 2026-05-25T08:43:46Z)
#     return 0 при успехе, 1 при ошибке
# ============================================================
timestamp_to_iso() {
  local ms="$1"

  if [ -z "$ms" ] || [ "$ms" = "null" ]; then
    echo "[lib.sh] FATAL: timestamp_to_iso: timestamp_ms is empty or null" >&2
    return 1
  fi

  local sec=$((ms/1000))
  LC_ALL=C date -u -d @"$sec" +"%Y-%m-%dT%H:%M:%SZ" 2>/dev/null || {
    echo "[lib.sh] FATAL: timestamp_to_iso: failed to convert timestamp $ms" >&2
    return 1
  }
}

# ============================================================
# verify_snapshot_integrity <task_file> <tmp_file>
#   Проверяет, что начало task_file совпадает с содержимым tmp_file (снапшот).
#
#   Если tmp_file не существует - ничего не делает (снапшот ещё не создан),
#   возвращает 0.
#
#   Если начало task_file совпадает с tmp_file - целостность ОК, возвращает 0.
#
#   Если не совпадает - восстанавливает целостность: препендит содержимое
#   tmp_file к task_file, возвращает 0.
#
#   При ошибках - возвращает 1.
#
#   Параметры:
#     task_file - путь к файлу задачи (.tasklog/<taskId>.md)
#     tmp_file  - путь к снапшоту (.tasklog/<taskId>.tmp)
#
#   Возвращает:
#     return 0 при успехе, 1 при ошибке
# ============================================================
verify_snapshot_integrity() {
  local task_file="$1"
  local tmp_file="$2"

  # Если tmp файла нет - снапшот ещё не создан, ничего не делаем
  if [ ! -f "$tmp_file" ]; then
    return 0
  fi

  local head_size
  head_size=$(wc -c < "$tmp_file" 2>/dev/null) || {
    echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to read size of $tmp_file" >&2
    return 1
  }

  # Пустой tmp - удаляем и выходим
  if [ "$head_size" -eq 0 ]; then
    rm -f "$tmp_file"
    return 0
  fi

  # Если task файла нет - создаём из снапшота
  if [ ! -f "$task_file" ]; then
    cp "$tmp_file" "$task_file" || {
      echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to restore $task_file from $tmp_file" >&2
      return 1
    }
    return 0
  fi

  # Читаем первые head_size байт task_file
  local task_head
  task_head=$(dd if="$task_file" bs=1 count="$head_size" 2>/dev/null) || {
    echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to read head of $task_file" >&2
    return 1
  }

  local tmp_content
  tmp_content=$(cat "$tmp_file" 2>/dev/null) || {
    echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to read $tmp_file" >&2
    return 1
  }

  if [ "$task_head" = "$tmp_content" ]; then
    # Снапшот цел - всё в порядке
    return 0
  fi

  # Снапшот не совпадает - препендим его к task_file
  # echo "[lib.sh] verify_snapshot_integrity: snapshot mismatch, prepending to $task_file" >&2
  local new_file
  new_file=$(mktemp "${task_file}.new.XXXXXX") || {
    echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to create temp file" >&2
    return 1
  }

  cat "$tmp_file" "$task_file" > "$new_file" 2>/dev/null || {
    echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to merge files" >&2
    rm -f "$new_file"
    return 1
  }

  mv "$new_file" "$task_file" || {
    echo "[lib.sh] WARNING: verify_snapshot_integrity: failed to mv $new_file to $task_file" >&2
    rm -f "$new_file"
    return 1
  }

  return 0
}

# ============================================================
# update_snapshot <task_file> <tmp_file>
#   Обновляет снапшот: копирует текущее содержимое task_file в tmp_file.
#
#   Вызывается ПОСЛЕ успешной записи в tasklog, чтобы снапшот отражал
#   актуальное состояние.
#
#   Параметры:
#     task_file - путь к файлу задачи (.tasklog/<taskId>.md)
#     tmp_file  - путь к снапшоту (.tasklog/<taskId>.tmp)
#
#   Возвращает:
#     return 0 при успехе, 1 при ошибке
# ============================================================
update_snapshot() {
  local task_file="$1"
  local tmp_file="$2"

  if [ ! -f "$task_file" ]; then
    echo "[lib.sh] WARNING: update_snapshot: $task_file does not exist" >&2
    return 1
  fi

  cp "$task_file" "$tmp_file" || {
    echo "[lib.sh] WARNING: update_snapshot: failed to copy $task_file to $tmp_file" >&2
    return 1
  }

  return 0
}

# ============================================================
# safe_append_to_tasklog <task_file> <tmp_file>
#   Читает данные из stdin и выполняет атомарную операцию:
#   1. verify_snapshot_integrity - проверка целостности перед записью
#   2. append данных из stdin в task_file
#   3. update_snapshot - обновление снапшота после записи
#
#   Использование: echo "data" | safe_append_to_tasklog "$TASK_FILE" "$TMP_FILE"
#   или многострочный pipe.
#
#   Параметры:
#     task_file - путь к файлу задачи (.tasklog/<taskId>.md)
#     tmp_file  - путь к снапшоту (.tasklog/<taskId>.tmp)
#
#   Возвращает:
#     return 0 при успехе, 1 при ошибке
# ============================================================
safe_append_to_tasklog() {
  local task_file="$1"
  local tmp_file="$2"

  # Шаг 1: проверка целостности
  verify_snapshot_integrity "$task_file" "$tmp_file" || {
    echo "[lib.sh] WARNING: safe_append_to_tasklog: verify_snapshot_integrity failed" >&2
    return 1
  }

  # Шаг 2: чтение данных из stdin и добавление в task_file
  local data
  data=$(cat) || {
    echo "[lib.sh] WARNING: safe_append_to_tasklog: failed to read stdin" >&2
    return 1
  }

  echo "$data" >> "$task_file" || {
    echo "[lib.sh] WARNING: safe_append_to_tasklog: failed to append to $task_file" >&2
    return 1
  }

  # Шаг 3: обновление снапшота
  update_snapshot "$task_file" "$tmp_file" || {
    echo "[lib.sh] WARNING: safe_append_to_tasklog: update_snapshot failed" >&2
    return 1
  }

  return 0
}