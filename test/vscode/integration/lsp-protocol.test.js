#!/usr/bin/env node
/**
 * lsp-protocol.test.js — Integration tests for trust-lsp LSP server
 *
 * Launches trust-lsp as a child process, sends LSP (JSON-RPC 2.0) packets
 * via stdin, reads responses from stdout, and validates the protocol flow.
 *
 * Usage:
 *   node lsp-protocol.test.js --src <file> [--lsp-path <path>] [--project-dir <dir>]
 *
 * Only --src is required.
 */

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

// ── Test configuration ──
const SUITE_TIMEOUT = 120000;

// Parse CLI args
function parseArgs() {
    const args = process.argv.slice(2);
    const opts = {};
    for (let i = 0; i < args.length; i++) {
        switch (args[i]) {
            case '--lsp-path':
                opts.lspPath = args[++i];
                break;
            case '--src':
                opts.srcFile = args[++i];
                break;
            case '--project-dir':
                opts.projectDir = args[++i];
                break;
            case '--help':
            case '-h':
                console.log(`Usage: node ${path.basename(process.argv[1])} [options]`);
                console.log('Options:');
                console.log('  --lsp-path <path>   Path to trust-lsp binary (default: auto-detect)');
                console.log('  --src <file>        Trust .src file (required)');
                console.log('  --project-dir <dir> Project directory for LSP server');
                process.exit(0);
        }
    }
    return opts;
}

// ── LSP (JSON-RPC 2.0) Protocol helpers ──
let lspId = 0;
function nextId() { return ++lspId; }

function encodeLspPacket(payload) {
    const body = JSON.stringify(payload);
    return `Content-Length: ${Buffer.byteLength(body, 'utf-8')}\r\n\r\n${body}`;
}

function parseLspResponse(data) {
    const packets = [];
    let pos = 0;
    const buf = Buffer.isBuffer(data) ? data : Buffer.from(data);

    while (pos < buf.length) {
        const headerEnd = buf.indexOf('\r\n\r\n', pos);
        if (headerEnd === -1) break;

        const header = buf.slice(pos, headerEnd).toString();
        const match = header.match(/Content-Length:\s*(\d+)/);
        if (!match) {
            console.error('[PARSE-ERROR] No Content-Length in header:', header);
            pos = headerEnd + 4;
            continue;
        }

        const contentLength = parseInt(match[1], 10);
        const bodyStart = headerEnd + 4;
        const bodyEnd = bodyStart + contentLength;

        if (buf.length < bodyEnd) break;

        const bodyStr = buf.slice(bodyStart, bodyEnd).toString();
        try {
            packets.push(JSON.parse(bodyStr));
        } catch (e) {
            console.error('[PARSE-ERROR] JSON parse error:', e.message, 'body:', bodyStr);
        }
        pos = bodyEnd;
    }
    return { packets, remaining: buf.slice(pos) };
}

// ── Test framework ──
let testsPassed = 0;
let testsFailed = 0;

function test(name, fn) {
    try {
        fn();
        testsPassed++;
    } catch (e) {
        testsFailed++;
        console.log(`  FAIL: ${name}: ${e.message}`);
    }
}

function assert(condition, message) {
    if (!condition) throw new Error(message || 'Assertion failed');
}

// ── LSP Client ──
class LspClient {
    constructor(lspPath, args) {
        this.lspPath = lspPath;
        this.args = args;
        this.process = null;
        this.buffer = Buffer.alloc(0);
        this.packets = [];
        this._waiter = null;
        this._processExited = false;
    }

