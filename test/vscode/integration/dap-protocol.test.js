#!/usr/bin/env node
/**
 * dap-protocol.test.js — Integration tests for trust-dap DAP server
 *
 * Launches trust-dap as a child process, sends DAP JSON-RPC packets via stdin,
 * reads responses from stdout, and validates the protocol flow.
 *
 * Usage:
 *   node dap-protocol.test.js [--dap-path <path>] [--src <file>] [--cpp <file>] [--target <file>] [--map <file>]
 *
 * Note: trust-dap больше не принимает --source/--cpp/--target/--map через CLI.
 * Пути к файлам передаются через DAP-запрос launch.
 * CLI-аргументы --src/--cpp/--target/--map здесь используются только для тестовых файлов
 * и передаются внутри тела launch-запроса.
 *
 * If arguments are omitted, the test creates temporary test files.
 */

const { spawn } = require('child_process');
const path = require('path');
const fs = require('fs');
const os = require('os');

// ── Test configuration ──
const SUITE_TIMEOUT = 120000; // ms for the entire test suite — kill if hung

// Parse CLI args
function parseArgs() {
    const args = process.argv.slice(2);
    const opts = {};
    for (let i = 0; i < args.length; i++) {
        switch (args[i]) {
            case '--dap-path':
                opts.dapPath = args[++i];
                break;
            case '--src':
                opts.srcFile = args[++i];
                break;
            case '--cpp':
                opts.cppFile = args[++i];
                break;
            case '--binary':
            case '--debuggee-path':
                opts.targetFile = args[++i];
                break;
            case '--map':
                opts.mapFile = args[++i];
                break;
            case '--help':
            case '-h':
                console.log(`Usage: node ${path.basename(process.argv[1])} --src <file> --cpp <file> --binary <file> --map <file>`);
                console.log('');
                console.log('Required:');
                console.log('  --src <file>         Trust .src file');
                console.log('  --cpp <file>         .cpp file');
                console.log('  --binary <file>      Compiled ELF binary');
                console.log('  --map <file>         .map file');
                console.log('');
                console.log('Options:');
                console.log('  --dap-path <path>    Path to trust-dap binary (default: auto-detect)');
                process.exit(0);
        }
    }
    return opts;
}

// ── DAP Protocol helpers ──
let dapSeq = 0;
function nextSeq() { return ++dapSeq; }

function encodeDapPacket(payload) {
    const body = JSON.stringify(payload);
    return `Content-Length: ${Buffer.byteLength(body, 'utf-8')}\r\n\r\n${body}`;
}

function parseDapResponse(data) {
    const packets = [];
    let pos = 0;

    while (pos < data.length) {
        const headerEnd = data.indexOf('\r\n\r\n', pos);
        if (headerEnd === -1) break;

        const header = data.slice(pos, headerEnd).toString();
        const match = header.match(/Content-Length:\s*(\d+)/);
        if (!match) {
            console.error('[PARSE-ERROR] No Content-Length in header:', header);
            pos = headerEnd + 4;
            continue;
        }

        const contentLength = parseInt(match[1], 10);
        const bodyStart = headerEnd + 4;
        const bodyEnd = bodyStart + contentLength;

        if (data.length < bodyEnd) break; // incomplete packet

        const bodyStr = data.slice(bodyStart, bodyEnd).toString();
        try {
            packets.push(JSON.parse(bodyStr));
        } catch (e) {
            console.error('[PARSE-ERROR] JSON parse error:', e.message, 'body:', bodyStr);
        }
        pos = bodyEnd;
    }
    return { packets, remaining: data.slice(pos) };
}

// ── Test framework (simple, no external dependencies) ──
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

// ── DAP Client ──
class DapClient {
    constructor(dapPath, args) {
        this.dapPath = dapPath;
        this.args = args;
        this.process = null;
        this.buffer = Buffer.alloc(0);
        this.packets = [];
        this._waiter = null; // { matchFn, resolve, reject, timer }
        this._processExited = false;
    }

