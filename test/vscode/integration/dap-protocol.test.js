#!/usr/bin/env node
/**
 * dap-protocol.test.js — Integration tests for trust-dap DAP server
 *
 * Launches trust-dap as a child process, sends DAP JSON-RPC packets via stdin,
 * reads responses from stdout, and validates the protocol flow.
 */

const { spawn, spawnSync } = require('child_process');
const path = require('path');

// ── Test configuration ──
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

function encodeDapPacket(payload) {
    const body = JSON.stringify(payload);
    return `Content-Length: ${Buffer.byteLength(body, 'utf-8')}\r\n\r\n${body}`;
}

// Accept --dap-path from CLI
function cliArg(name) {
    const idx = process.argv.indexOf(name);
    return idx >= 0 && idx + 1 < process.argv.length ? process.argv[idx + 1] : null;
}
const dapPath = cliArg('--dap-path') || path.resolve(__dirname, '..', '..', '..', 'build', 'trust-dap');

function runTrustDap(packets, timeoutMs = 10000) {
    return new Promise((resolve, reject) => {
        const p = spawn(dapPath, [], { stdio: ['pipe', 'pipe', 'pipe'] });
        let stderr = '';
        let stdout = '';
        let done = false;

        p.stderr.on('data', d => { stderr += d.toString(); });
        p.stdout.on('data', d => { stdout += d.toString(); });

        const timer = setTimeout(() => {
            if (!done) {
                done = true;
                p.kill('SIGTERM');
                reject(new Error(`Timeout\nstdout: ${JSON.stringify(stdout)}\nstderr: ${JSON.stringify(stderr)}`));
            }
        }, timeoutMs);

        p.on('error', (err) => {
            if (!done) {
                done = true;
                clearTimeout(timer);
                reject(new Error(`Spawn error: ${err.message}`));
            }
        });

        p.on('exit', (code) => {
            if (!done) {
                done = true;
                clearTimeout(timer);
                resolve({ stdout, stderr, code });
            }
        });

        // Send packets sequentially with delays
        let idx = 0;
        function sendNext() {
            if (idx >= packets.length) {
                p.stdin.end();
                return;
            }
            const pkt = packets[idx++];
            p.stdin.write(encodeDapPacket(pkt));
            setTimeout(sendNext, 500);
        }
        setTimeout(() => {
            try {
                sendNext();
            } catch (e) {
                if (!done) {
                    done = true;
                    clearTimeout(timer);
                    reject(e);
                }
            }
        }, 500);
    });
}

// ── Tests ──

async function testInitialize() {
    const { stdout, stderr } = await runTrustDap([
        { type: 'request', seq: 1, command: 'initialize', arguments: { clientID: 'test', adapterID: 'trust' } },
        { type: 'request', seq: 2, command: 'setBreakpoints', arguments: {
            source: { path: 'simple_example.src' },
            breakpoints: [{ line: 3 }]
        }},
        { type: 'request', seq: 3, command: 'disconnect', arguments: {} }
    ]);

    console.log(`  stderr: ${stderr.replace(/\n/g, '\\n')}`);

    test('initialize success=true', () => {
        assert(stdout.includes('"command":"initialize"') && stdout.includes('"success":true'),
            `Missing initialize response in: ${stdout}`);
    });

    test('setBreakpoints responds', () => {
        assert(stdout.includes('"command":"setBreakpoints"'),
            `Missing setBreakpoints response in: ${stdout}`);
    });

    test('disconnect responds', () => {
        assert(stdout.includes('"command":"disconnect"'),
            `Missing disconnect response in: ${stdout}`);
    });

    test('supportsConfigurationDoneRequest=true', () => {
        assert(stdout.includes('"supportsConfigurationDoneRequest":true'),
            `Missing capability in: ${stdout}`);
    });

    test('supportsDisassemblyRequest=true', () => {
        assert(stdout.includes('"supportsDisassemblyRequest":true'),
            `Missing capability in: ${stdout}`);
    });

    test('supportsStepIn=true', () => {
        assert(stdout.includes('"supportsStepIn":true'),
            `Missing capability in: ${stdout}`);
    });

    test('supportsStepOut=true', () => {
        assert(stdout.includes('"supportsStepOut":true'),
            `Missing capability in: ${stdout}`);
    });
}

async function testHelp() {
    const result = require('child_process').spawnSync(dapPath, ['--help'], { encoding: 'utf-8', timeout: 5000 });
    test('--help exit code 0', () => {
        assert(result.status === 0, `exit=${result.status}`);
    });
    test('--help shows usage', () => {
        const output = (result.stdout || '') + (result.stderr || '');
        assert(output.includes('Usage'), `expected Usage in: ${JSON.stringify(output)}`);
    });
}

async function main() {
    try {
        console.log('Test: DAP Initialize Sequence');
        await testInitialize();

        console.log('Test: DAP --help');
        testHelp();

        if (testsFailed === 0) {
            console.log(`  All ${testsPassed} tests passed`);
        } else {
            console.log(`  FAILED: ${testsFailed}/${testsPassed + testsFailed} tests`);
        }
    } catch (err) {
        console.error('[FATAL] Test suite error:', err.message);
        console.error(err.stack);
        testsFailed++;
    }

    process.exit(testsFailed > 0 ? 1 : 0);
}

main();