/**
 * Unit tests for extension.js — activation, commands, DAP adapter factory
 *
 * moduleNameMapper in jest.config.js provides the vscode mock.
 *
 * This validates that:
 * - vscode.commands.registerCommand is called with correct commandIds
 * - vscode.debug.registerDebugAdapterDescriptorFactory is called
 * - vscode.debug.registerDebugConfigurationProvider registers a provider
 * - The factory class creates DebugAdapterExecutable with expected args
 * - reset commands call update() with undefined
 * - openCppFile uses stackTrace from DAP session
 */

const mockExistsSync = jest.fn().mockReturnValue(true);
jest.mock('fs', () => {
    const actual = jest.requireActual('fs');
    return {
        ...actual,
        existsSync: mockExistsSync
    };
});

const vscode = require('vscode');
const extension = require('extension');
const { TrustDebugAdapterDescriptorFactory } = require('dap-adapter');
const fs = require('fs');

beforeEach(() => {
    // Reset mocks
    mockExistsSync.mockReset();
    mockExistsSync.mockReturnValue(true);
    Object.keys(vscode.registeredCommands).forEach(k => delete vscode.registeredCommands[k]);
    // Reset trust config cache so each test gets fresh defaults
    if (vscode.workspace._trustConfig) {
        vscode.workspace._trustConfig = null;
    }
    // Reset debug session
    vscode.debug.activeDebugSession = null;
    // Reset output channels
    vscode.window._outputChannels = [];
    vscode.window._statusBarItems = [];
});

describe('activate(): command registration', () => {
    test('registers trust.openCppFile command', () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        expect(vscode.registeredCommands['trust.openCppFile']).toBeDefined();
    });

    test('registers only reset commands for dapPath and lspPath', () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        const resetIds = [
            'trust.resetDapPath',
            'trust.resetLspPath'
        ];
        resetIds.forEach(id => {
            expect(vscode.registeredCommands[id]).toBeDefined();
        });
        // Build-related reset commands should NOT exist
        expect(vscode.registeredCommands['trust.resetCompilerPath']).toBeUndefined();
        expect(vscode.registeredCommands['trust.resetCppCompilerPath']).toBeUndefined();
        expect(vscode.registeredCommands['trust.resetCppCompilerOptions']).toBeUndefined();
        expect(vscode.registeredCommands['trust.resetTempDir']).toBeUndefined();
    });

    test('registers DAP factory, debug session handlers, configuration provider, and LSP client', () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        expect(ctx.subscriptions.length).toBeGreaterThanOrEqual(8);
    });

    test('resetDapPath resets dapPath', () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        const cfg = vscode.workspace.getConfiguration('trust');
        const spy = jest.spyOn(cfg, 'update');
        vscode.registeredCommands['trust.resetDapPath']();
        expect(spy).toHaveBeenCalledWith('dapPath', undefined, vscode.ConfigurationTarget.Global);
        spy.mockRestore();
    });
});

describe('TrustDebugAdapterDescriptorFactory', () => {
    test('DebugAdapterExecutable is a function', () => {
        expect(typeof vscode.debug.DebugAdapterExecutable).toBe('function');
    });

    test('direct DebugAdapterExecutable creation works', () => {
        const exe = new vscode.debug.DebugAdapterExecutable('test-cmd', ['a', 'b']);
        expect(exe.command).toBe('test-cmd');
        expect(exe.args).toEqual(['a', 'b']);
    });

    test('creates DebugAdapterExecutable with project-dir and lldb-server', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dapPath') return 'trust-dap';
            return origGet(key, defaultValue);
        });

        const factory = new TrustDebugAdapterDescriptorFactory();
        const session = {
            configuration: {
                projectDir: '/ws',
                lldbServerPath: '/usr/bin/lldb-server'
            }
        };

        const executable = factory.createDebugAdapterDescriptor(session);
        expect(executable.command).toBe('trust-dap');
        expect(executable.args).toContain('--project-dir');
        expect(executable.args).toContain('/ws');
        expect(executable.args).toContain('--lldb-server');
        expect(executable.args).toContain('/usr/bin/lldb-server');
        // Старые CLI-аргументы больше не передаются
        expect(executable.args).not.toContain('--source');
        expect(executable.args).not.toContain('--cpp');
        expect(executable.args).not.toContain('--target');
        expect(executable.args).not.toContain('--map');

        config.get.mockRestore();
    });

    test('creates executable with workspace folder as project-dir', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dapPath') return 'trust-dap';
            return origGet(key, defaultValue);
        });

        const factory = new TrustDebugAdapterDescriptorFactory();
        const session = { configuration: {} };
        const executable = factory.createDebugAdapterDescriptor(session);
        expect(executable.command).toBe('trust-dap');
        // projectDir берется из workspaceFolder ("/test/workspace")
        expect(executable.args).toContain('--project-dir');
        expect(executable.args).toContain('/test/workspace');
        expect(executable.args).not.toContain('--lldb-server');

        config.get.mockRestore();
    });
});