    start() {
        return new Promise((resolve, reject) => {
            this.process = spawn(this.dapPath, this.args, {
                stdio: ['pipe', 'pipe', 'pipe'],
                env: { ...process.env }
            });

            let stderrData = '';
            this.process.stderr.on('data', (data) => {
                stderrData += data.toString();
            });

            this.process.stdout.on('data', (data) => {
                this.buffer = Buffer.concat([this.buffer, data]);
                const { packets, remaining } = parseDapResponse(this.buffer);
                this.buffer = remaining;

                for (const pkt of packets) {
                    this.packets.push(pkt);
                    if (pkt.success === false) {
                        console.log(`    ← FAIL: ${pkt.command || pkt.event}: ${pkt.message || 'no message'}`);
                    }
                }

                // Уведомление waiter'а
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
                    // Если не нашли — waiter продолжает ждать
                }
            });

            let started = false;
            const onError = (err) => {
                if (!started) {
                    started = true;
                    reject(new Error(`Failed to start trust-dap: ${err.message}\nstderr: ${stderrData}`));
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
                    reject(new Error(`trust-dap exited immediately with code ${code}\nstderr: ${stderrData}`));
                }
            });

            // Give it time to start
            setTimeout(() => {
                if (!started) {
                    started = true;
                    resolve();
                }
            }, 200);
        });
    }

    send(command, args = {}) {
        const seq = nextSeq();
        const request = { type: 'request', seq, command, arguments: args };
        const packet = encodeDapPacket(request);
        this.process.stdin.write(packet);
        return seq;
    }

    waitForResponse(requestSeq, timeout = 15000) {
        return this._waitFor(
            p => p.type === 'response' && p.request_seq === requestSeq,
            timeout,
            `response seq=${requestSeq}`
        );
    }

    waitForEvent(eventName, timeout = 15000) {
        return this._waitFor(
            p => p.type === 'event' && p.event === eventName,
            timeout,
            `event ${eventName}`
        );
    }

    _waitFor(matchFn, timeout, label) {
        return new Promise((resolve, reject) => {
            // Сначала проверим уже имеющиеся пакеты
            for (let i = 0; i < this.packets.length; i++) {
                if (matchFn(this.packets[i])) {
                    const pkt = this.packets.splice(i, 1)[0];
                    resolve(pkt);
                    return;
                }
            }

            // Не нашли — регистрируем waiter
            if (this._waiter) {
                reject(new Error(`Concurrent waiter conflict: ${label} (previous waiter still active)`));
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

    kill(signal) {
        if (this.process) {
            try { this.process.kill(signal); } catch (_) {}
        }
    }

    isRunning() {
        return !this._processExited;
    }
}

// ── Helper: run a DAP suite ──
async function runSuites(dapPath, suiteName, dapArgs, fn) {
    const client = new DapClient(dapPath, dapArgs);
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
        // ── Setup test files if not provided ──
        if (!opts.srcFile) {
            tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), 'trust-dap-test-'));
            opts.srcFile = path.join(tmpDir, 'test.src');
            opts.cppFile = path.join(tmpDir, 'test.cpp');
            opts.targetFile = path.join(tmpDir, 'test_elf');
            opts.mapFile = path.join(tmpDir, 'test.map');

            fs.writeFileSync(opts.srcFile, [
                '# Test Source File',
                'fn main() -> int',
                '  let x: int = 42',
                '  let y: int = x + 1',
                '  return y',
                ''
            ].join('\n') + '\n');

            fs.writeFileSync(opts.cppFile, [
                '#include <iostream>',
                'int main() {',
                '    int x = 42;',
                '    int y = x + 1;',
                '    return y;',
                '}',
                ''
            ].join('\n') + '\n');

            // Компилируем реальный ELF-бинарник
            const { spawnSync } = require('child_process');
            const cc = process.env.CC || 'g++';
            const compileResult = spawnSync(cc, [
                '-g', '-O0',
                '-o', opts.targetFile,
                opts.cppFile
            ], { timeout: 30000, encoding: 'utf-8' });

            if (compileResult.status !== 0) {
                console.warn(`  WARNING: compilation failed (${compileResult.stderr.trim() || 'unknown error'}), ` +
                    'creating fake ELF as fallback');
                fs.writeFileSync(opts.targetFile, '# fake\n');
            }

            const mapContent = JSON.stringify({
                version: 1,
                mappings: [
                    { trust: { file: opts.srcFile, line: 3 }, cpp: { file: opts.cppFile, line: 3 } },
                    { trust: { file: opts.srcFile, line: 4 }, cpp: { file: opts.cppFile, line: 4 } }
                ]
            });
            fs.writeFileSync(opts.mapFile, mapContent + '\n');
        } else {
            console.log(`  src: ${opts.srcFile}`);
            console.log(`  cpp: ${opts.cppFile}`);
            console.log(`  map: ${opts.mapFile}`);
            console.log(`  target: ${opts.targetFile}`);
        }

        // Detect trust-dap binary path
        let dapPath = opts.dapPath;
        if (!dapPath) {
            const candidates = [
                path.join(__dirname, '..', '..', '..', 'build', 'src', 'debug', 'trust-dap'),
                path.join(__dirname, '..', '..', '..', 'build', 'debug', 'trust-dap'),
                'trust-dap',
                '/usr/local/bin/trust-dap'
            ];
            for (const c of candidates) {
                try {
                    if (fs.existsSync(c)) {
                        dapPath = c;
                        break;
                    }
                } catch (_) {}
            }
            if (!dapPath) dapPath = 'trust-dap';
        }

        console.log(`  trust-dap: ${dapPath}`);

        // ── Test 1: DAP initialize sequence ──
        await runSuites(dapPath, 'DAP Initialize Sequence', [], async (client) => {
            // initialize
            const initSeq = client.send('initialize', {
                clientID: 'trust-lang-test',
                adapterID: 'trust'
            });
            const initResp = await client.waitForResponse(initSeq);
            test('initialize success=true', () => {
                assert(initResp.success, `initialize failed: ${initResp.message}`);
            });
            test('initialize supportsConfigurationDoneRequest=true', () => {
                assert(initResp.body.supportsConfigurationDoneRequest === true,
                    `got ${initResp.body.supportsConfigurationDoneRequest}`);
            });
            test('initialize supportsDisassemblyRequest=true', () => {
                assert(initResp.body.supportsDisassemblyRequest === true,
                    `got ${initResp.body.supportsDisassemblyRequest}`);
            });

            // launch — пути к файлам передаются в аргументах launch
            const launchSeq = client.send('launch', {
                sourceFile: opts.srcFile,
                cppFile: opts.cppFile,
                targetFile: opts.targetFile,
                mapFile: opts.mapFile
            });
            const launchResp = await client.waitForResponse(launchSeq);
            // launch может завершиться ошибкой (запуск отладки пока не реализован),
            // но сервер должен остаться жив и отвечать на команды
            test('launch responds', () => {
                assert(typeof launchResp.success === 'boolean', 'launch did not respond with success');
            });
            if (!launchResp.success) {
                console.log(`    INFO: launch returned success=false (${launchResp.message}) — continuing test`);
            }

            // setBreakpoints — должен ответить даже без активной debug-сессии
            const bpSeq = client.send('setBreakpoints', {
                source: { path: opts.srcFile, name: path.basename(opts.srcFile) },
                breakpoints: [{ line: 3 }, { line: 4 }, { line: 100 }]
            });
            const bpResp = await client.waitForResponse(bpSeq);
            test('setBreakpoints success=true', () => {
                assert(bpResp.success, `setBreakpoints failed: ${bpResp.message}`);
            });
            test('setBreakpoints returns 3 breakpoints', () => {
                assert(bpResp.body.breakpoints.length === 3,
                    `expected 3, got ${bpResp.body.breakpoints.length}`);
            });

            // configurationDone
            const cfgSeq = client.send('configurationDone', {});
            const cfgResp = await client.waitForResponse(cfgSeq, 10000);
            test('configurationDone success=true', () => {
                assert(cfgResp.success, `configurationDone failed: ${cfgResp.message}`);
            });

            // disconnect
            const discSeq = client.send('disconnect', {});
            const discResp = await client.waitForResponse(discSeq, 10000);
            test('disconnect success=true', () => {
                assert(discResp.success, `disconnect failed: ${discResp.message}`);
            });
        });

        // ── Test 2: DAP breakpointLocations ──
        await runSuites(dapPath, 'DAP Breakpoint Locations', [], async (client) => {
            // launch — paths via launch args
            client.send('launch', {
                sourceFile: opts.srcFile,
                cppFile: opts.cppFile,
                targetFile: opts.targetFile,
                mapFile: opts.mapFile
            });
            const initSeq = client.send('initialize', {});
            await client.waitForResponse(initSeq);

            const locSeq = client.send('breakpointLocations', {
                source: { path: opts.srcFile },
                line: 1,
                endLine: 6
            });
            const locResp = await client.waitForResponse(locSeq);
            test('breakpointLocations success=true', () => {
                assert(locResp.success, `breakpointLocations failed: ${locResp.message}`);
            });
            test('breakpointLocations returns >=3 locations', () => {
                assert(locResp.body.breakpoints.length >= 3,
                    `got ${locResp.body.breakpoints.length}`);
            });
            test('breakpointLocations contains line 3', () => {
                const lines = locResp.body.breakpoints.map(b => b.line);
                assert(lines.includes(3), `line 3 not in: ${JSON.stringify(lines)}`);
            });
        });

        // ── Test 3: DAP getCppFile custom request ──
        await runSuites(dapPath, 'DAP getCppFile Custom Request', [], async (client) => {
            // launch — paths via launch args
            client.send('launch', {
                sourceFile: opts.srcFile,
                cppFile: opts.cppFile,
                targetFile: opts.targetFile,
                mapFile: opts.mapFile
            });
            const initSeq = client.send('initialize', {});
            await client.waitForResponse(initSeq);

            const getCppSeq = client.send('getCppFile', {});
            const getCppResp = await client.waitForResponse(getCppSeq);
            test('getCppFile success=true', () => {
                assert(getCppResp.success, `getCppFile failed: ${getCppResp.message}`);
            });
            test('getCppFile returns correct cppFile', () => {
                assert(getCppResp.body.cppFile === opts.cppFile,
                    `expected ${opts.cppFile}, got ${getCppResp.body.cppFile}`);
            });
            test('getCppFile cppLine is positive integer', () => {
                assert(typeof getCppResp.body.cppLine === 'number' && getCppResp.body.cppLine > 0,
                    `invalid cppLine: ${getCppResp.body.cppLine}`);
            });
        });

        // ── Test 4: DAP unsupported command handling ──
        await runSuites(dapPath, 'DAP Unsupported Commands', [], async (client) => {
            // launch — paths via launch args
            client.send('launch', {
                sourceFile: opts.srcFile,
                cppFile: opts.cppFile,
                targetFile: opts.targetFile,
                mapFile: opts.mapFile
            });
            const initSeq = client.send('initialize', {});
            await client.waitForResponse(initSeq);

            const badSeq = client.send('nonexistentCommand', {});
            const badResp = await client.waitForResponse(badSeq);
            test('unknown command returns success=false', () => {
                assert(badResp.success === false, `expected false, got ${badResp.success}`);
            });
            test('unknown command has unsupported message', () => {
                assert(badResp.message && badResp.message.includes('unsupported'),
                    `message: ${badResp.message}`);
            });
        });

        // ── Test 5: DAP --help ──
        {
            const cp = require('child_process');
            const result = cp.spawnSync(dapPath, ['--help'], { encoding: 'utf-8', timeout: 5000 });
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
        if (tmpDir) {
            try { fs.rmSync(tmpDir, { recursive: true, force: true }); } catch (_) {}
        }
    }

    process.exit(testsFailed > 0 ? 1 : 0);
}

main();