    start() {
        return new Promise((resolve, reject) => {
            this.process = spawn(this.lspPath, this.args, {
                stdio: ['pipe', 'pipe', 'pipe'],
                env: { ...process.env }
            });

            let stderrData = '';
            this.process.stderr.on('data', (data) => {
                stderrData += data.toString();
            });

            this.process.stdout.on('data', (data) => {
                this.buffer = Buffer.concat([this.buffer, data]);
                const { packets, remaining } = parseLspResponse(this.buffer);
                this.buffer = remaining;

                for (const pkt of packets) {
                    this.packets.push(pkt);
                }

                if (this._waiter) {
                    const w = this._waiter;
                    for (let i = 0; i < this.packets.length; i++) {
                        if (w.matchFn(this.packets[i])) {
                            clearTimeout(w.timer);
                            const pkt = this.packets.splice(i, 1)[0];
                            this._waiter = null;
                            w.resolve(pkt);
                            return;
                        }
                    }
                }
            });

            let started = false;
            const onError = (err) => {
                if (!started) {
                    started = true;
                    reject(new Error(`Failed to start trust-lsp: ${err.message}\nstderr: ${stderrData}`));
                }
            };

            this.process.on('error', onError);
            this.process.on('exit', (code) => {
                this._processExited = true;
                if (this._waiter) {
                    clearTimeout(this._waiter.timer);
                    this._waiter.reject(new Error(`Process exited with code ${code} while waiting`));
                    this._waiter = null;
                }
                if (!started) {
                    started = true;
                    reject(new Error(`trust-lsp exited immediately with code ${code}\nstderr: ${stderrData}`));
                }
            });

            setTimeout(() => {
                if (!started) {
                    started = true;
                    resolve();
                }
            }, 200);
        });
    }

    sendRequest(method, params = {}) {
        const id = nextId();
        const request = { jsonrpc: '2.0', id, method, params };
        this.process.stdin.write(encodeLspPacket(request));
        return id;
    }

    sendNotification(method, params = {}) {
        const notification = { jsonrpc: '2.0', method, params };
        this.process.stdin.write(encodeLspPacket(notification));
    }

    waitForResponse(requestId, timeout = 15000) {
        return this._waitFor(
            p => p.id === requestId,
            timeout,
            `response id=${requestId}`
        );
    }

    waitForNotification(method, timeout = 15000) {
        return this._waitFor(
            p => p.method === method,
            timeout,
            `notification ${method}`
        );
    }

    _waitFor(matchFn, timeout, label) {
        return new Promise((resolve, reject) => {
            for (let i = 0; i < this.packets.length; i++) {
                if (matchFn(this.packets[i])) {
                    const pkt = this.packets.splice(i, 1)[0];
                    resolve(pkt);
                    return;
                }
            }

            if (this._waiter) {
                reject(new Error(`Concurrent waiter conflict: ${label}`));
                return;
            }

            const timer = setTimeout(() => {
                if (this._waiter && this._waiter.reject === reject) {
                    this._waiter = null;
                    reject(new Error(`Timeout ${label}: no matching packet in ${timeout}ms`));
                }
            }, timeout);

            this._waiter = { matchFn, resolve, reject, timer };
        });
    }

    stop() {
        if (this._waiter) {
            clearTimeout(this._waiter.timer);
            this._waiter = null;
        }
        if (this.process) {
            try { this.process.stdin.end(); } catch (_) {}
            try { this.process.kill('SIGTERM'); } catch (_) {}
        }
    }

    isRunning() {
        return !this._processExited;
    }
}

// ── Helper: run an LSP suite ──
async function runSuite(lspPath, suiteName, lspArgs, fn) {
    const client = new LspClient(lspPath, lspArgs);
    await client.start();
    try {
        await fn(client);
    } finally {
        client.stop();
    }
}