describe('resolveDebugConfiguration', () => {
    test('returns null when no .src file is active', async () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const provider = vscode.debug._lastConfigProvider;
        expect(provider).toBeDefined();

        const origEditor = vscode.window.activeTextEditor;
        vscode.window.activeTextEditor = {
            document: { fileName: '/test/main.txt' }
        };

        const result = await provider.resolveDebugConfiguration(
            { uri: { fsPath: '/test' } },
            null
        );
        expect(result).toBeNull();

        vscode.window.activeTextEditor = origEditor;
    });

    test('returns resolved configuration when .src file is active', async () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const provider = vscode.debug._lastConfigProvider;
        expect(provider).toBeDefined();

        const config = { type: 'trust', request: 'launch', name: 'Test' };
        const result = await provider.resolveDebugConfiguration(
            { uri: { fsPath: '/test/workspace' } },
            config
        );
        // Should get back configuration with resolved variables from defaults
        expect(result).toBeDefined();
        expect(result.sourceFile).toBe('/test/workspace/test.src');
        // cppFile, targetFile, mapFile should remain as provided (empty)
        expect(result.cppFile).toBe('');
        expect(result.targetFile).toBe('');
        expect(result.mapFile).toBe('');
    });

    test('generates default config when no configuration provided', async () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const provider = vscode.debug._lastConfigProvider;
        expect(provider).toBeDefined();

        const result = await provider.resolveDebugConfiguration(
            { uri: { fsPath: '/test/workspace' } },
            null
        );
        expect(result).toBeDefined();
        expect(result.type).toBe('trust');
        expect(result.request).toBe('launch');
        expect(result.sourceFile).toBe('/test/workspace/test.src');
    });
});

describe('sendCustomRequest', () => {
    test('calls session.customRequest and returns result', async () => {
        const mockSession = {
            customRequest: jest.fn().mockResolvedValue({
                body: {
                    stackFrames: [{ id: 0, source: { path: '/tmp/test.cpp' }, line: 42 }]
                }
            })
        };
        const result = await mockSession.customRequest('stackTrace', { startFrame: 0, levels: 1 });
        expect(result.body.stackFrames[0].source.path).toBe('/tmp/test.cpp');
        expect(mockSession.customRequest).toHaveBeenCalledWith('stackTrace', { startFrame: 0, levels: 1 });
    });

    test('throws on session error', async () => {
        const mockSession = {
            customRequest: jest.fn().mockRejectedValue(new Error('session closed'))
        };
        await expect(mockSession.customRequest('stackTrace')).rejects.toThrow('session closed');
    });
});

