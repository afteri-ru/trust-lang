---
title: Запустить свой узел-исполнитель (worker)
linkTitle: Host a worker
weight: 60
---

Playground TrustLang работает по трёхуровневой схеме: статический сайт →
балансировщик (playground) → множество **исполнительных VPS (worker)**. Воркеры
сами подключаются к балансировщику (исходящее соединение — двойной NAT не мешает),
регистрируются и выполняют транспиляцию Trust → C++. Когда свободных воркеров нет,
playground отвечает «нет воркеров» со ссылкой на эту инструкцию.

Вы можете помочь проекту и запустить свой узел-исполнитель за несколько минут.

## Требования

- Linux-сервер (VPS) с `systemd` и возможностью исходящего TCP-соединения к
  балансировщику (обычный интернет).
- Дистрибутив `trust-lang-*.tar.gz` (содержит `trust-playground` и `trust-lsp`).
- Токен воркера (64 hex-символа) и URL балансировщика — выдаются администратором.

## Шаги

1. **Получите токен** у администратора playground (`label` + токен). Воркер
   аутентифицируется этим токеном; при отзыве токена доступ прекращается.

2. **Скопируйте и соберите дистрибутив** (или запросите у администратора):
   ```sh
   cmake -B _build && cmake --build _build --target trust-playground trust-lsp
   cmake --build _build --target package
   ```

3. **Запустите воркер напрямую** (без root и без скрипта установки). Он читает
   настройки из `trust-playground.conf` рядом с бинарником; если файла нет —
   укажите обязательные опции в командной строке, и они **сохранятся в файл
   как настройки по умолчанию**:
   ```sh
   ./trust-playground --playground-url https://playground.trust-lang.net \
                      --token <ТОКЕН> --max-parallel "$(nproc)" --lsp /path/to/trust-lsp
   ```
   В консоли появится сообщение `settings saved to ./trust-playground.conf as defaults`
   и запустится воркер (статистика в реальном времени). В следующий раз достаточно:
   ```sh
   ./trust-playground
   ```
   Или **в фоне**:
   ```sh
   nohup ./trust-playground >> trust-playground-worker.log 2>&1 &
   ```
   Остановка в фоне: `pkill -x trust-playground`. systemd/root не используются.

   В консоли воркер периодически выводит статистику:
   `uptime`, занятые слоты, число выполненных/упавших задач, состояние
   соединения с балансировщиком (`connected=yes/no`).

## Настройка и лимиты

Параметры воркера редактируются в `trust-playground.conf` рядом с бинарником
(или через CLI, затем `--save-config`), секция `worker.`:

```
worker.playground_url=https://playground.trust-lang.net
worker.token=<ТОКЕН>
worker.lsp_bin=/opt/trust-playground/bin/trust-lsp
worker.max_parallel=8        # параллельных задач (обычно = число ядер)
worker.max_memory_mb=512     # лимит памяти на задачу
worker.max_output_kb=2048    # лимит результата
worker.job_timeout=30        # таймаут одной транспиляции
worker.stats_interval_ms=10000  # период вывода статистики
worker.project_name=my-project  # имя проекта — в имени скачиваемого build-архива (default: trust-project)
worker.lsp_opts=-Wsigil=ignore  # доп. опции, всегда передаваемые в trust-lsp (--json), пробрасываются в pipeline
```

Имя скачиваемого build-архива (`<project>.tar.gz`) берётся из `worker.project_name`:
если опция не задана — используется `trust-project.tar.gz`.

После правки конфига — перезапустите воркер (Ctrl+C / перезапуск в фоне).

## Безопасность

- Воркер подключается к балансировщику по HTTPS (`worker.playground_url`); для этого
  на хосте должен быть установлен `curl` (используется системный TLS-стек).
- Токен аутентифицирует воркера; без токена балансировщик отклоняет запросы.
- Транспиляция выполняется в изолированном субпроцессе с ограничениями памяти,
  времени и размера вывода.
