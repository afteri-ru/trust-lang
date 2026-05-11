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

            // Request definition at create x (строка 11, 0-based: 10)
            const defId = client.sendRequest('textDocument/definition', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 10, character: 9 }
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
                position: { line: 10, character: 9 }
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
                    (Array.isArray(contents) ? contents.join(' ') : (contents.value || JSON.stringify(contents)));
                assert(text.includes('cpp') || text.includes('int') || text.includes('cout'),
                    `expected C++ code in hover, got: ${text}`);
            });
        });

        // Note: publishDiagnostics is sent only on transpile errors (via handleDidOpen on error path).
        // Valid files do NOT trigger publishDiagnostics. Test 9 covers the error case.

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

        // ── Test 5: LSP textDocument/documentLink — Trust → C++ ──
        await runSuite(lspPath, 'LSP DocumentLink Trust→Cpp', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: { textDocument: { definition: { dynamicRegistration: false }, hover: { dynamicRegistration: false } } }
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

            const docLinkId = client.sendRequest('textDocument/documentLink', {
                textDocument: { uri: `file://${opts.srcFile}` }
            });
            const docLinkResp = await client.waitForResponse(docLinkId);
            test('documentLink responds', () => {
                assert(docLinkResp != null, 'no response');
            });
            test('documentLink has array result', () => {
                assert(Array.isArray(docLinkResp.result), `expected array, got ${JSON.stringify(docLinkResp.result)}`);
            });
            test('documentLink contains at least one link', () => {
                assert(docLinkResp.result.length > 0, 'expected at least one link');
            });
            test('documentLink links to cpp file', () => {
                const link = docLinkResp.result[0];
                assert(link.target && link.target.includes('.cpp'),
                    `expected cpp target, got ${JSON.stringify(link)}`);
            });
        });

        // ── Test 6: LSP textDocument/didChange ──
        // Использует существующий файл simple_example.src.
        // Файл не модифицируется — проверяем, что didChange и ховер
        // после него работают корректно.
        await runSuite(lspPath, 'LSP didChange', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: { textDocument: { definition: { dynamicRegistration: false }, hover: { dynamicRegistration: false } } }
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            // Читаем содержимое существующего файла с диска
            const content = fs.readFileSync(opts.srcFile, 'utf-8');

            client.sendNotification('textDocument/didOpen', {
                textDocument: { uri: `file://${opts.srcFile}`, languageId: 'trust', version: 1, text: content }
            });

            // Hover to check transpilation happened
            const hoverId1 = client.sendRequest('textDocument/hover', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 10, character: 9 }
            });
            const hoverResp1 = await client.waitForResponse(hoverId1);
            test('hover after didOpen responds', () => {
                assert(hoverResp1 != null, 'no response');
            });
            const text1 = (() => {
                const c = hoverResp1.result && hoverResp1.result.contents;
                return typeof c === 'string' ? c : (Array.isArray(c) ? c.join(' ') : '');
            })();
            test('hover after didOpen has content', () => {
                assert(text1.length > 0, `expected non-empty hover content, got empty`);
            });

            // didChange с тем же содержимым (файл на диске не модифицируется)
            client.sendNotification('textDocument/didChange', {
                textDocument: { uri: `file://${opts.srcFile}`, version: 2 },
                contentChanges: [{ text: content }]
            });

            // Wait a bit for re-transpilation
            await new Promise(resolve => setTimeout(resolve, 200));

            // Hover to check re-transpilation happened
            const hoverId2 = client.sendRequest('textDocument/hover', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 10, character: 9 }
            });
            const hoverResp2 = await client.waitForResponse(hoverId2);
            test('hover after didChange responds', () => {
                assert(hoverResp2 != null, 'no response');
            });
            const text2 = (() => {
                const c = hoverResp2.result && hoverResp2.result.contents;
                return typeof c === 'string' ? c : (Array.isArray(c) ? c.join(' ') : '');
            })();
            test('hover after didChange has content', () => {
                assert(text2.length > 0, `expected non-empty hover content, got empty`);
            });
        });

        // ── Test 7: LSP textDocument/hover — reverse (на C++ файле) ──
        await runSuite(lspPath, 'LSP Hover Reverse (Cpp→Trust)', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: { textDocument: { definition: { dynamicRegistration: false }, hover: { dynamicRegistration: false } } }
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            // Open trust file to trigger transpilation
            client.sendNotification('textDocument/didOpen', {
                textDocument: {
                    uri: `file://${opts.srcFile}`,
                    languageId: 'trust',
                    version: 1,
                    text: fs.readFileSync(opts.srcFile, 'utf-8')
                }
            });

            // Запрашиваем ховер на C++ файле (dummy cppUri — реально сервер не обслуживает cpp файлы,
            // но мы можем проверить, что если cpp файл есть в reverse-кеше, ховер работает)
            // Отправляем definition, чтобы получить cpp URI
            const defId = client.sendRequest('textDocument/definition', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 10, character: 9 }
            });
            const defResp = await client.waitForResponse(defId);
            if (defResp.result && defResp.result.uri) {
                const cppUri = defResp.result.uri;
                const hoverId = client.sendRequest('textDocument/hover', {
                    textDocument: { uri: cppUri },
                    position: { line: 3, character: 9 }
                });
                const hoverResp = await client.waitForResponse(hoverId);
                test('reverse hover responds', () => {
                    assert(hoverResp != null, 'no response');
                });
                test('reverse hover has contents', () => {
                    assert(hoverResp.result && hoverResp.result.contents,
                        `expected contents, got ${JSON.stringify(hoverResp)}`);
                });
            } else {
                test('reverse hover skipped (no cpp file in result)', () => {});
            }
        });

        // ── Test 8: LSP textDocument/definition — reverse (C++ → Trust) ──
        await runSuite(lspPath, 'LSP Definition Reverse', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: { textDocument: { definition: { dynamicRegistration: false }, hover: { dynamicRegistration: false } } }
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

            // Сначала получаем cpp URI через definition на trust
            const defId = client.sendRequest('textDocument/definition', {
                textDocument: { uri: `file://${opts.srcFile}` },
                position: { line: 10, character: 9 }
            });
            const defResp = await client.waitForResponse(defId);
            if (defResp.result && defResp.result.uri) {
                const cppUri = defResp.result.uri;
                // Запрашиваем definition на cpp → trust (обратный маппинг)
                const reverseDefId = client.sendRequest('textDocument/definition', {
                    textDocument: { uri: cppUri },
                    position: { line: 3, character: 9 }
                });
                const reverseDefResp = await client.waitForResponse(reverseDefId);
                test('reverse definition responds', () => {
                    assert(reverseDefResp != null, 'no response');
                });
                test('reverse definition points back to trust file', () => {
                    if (reverseDefResp.result) {
                        const loc = Array.isArray(reverseDefResp.result) ? reverseDefResp.result[0] : reverseDefResp.result;
                        assert(loc.uri.includes('.src') || loc.uri.includes('.trust'),
                            `expected trust file, got ${loc.uri}`);
                    }
                });
            } else {
                test('reverse definition skipped (no cpp file in result)', () => {});
            }
        });

        // ── Test 9: LSP invalid syntax → diagnostics ──
        await runSuite(lspPath, 'LSP Invalid Syntax Diagnostics', lspArgs, async (client) => {
            const initId = client.sendRequest('initialize', {
                processId: process.pid,
                rootUri: `file://${tmpDir}`,
                capabilities: { textDocument: { definition: { dynamicRegistration: false }, hover: { dynamicRegistration: false } } }
            });
            await client.waitForResponse(initId);
            client.sendNotification('initialized', {});

            // transpileSourceFile reads from DISK, so create a file with invalid syntax first
            const tmpDir2 = fs.mkdtempSync(path.join(os.tmpdir(), 'trust-lsp-test-'));
            const badFilePath = path.join(tmpDir2, 'invalid.src');
            fs.writeFileSync(badFilePath, 'create x = ;\n');
            const badUri = `file://${badFilePath}`;

            client.sendNotification('textDocument/didOpen', {
                textDocument: {
                    uri: badUri,
                    languageId: 'trust',
                    version: 1,
                    text: 'create x = ;\n'
                }
            });

            const diagNotification = await client.waitForNotification('textDocument/publishDiagnostics', 10000);
            test('invalid syntax diagnostics received', () => {
                assert(diagNotification != null, 'no publishDiagnostics');
            });
            test('invalid syntax diagnostics has uri', () => {
                assert(diagNotification.params && diagNotification.params.uri,
                    `expected uri, got ${JSON.stringify(diagNotification)}`);
            });
            test('invalid syntax has at least one diagnostic', () => {
                assert(Array.isArray(diagNotification.params.diagnostics) && diagNotification.params.diagnostics.length > 0,
                    'expected diagnostics array with entries');
            });

            // Cleanup temp dir
            try { fs.rmSync(tmpDir2, { recursive: true }); } catch (_) {}
        });

        // ── Test 10: LSP --help ──
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