describe('trust.openCppFile command handler', () => {
    test('shows warning when no .src file', async () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const showWarnSpy = jest.spyOn(vscode.window, 'showWarningMessage');
        const origEditor = vscode.window.activeTextEditor;
        vscode.window.activeTextEditor = {
            document: { fileName: '/test/main.txt' }
        };

        await vscode.registeredCommands['trust.openCppFile']();
        expect(showWarnSpy).toHaveBeenCalledWith('No active .src file in editor');

        vscode.window.activeTextEditor = origEditor;
        showWarnSpy.mockRestore();
    });

    test('returns cppFile with line from active debug session stackTrace', async () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const session = new vscode.MockDebugSession();
        const origSession = vscode.debug.activeDebugSession;
        vscode.debug.activeDebugSession = session;

        // Should open the C++ file from session's stackTrace response
        const showDocSpy = jest.spyOn(vscode.window, 'showTextDocument');
        await vscode.registeredCommands['trust.openCppFile']();
        expect(showDocSpy).toHaveBeenCalled();

        vscode.debug.activeDebugSession = origSession;
        showDocSpy.mockRestore();
    });

    test('uses fallback from config when session errors', async () => {
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const session = new vscode.MockDebugSession();
        session.customRequest = async () => { throw new Error('session closed'); };
        const origSession = vscode.debug.activeDebugSession;
        vscode.debug.activeDebugSession = session;

        // Should fall back to tempDir computed path after session error
        const showDocSpy = jest.spyOn(vscode.window, 'showTextDocument');
        await vscode.registeredCommands['trust.openCppFile']();
        expect(showDocSpy).toHaveBeenCalled();

        vscode.debug.activeDebugSession = origSession;
        showDocSpy.mockRestore();
    });
});

describe('LSP Client initialization', () => {
    beforeEach(() => {
        vscode.window._statusBarItems = [];
    });

    test('creates LSP status bar item on activate with lspPath set', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);

        config.get.mockRestore();
    });

    test('creates LSP output channel on client creation', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        const createOutputSpy = jest.spyOn(vscode.window, 'createOutputChannel');
        extension.activate(ctx);
        const channelNames = createOutputSpy.mock.calls.map(call => call[0]);
        expect(channelNames).toContain('Trust Lang LSP');
        createOutputSpy.mockRestore();
        config.get.mockRestore();
    });

    test('creates LSP trace output channel on client creation (with lspPath set)', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        const createOutputSpy = jest.spyOn(vscode.window, 'createOutputChannel');
        extension.activate(ctx);
        const channelNames = createOutputSpy.mock.calls.map(call => call[0]);
        expect(channelNames).toContain('Trust Lang LSP Trace');
        createOutputSpy.mockRestore();
        config.get.mockRestore();
    });

    test('LanguageClient receives correct serverOptions.command (with lspPath set)', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const { LanguageClient } = require('vscode-languageclient/node');
        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        // Last call to LanguageClient constructor
        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        expect(lastCall[2].command).toBe('trust-lsp');
        expect(lastCall[2].args).toBeDefined();

        config.get.mockRestore();
    });

    test('LanguageClient receives outputChannel and traceOutputChannel (with lspPath set)', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const { LanguageClient } = require('vscode-languageclient/node');
        const ctx = { subscriptions: [] };
        extension.activate(ctx);
        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        const clientOptions = lastCall[3];
        expect(clientOptions.outputChannel).toBeDefined();
        expect(clientOptions.traceOutputChannel).toBeDefined();

        config.get.mockRestore();
    });
});

describe('LSP binary not found (handled via start().catch)', () => {
    beforeEach(() => {
        vscode.window._statusBarItems = [];
        vscode.window._outputChannels = [];
    });

    test('LSP client start() rejection shows error and updates status bar', async () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const { LanguageClient } = require('vscode-languageclient/node');

        // Make the next start() reject
        LanguageClient.mockImplementationOnce(jest.fn().mockImplementation((id, name, serverOptions, clientOptions) => ({
            id,
            serverOptions,
            clientOptions,
            start: jest.fn().mockRejectedValue(new Error('ENOENT: trust-lsp not found')),
            stop: jest.fn().mockResolvedValue(undefined),
            dispose: jest.fn().mockResolvedValue(undefined),
            onDidChangeState: jest.fn().mockImplementation(() => ({ dispose: jest.fn() })),
            onNotification: jest.fn().mockImplementation(() => ({ dispose: jest.fn() })),
            sendNotification: jest.fn(),
            sendRequest: jest.fn().mockResolvedValue({}),
        })));

        const showErrorSpy = jest.spyOn(vscode.window, 'showErrorMessage');

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        // After activate(), start() should have been called; its rejection is async
        // We need to flush pending promises
        await new Promise(process.nextTick);

        expect(showErrorSpy).toHaveBeenCalledWith(
            expect.stringContaining('failed to start')
        );

        const lspChannel = vscode.window._outputChannels.find(
            ch => ch.name === 'Trust Lang LSP'
        );
        expect(lspChannel).toBeDefined();
        expect(lspChannel.show).toHaveBeenCalledWith(true);
        expect(lspChannel.content).toContain('[ERROR]');
        expect(lspChannel.content).toContain('failed to start');

        // Status bar should show error state
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);
        expect(lspStatusBars[0].text).toContain('$(error)');

        showErrorSpy.mockRestore();
        config.get.mockRestore();
    });
});