// ── Main test suite ──
async function main() {
    const opts = parseArgs();
    let tmpDir = null;

    try {
        // ── Validate required args ──
        if (!opts.srcFile) {
            console.error('Error: --src is required');
            console.error('Usage: node lsp-protocol.test.js --src <file> [--lsp-path <path>]');
            process.exit(1);
        }
        // Use srcFile directory as project dir
        tmpDir = path.dirname(opts.srcFile);

        // Определяем projectDir для LSP сервера
        const projectDir = opts.srcFile ? path.dirname(opts.srcFile) : tmpDir;
        console.log(`  project-dir: ${projectDir}`);
        console.log(`  src: ${opts.srcFile}`);

        // Detect trust-lsp binary
        let lspPath = opts.lspPath;
        if (!lspPath) {
            const candidates = [
                path.join(__dirname, '..', '..', '..', 'build', 'trust-lsp'),
                path.join(__dirname, '..', '..', '..', 'build', 'src', 'lsp', 'trust-lsp'),
                'trust-lsp',
                '/usr/local/bin/trust-lsp'
            ];
            for (const c of candidates) {
                try {
                    if (fs.existsSync(c)) {
                        lspPath = c;
                        break;
                    }
                } catch (_) {}
            }
            if (!lspPath) lspPath = 'trust-lsp';
        }

        console.log(`  trust-lsp: ${lspPath}`);

        // Общие аргументы LSP сервера (--project-dir)
        const lspArgs = ['--project-dir', projectDir];

        // ── Test 1: LSP initialize sequence ──
        await runSuite(lspPath, 'LSP Initialize Sequence', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: {
                    textDocument: {
                        definition: { dynamicRegistration: false },
                        hover: { dynamicRegistration: false }
                    }
                }
            });
            const initResp = await client.waitForResponse(initId);
            test('initialize responds', () => {
                assert(initResp != null, 'no response');
            });
            test('initialize has capabilities', () => {
                assert(initResp.result && initResp.result.capabilities,
                    `expected capabilities, got ${JSON.stringify(initResp)}`);
            });
            test('initialize supports definitionProvider', () => {
                assert(initResp.result.capabilities.definitionProvider === true);
            });
            test('initialize supports hoverProvider', () => {
                assert(initResp.result.capabilities.hoverProvider === true);
            });

            // initialized notification
            client.sendNotification('initialized', {});
        });

        // ── Test 2: LSP textDocument/didOpen + definition ──
        await runSuite(lspPath, 'LSP Definition Provider', lspArgs, async (client) => {
            // Initialize
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: {
                    textDocument: {
                        definition: { dynamicRegistration: false },
                        hover: { dynamicRegistration: false }
                    }
                }
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            // didOpen
            client.sendNotification('textDocument/didOpen', {
                textDocument: {
                    uri: `file://${opts.srcFile}`,
                    languageId: 'trust',
                    version: 1,
                    text: fs.readFileSync(opts.srcFile, 'utf-8')
                }
            });

            // Request definition at line 2 (let x: int = 42)
            const defId = client.sendRequest('textDocument/definition', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 1, character: 9 }
            });
            const defResp = await client.waitForResponse(defId);
            test('definition responds', () => {
                assert(defResp != null, 'no response');
            });
            test('definition has result', () => {
                assert(defResp.result != null, 'expected result');
            });
            test('definition points to cpp file', () => {
                const loc = Array.isArray(defResp.result) ? defResp.result[0] : defResp.result;
                assert(loc.uri.includes('.cpp'), `expected cpp file, got ${loc.uri}`);
            });
        });

        // ── Test 3: LSP textDocument/hover ──
        await runSuite(lspPath, 'LSP Hover', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: {
                    textDocument: {
                        definition: { dynamicRegistration: false },
                        hover: { dynamicRegistration: false }
                    }
                }
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            client.sendNotification('textDocument/didOpen', {
                textDocument: {
                    uri: `file://${opts.srcFile}`,
                    languageId: 'trust',
                    version: 1,
                    text: fs.readFileSync(opts.srcFile, 'utf-8')
                }
            });

            const hoverId = client.sendRequest('textDocument/hover', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 1, character: 9 }
            });
            const hoverResp = await client.waitForResponse(hoverId);
            test('hover responds', () => {
                assert(hoverResp != null, 'no response');
            });
            test('hover has contents', () => {
                assert(hoverResp.result && hoverResp.result.contents,
                    `expected contents, got ${JSON.stringify(hoverResp)}`);
            });
            test('hover shows C++ mapping info', () => {
                const contents = hoverResp.result.contents;
                const text = typeof contents === 'string' ? contents :
                    (contents.value || JSON.stringify(contents));
                assert(text.includes('.cpp') || text.includes('C++') || text.includes('test.cpp'),
                    `expected C++ reference, got: ${text}`);
            });
        });

        // ── Test 4: LSP diagnostics on open with non-existent file ──
        await runSuite(lspPath, 'LSP Diagnostics', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: {
                    textDocument: {
                        definition: { dynamicRegistration: false },
                        hover: { dynamicRegistration: false }
                    }
                }
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            // Open a non-existent file → should trigger diagnostics
            const badUri = `file://${tmpDir}/nonexistent.src`;
            client.sendNotification('textDocument/dialOpen', { // intentionally typo'd — silent ignore
            });
            client.sendNotification('textDocument/didOpen', {
                textDocument: {
                    uri: badUri,
                    languageId: 'trust',
                    version: 1,
                    text: 'let x: int = 42'
                }
            });

            const diagNotification = await client.waitForNotification('textDocument/publishDiagnostics', 10000);
            test('publishDiagnostics notification received', () => {
                assert(diagNotification != null, 'no publishDiagnostics notification');
            });
            test('publishDiagnostics has uri', () => {
                assert(diagNotification.params && diagNotification.params.uri,
                    `expected uri, got ${JSON.stringify(diagNotification)}`);
            });
            test('publishDiagnostics has diagnostics array', () => {
                assert(Array.isArray(diagNotification.params.diagnostics),
                    `expected diagnostics array, got ${JSON.stringify(diagNotification)}`);
            });
            test('publishDiagnostics contains at least one diagnostic', () => {
                assert(diagNotification.params.diagnostics.length > 0,
                    'expected at least one diagnostic');
            });
            test('publishDiagnostics has severity=1 (Error)', () => {
                assert(diagNotification.params.diagnostics[0].severity === 1,
                    `expected severity 1, got ${diagNotification.params.diagnostics[0].severity}`);
            });
            test('publishDiagnostics source is trust-lsp', () => {
                assert(diagNotification.params.diagnostics[0].source === 'trust-lsp',
                    `expected trust-lsp source, got ${diagNotification.params.diagnostics[0].source}`);
            });
            test('publishDiagnostics has message', () => {
                const msg = diagNotification.params.diagnostics[0].message;
                assert(msg && msg.length > 0, `expected non-empty message, got "${msg}"`);
            });
        });

        // ── Test 6: LSP shutdown ──
        await runSuite(lspPath, 'LSP Shutdown', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: {}
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            const shutdownId = client.sendRequest('shutdown', {});
            const shutdownResp = await client.waitForResponse(shutdownId);
            test('shutdown responds', () => {
                assert(shutdownResp != null, 'no response');
            });

            client.sendNotification('exit', {});

            // Даем процессу время завершиться
            await new Promise(resolve => setTimeout(resolve, 500));
            test('process exited after shutdown', () => {
                assert(!client.isRunning() || client.process.killed,
                    'expected process to exit');
            });
        });

        // ── Test 7: LSP --help ──
        {
            const cp = require('child_process');
            const result = cp.spawnSync(lspPath, ['--help'], { encoding: 'utf-8', timeout: 5000 });
            test('--help exit code 0', () => {
                assert(result.status === 0, `exit=${result.status}`);
            });
            test('--help shows usage', () => {
                const output = result.stdout + result.stderr;
                assert(output.includes('Usage'), `expected Usage in: ${output}`);
            });
        }

        if (testsFailed === 0) {
            console.log(`  All ${testsPassed} tests passed`);
        } else {
            console.log(`  FAILED: ${testsFailed}/${testsPassed + testsFailed} tests`);
        }

    } catch (err) {
        console.error('[FATAL] Test suite error:', err.message);
        console.error(err.stack);
        testsFailed++;
    } finally {
        // Don't clean up tmpDir — it points to pre-existing test data directory
    }

    process.exit(testsFailed > 0 ? 1 : 0);
}

main();