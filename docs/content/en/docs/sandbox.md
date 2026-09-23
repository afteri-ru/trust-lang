---
title: Playground
tags: [getting-started, playground]
linkTitle: Playground
weight: 50
---

## What the playground is

The **playground** is an interactive web page where you can write TrustLang code
directly in the browser and immediately see the generated result of transpilation to C++.
It is a "live" example of the language with a built-in editor and code highlighting, selection of examples from a list,
output of diagnostic messages, cross-navigation between the code and C++, and a button to download the resulting build archive.

## Links to the playground state (URL parameters)

The playground page can be opened directly in the desired state by passing optional parameters
in the URL (query string). All parameters are optional, and without `line` the cursor position is not changed:

- `file=<example file name>` — select a predefined file from the list of examples (combobox);
- `win=src|cppt` — the pane where the cursor is placed (`src` — the source Trust code, `cppt` —
  the generated C++; default is `src`);
- `line=<n>`, `col=<m>` — cursor position (rows/columns are 1-based; `col` defaults to 1);
- `toLine=<n>`, `toCol=<m>` — the end of the selection range (if set — the selection
  from `line:col` to `toLine:toCol`, otherwise just setting the cursor).

**Copying a link**: when navigating through lines, at the bottom of the page in the status bar
the range is shown (`→ cpp: N` / `→ trust: N`) and the link "🔗 copy link",
clicking which copies to the clipboard the URL of the current playground state (the selected file,
pane and cursor position/selection), so it can be shared.

## Playground status bar

The page itself does not compute anything — it sends the code to the **router/balancer**
(playground), which distributes the transpilation task of the source code to a free **execution node
(worker)**, and that node performs the Trust → C++ transpilation, while the result is returned processed to the balancer and displayed in the browser page.

At the bottom of the page a permanent indicator of the connection with the balancer is displayed:

- 🟢 **balancer online · workers: N** — the connection is up, workers are active;
- 🟠 **balancer online · no workers** — the connection is up, but there are no free workers;
- 🔴 **no connection to the balancer** — the balancer is unavailable (network failure or the balancer is stopped).

## How to run your own worker

You can help the project and run your own execution node in a few minutes. To do this you need:

- A Linux server (VPS) with `systemd` and the ability for an outgoing TCP connection to the balancer (ordinary internet).
- The `trust-lang-*.tar.gz` distribution or the `deb` package (contains `trust-playground` and `trust-lsp`).
- A worker token (64 hex characters) — issued by the administrator.

## The playground works on a three-tier scheme

```
browser (static page)
        │  POST /run, GET /health, POST /download
        ▼
balancer (playground)
        │  worker registry, request queue, rate-limit, statistics
        ▼
execution nodes (worker)     ←  connect themselves (outgoing connection),
        │                       reverse long-poll for tasks
        ▼
   trust-lsp --json             ← (local execution of Trust → C++)
```

Key features:

- The **router/balancer** does not perform computations — it only accepts requests, maintains the worker
  registry and dispatches tasks. It listens locally (`127.0.0.1`); nginx exposes it externally over HTTPS.
- **Workers** connect to the balancer themselves (an outgoing connection — double NAT does not interfere),
  register by token and perform transpilation in an isolated subprocess with limits on memory, time and output size.
- If there are no free workers, the balancer responds "no workers" with a link to this instruction.

### Steps

1. **Obtain an access token** from the project administrator (<admin@trust-lang.net>). The worker authenticates with this token.

2. **Copy or build the distribution**:
   ```sh
   cmake -B _build && cmake --build _build --target trust-playground trust-lsp
   cmake --build _build --target package
   ```

3. **Start the worker directly** (without root and without an installation script). It reads the settings from `trust-playground.conf` next to the binary or via command-line options:
   ```sh
   ./trust-playground --token <TOKEN> --lsp /path/to/trust-lsp
   ```
   After that, the message `settings saved to ./trust-playground.conf as defaults` appears in the console
   and the worker starts with real-time operation statistics, and the next time it will be enough to run:
   ```sh
   ./trust-playground
   ```
   Or to run **in the background**:
   ```sh
   nohup ./trust-playground >> trust-playground-worker.log 2>&1 &
   ```
   Stopping in the background: `pkill -x trust-playground`.