describe('LSP client configuration', () => {
    beforeEach(() => {
        vscode.window._statusBarItems = [];
        vscode.window._outputChannels = [];
    });

    function setLspPath(value) {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return value;
            return origGet(key, defaultValue);
        });
        return config;
    }

    test('registers onDidChangeState and onNotification handlers (with lspPath set)', () => {
        const config = setLspPath('trust-lsp');
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const { LanguageClient } = require('vscode-languageclient/node');
        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        const client = lastCall[3]; // clientOptions

        // Check output channels are created
        expect(client.outputChannel).toBeDefined();
        expect(client.traceOutputChannel).toBeDefined();
        expect(client.outputChannel.name).toBe('Trust Lang LSP');

        // LSP status bar shows starting
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);
        expect(lspStatusBars[0].text).toContain('$(sync~spin)');

        config.get.mockRestore();
    });

    test('start() is called on LanguageClient (with lspPath set)', () => {
        const config = setLspPath('trust-lsp');
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const { LanguageClient } = require('vscode-languageclient/node');
        // Last constructed instance's start should have been called
        const startCalls = LanguageClient.mock.results
            .filter(r => r.value && r.value.start)
            .map(r => r.value.start.mock);
        const allStarts = startCalls.flatMap(s => s.calls);
        expect(allStarts.length).toBeGreaterThan(0);

        config.get.mockRestore();
    });
});

describe('DAP session commands', () => {
    test('continue sends customRequest', async () => {
        const session = new vscode.MockDebugSession();
        const spy = jest.spyOn(session, 'customRequest');
        const result = await session.customRequest('continue');
        expect(result).toEqual({ body: {} });
        expect(spy).toHaveBeenCalledWith('continue');
    });

    test('next sends customRequest', async () => {
        const session = new vscode.MockDebugSession();
        const result = await session.customRequest('next');
        expect(result).toEqual({ body: {} });
    });

    test('stepIn sends customRequest', async () => {
        const session = new vscode.MockDebugSession();
        const result = await session.customRequest('stepIn');
        expect(result).toEqual({ body: {} });
    });

    test('stackTrace returns mock frame', async () => {
        const session = new vscode.MockDebugSession();
        const result = await session.customRequest('stackTrace');
        expect(result.body.stackFrames).toHaveLength(1);
        expect(result.body.stackFrames[0].source.path).toBe('/tmp/test.cpp');
    });

    test('variables returns empty array by default', async () => {
        const session = new vscode.MockDebugSession();
        const result = await session.customRequest('variables');
        expect(result.body.variables).toEqual([]);
    });

    test('disconnect sends customRequest', async () => {
        const session = new vscode.MockDebugSession();
        const result = await session.customRequest('disconnect');
        expect(result).toEqual({ body: {} });
    });

    test('unknown command throws', async () => {
        const session = new vscode.MockDebugSession();
        await expect(session.customRequest('unknown')).rejects.toThrow('Unknown request');
    });
});

