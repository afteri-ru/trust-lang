---
title: Sandbox (playground)
linkTitle: Sandbox
weight: 55
---

## What is the sandbox

**Sandbox (playground)** is an interactive web page where you can write TrustLang code
right in the browser and immediately see the generated C++ and the transpilation result.
It is a "live" example of the language: an editor with syntax highlighting (Monaco),
an examples dropdown, cross-navigation between the code and C++, and a button to
download the build archive.

The page itself does not compute anything - it sends the code to the **balancer**
(playground), which distributes the task to a free **executor node (worker)**, and that
node performs the Trust → C++ transpilation and returns the result.

## Architecture

The sandbox works as a three-tier system:

```
browser (static page)
        |  POST /run, GET /health, POST /download
        v
balancer (playground)           <-  https://playground.trust-lang.net (public)
        |  worker registry, queue, rate-limit, stats
        v
executor VPS (worker)           <-  connect themselves (outbound connection),
        |                             reverse long-poll for jobs
        v
   trust-lsp --json             (Trust -> C++ transpilation)
```

Key points:

- The **balancer** does not compute anything - it accepts requests, keeps the worker
  registry and dispatches jobs. It listens on `127.0.0.1` locally; nginx exposes it
  publicly over HTTPS.
- **Workers** connect to the balancer themselves (outbound connection - double NAT is
  not a problem), register with a token and run transpilation in an isolated
  subprocess with memory, time and output limits.
- If there are no free workers, the balancer replies "no workers" with a link to this
  page.

## Sandbox status line

At the bottom of the page there is a persistent balancer connectivity indicator:

- 🟢 **balancer online · workers: N** - connected, workers are active;
- 🟠 **balancer online · no workers** - connected, but no free workers;
- 🔴 **no connection to the balancer** - the balancer is unreachable (network failure or the balancer is stopped).

## How to run your own worker

You can help the project and run your own executor node in a few minutes. To do this you need:

- A Linux server (VPS) with `systemd` and outbound TCP access to the balancer (regular internet).
- The `trust-lang-*.tar.gz` distribution (contains `trust-playground` and `trust-lsp`).
- A worker token (64 hex characters) and the balancer URL - issued by the administrator.

### Steps

1. **Get an access token** from the project administrator (<admin@trust-lang.net>). The worker authenticates with this token.

2. **Copy or build the distribution**:
   ```sh
   cmake -B _build && cmake --build _build --target trust-playground trust-lsp
   cmake --build _build --target package
   ```

3. **Run the worker directly** (no root, no install script). It reads settings from
   `trust-playground.conf` next to the binary or from options on the command line:
   ```sh
   ./trust-playground --token <TOKEN> --lsp /path/to/trust-lsp
   ```
   By default the worker connects to `https://playground.trust-lang.net`
   (`worker.playground_url`), and the console shows
   `settings saved to ./trust-playground.conf as defaults` and the worker starts
   with real-time stats. Next time it is enough to just run:
   ```sh
   ./trust-playground
   ```
   Or to work **in the background**:
   ```sh
   nohup ./trust-playground >> trust-playground-worker.log 2>&1 &
   ```
   Stop in the background: `pkill -x trust-playground`.

### Security

- The worker connects to the balancer over HTTPS (`worker.playground_url`); for that
  `curl` must be installed on the host (uses the system TLS stack).
- The token authenticates the worker and without it the balancer rejects requests.
- Transpilation runs in an isolated subprocess with memory, time and output limits.
- At startup the worker validates its settings (`validateWorkerSettings`): the
  `trust-lsp` binary (`worker.lsp_bin`) must exist and be executable, the working
  directory `worker.project_dir` (if set) must exist and be a directory, and
  `worker.lsp_opts` must be valid options. If validation fails, the worker does not
  connect to the balancer and reports the reason.
- The worker token is masked (`<redacted>`) in any result returned to the public
  sandbox, even if it accidentally ends up in the `trust-lsp` output, so the
  confidential token is never displayed to site visitors.

### Balancer and sandbox page protection

The balancer accepts public `/run` and `/download` requests only from the specific
sandbox and throttles automated floods:

- **Domain binding** (`playground.allowed_origins`, `playground.allowed_hosts`): public
  endpoints accept requests only from an allowed Origin/Host; CORS responds with a concrete
  origin instead of `*`. Foreign sites/hotlinking get `403`. By default (empty
  `allowed_origins`) CORS is fail-closed: only loopback origins are allowed (local dev).
- **Rate limit by real IP** (`playground.rate_limit_per_ip`): behind nginx the first
  `X-Forwarded-For` hop is used (trusted only from loopback), so the limit is per visitor,
  not per the whole balancer.
- **Proof-of-work (PoW, `playground.pow_min_difficulty`, disabled by default):** the server
  side is implemented - when enabled the balancer issues a challenge (`GET /challenge`) and
  requires the `X-PoW: nonce:solution` header with a `sha256(nonce+solution)` solution meeting
  the required difficulty; the difficulty is adaptive (grows under load). The client (browser)
  solver on the page is NOT implemented yet - it is a separate task
  (`docs/pow-full-implementation.md`), so this layer stays disabled until then. Protects against
  automated distributed floods.
- **Example cache:** `/run` for an unmodified example is cached by the example file name
  (`X-Example-Name`); when the text changes the name is cleared and the request goes to a
  worker. This reduces worker load for default examples.
- **Admin `/stats`** is available only by token: via the `X-Stats-Token` header (scripts) or
  through the `/stats/login` form with a cookie session (browser). The token is no longer
  passed in the URL (`?token=` removed). The stats page has a "Logout" button
  (`/stats/logout`).

Additionally: the `Content-Disposition` header is sanitized (no header injection), the
`/stats` page escapes HTML (no stored XSS), worker temp files are cleaned at startup and
periodically (disk-overflow protection), and the `trust-lsp` subprocess runs with
`RLIMIT_NOFILE` and `RLIMIT_CORE=0` limits.