describe('Diagnostics: LSP path errors', () => {
    beforeEach(() => {
        vscode.window._outputChannels = [];
        vscode.window._statusBarItems = [];
    });

    test('shows error when lspPath is empty (not configured)', () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        LanguageClient.mockClear(); // Очищаем глобальный мок от предыдущих тестов

        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return ''; // empty = not configured
            return origGet(key, defaultValue);
        });

        const showErrorSpy = jest.spyOn(vscode.window, 'showErrorMessage');
        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        // Should show error about path not configured (no fallback, no existsSync)
        expect(showErrorSpy).toHaveBeenCalledWith(
            expect.stringContaining('path not configured')
        );

        // LanguageClient НЕ создаётся при пустом lspPath
        expect(LanguageClient.mock.calls.length).toBe(0);

        // Output channel should have ERROR
        const lspChannel = vscode.window._outputChannels.find(
            ch => ch.name === 'Trust Lang LSP'
        );
        expect(lspChannel).toBeDefined();
        expect(lspChannel.content).toContain('[ERROR]');
        expect(lspChannel.content).toContain('not configured');

        // Status bar should show error state
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);
        expect(lspStatusBars[0].text).toContain('$(error)');

        showErrorSpy.mockRestore();
        config.get.mockRestore();
    });

    test('creates LanguageClient when lspPath is non-empty', () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp'; // non-empty → LanguageClient создаётся
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        // LanguageClient constructor должен быть вызван
        const calls = LanguageClient.mock.calls;
        expect(calls.length).toBeGreaterThan(0);
        const lastCall = calls[calls.length - 1];
        expect(lastCall[2].command).toBe('trust-lsp');

        // start() должен быть вызван
        const startCalls = LanguageClient.mock.results
            .filter(r => r.value && r.value.start)
            .map(r => r.value.start.mock);
        const allStarts = startCalls.flatMap(s => s.calls);
        expect(allStarts.length).toBeGreaterThan(0);

        // Status bar — starting
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);
        expect(lspStatusBars[0].text).toContain('$(sync~spin)');

        config.get.mockRestore();
    });

    test('onDidChangeState with stopped produces diagnostic error', async () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        // Кастомная реализация LanguageClient, которая захватывает onDidChangeState
        let capturedOnDidChangeState = null;
        const mockClientInstance = {
            start: jest.fn().mockResolvedValue(undefined),
            stop: jest.fn().mockResolvedValue(undefined),
            dispose: jest.fn().mockResolvedValue(undefined),
            onDidChangeState: jest.fn().mockImplementation((cb) => {
                capturedOnDidChangeState = cb;
                return { dispose: jest.fn() };
            }),
            onNotification: jest.fn().mockImplementation(() => ({ dispose: jest.fn() })),
            sendNotification: jest.fn(),
            sendRequest: jest.fn().mockResolvedValue({}),
        };
        LanguageClient.mockImplementationOnce(
            jest.fn().mockImplementation(() => mockClientInstance)
        );

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        // Симулируем падение LSP: running → stopped
        expect(capturedOnDidChangeState).toBeDefined();
        capturedOnDidChangeState({ oldState: 'running', newState: 'stopped' });

        // Output channel должен содержать сообщение об ошибке
        const lspChannel = vscode.window._outputChannels.find(
            ch => ch.name === 'Trust Lang LSP'
        );
        expect(lspChannel).toBeDefined();
        expect(lspChannel.content).toContain('[ERROR]');
        expect(lspChannel.content).toContain('unexpectedly');

        // Status bar — error
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);
        expect(lspStatusBars[0].text).toContain('$(error)');

        config.get.mockRestore();
    });
});

describe('Diagnostics: DAP path errors', () => {
    test('throws when dapPath is not configured (empty)', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dapPath') return '';
            return origGet(key, defaultValue);
        });

        const showErrorSpy = jest.spyOn(vscode.window, 'showErrorMessage');
        const factory = new TrustDebugAdapterDescriptorFactory();

        expect(() => {
            factory.createDebugAdapterDescriptor({ configuration: {} });
        }).toThrow('path not configured');

        expect(showErrorSpy).toHaveBeenCalledWith(
            expect.stringContaining('path not configured')
        );

        showErrorSpy.mockRestore();
        config.get.mockRestore();
    });

    test('creates DebugAdapterExecutable when dapPath is non-empty', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dapPath') return 'trust-dap'; // non-empty → executable создаётся
            return origGet(key, defaultValue);
        });

        const factory = new TrustDebugAdapterDescriptorFactory();
        const session = { configuration: {} };
        const executable = factory.createDebugAdapterDescriptor(session);

        // Должен быть создан DebugAdapterExecutable с command='trust-dap'
        expect(executable).toBeDefined();
        expect(executable.command).toBe('trust-dap');
        expect(executable.args).toContain('--project-dir');

        config.get.mockRestore();
    });
});
