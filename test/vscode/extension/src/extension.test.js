/**
 * Unit tests for extension.js - activation, commands, DAP adapter factory
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
const mockMkdirSync = jest.fn();
jest.mock('fs', () => {
    const actual = jest.requireActual('fs');
    return {
        ...actual,
        existsSync: mockExistsSync,
        mkdirSync: mockMkdirSync
    };
});

// Mock execSync to avoid real command execution in tests
jest.mock('child_process', () => {
    const actual = jest.requireActual('child_process');
    return {
        ...actual,
        execSync: jest.fn().mockReturnValue('')
    };
});

// Mock buildForDebug to avoid real file system and process execution
jest.mock('extension-utils', () => {
    const actual = jest.requireActual('extension-utils');
    return {
        ...actual,
        buildForDebug: jest.fn().mockResolvedValue({
            success: true,
            cppFile: '/test/workspace/.trust/test.cpp',
            targetFile: '/test/workspace/.trust/test'
        }),
        transpileSource: jest.fn().mockReturnValue({ success: true, cppFile: '/test/workspace/.trust/test.cpp' }),
        compileCpp: jest.fn().mockReturnValue({ success: true, targetFile: '/test/workspace/.trust/test' })
    };
});

const vscode = require('vscode');
const extension = require('extension');
const { TrustDebugAdapterDescriptorFactory, TrustDebugAdapterTracker, getTraceChannel, writeTrace, resetTraceChannel } = require('dap-adapter');
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
    // Reset trace channel in dap-adapter module
    resetTraceChannel();
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

    test('creates DebugAdapterExecutable with project-dir only', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dapPath') return 'trust-dap';
            return origGet(key, defaultValue);
        });

        const factory = new TrustDebugAdapterDescriptorFactory();
        const session = {
            configuration: {
                projectDir: '/ws'
            }
        };

        const executable = factory.createDebugAdapterDescriptor(session);
        expect(executable.command).toBe('trust-dap');
        expect(executable.args).toContain('--project-dir');
        expect(executable.args).toContain('/ws');
        // Никакие другие CLI-аргументы не передаются
        expect(executable.args).not.toContain('--gdb');
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
        expect(executable.args).not.toContain('--gdb');

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
        // cppFile and targetFile are filled by buildForDebug mock
        expect(result.cppFile).toBe('/test/workspace/.trust/test.cpp');
        expect(result.targetFile).toBe('/test/workspace/.trust/test');
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

        // Status bar - starting
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

        // Status bar - error
        const lspStatusBars = vscode.window._statusBarItems.filter(
            item => item.text && item.text.includes('Trust LSP')
        );
        expect(lspStatusBars.length).toBeGreaterThan(0);
        expect(lspStatusBars[0].text).toContain('$(error)');

        config.get.mockRestore();
    });
});

describe('TrustBuildTask', () => {
    const { TrustBuildTask } = extension;

    test('buildTaskType is trust-build', () => {
        expect(TrustBuildTask.buildTaskType).toBe('trust-build');
    });

    test('getBuildCppTask creates Task with correct type and name', () => {
        const ws = { uri: { fsPath: '/test/workspace' } };
        const task = TrustBuildTask.getBuildCppTask('/test/workspace/test.src', ws);
        expect(task.definition.type).toBe('trust-build');
        expect(task.name).toBe('Trust: Transpile .src');
        expect(task.source).toBe('trust');
        expect(task.group).toBe(vscode.TaskGroup.Build);
    });

    test('getCompileTask creates Task with correct type and name', () => {
        const ws = { uri: { fsPath: '/test/workspace' } };
        const task = TrustBuildTask.getCompileTask('/test/workspace/test.src', ws);
        expect(task.definition.type).toBe('trust-build');
        expect(task.name).toBe('Trust: Compile .cpp');
        expect(task.source).toBe('trust');
        expect(task.group).toBe(vscode.TaskGroup.Build);
    });

    test('getBuildAllTask creates Task with correct type and name', () => {
        const ws = { uri: { fsPath: '/test/workspace' } };
        const task = TrustBuildTask.getBuildAllTask('/test/workspace/test.src', ws);
        expect(task.definition.type).toBe('trust-build');
        expect(task.name).toBe('Trust: Build all');
        expect(task.source).toBe('trust');
        expect(task.group).toBe(vscode.TaskGroup.Build);
    });

    test('getBuildCppTask creates CustomExecution', () => {
        const ws = { uri: { fsPath: '/test/workspace' } };
        const task = TrustBuildTask.getBuildCppTask('/test/workspace/test.src', ws);
        expect(task.execution).toBeInstanceOf(vscode.CustomExecution);
    });

    test('getCompileTask creates CustomExecution', () => {
        const ws = { uri: { fsPath: '/test/workspace' } };
        const task = TrustBuildTask.getCompileTask('/test/workspace/test.src', ws);
        expect(task.execution).toBeInstanceOf(vscode.CustomExecution);
    });

    test('getBuildAllTask creates CustomExecution', () => {
        const ws = { uri: { fsPath: '/test/workspace' } };
        const task = TrustBuildTask.getBuildAllTask('/test/workspace/test.src', ws);
        expect(task.execution).toBeInstanceOf(vscode.CustomExecution);
    });
});

describe('Diagnostics: DAP path errors (binary not found)', () => {
    test('throws when dapPath binary does not exist', () => {
        const config = vscode.workspace.getConfiguration('trust');
        const origGet = config.get.bind(config);
        jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dapPath') return '/nonexistent/trust-dap';
            return origGet(key, defaultValue);
        });

        // existsSync returns true by default, mock it to return false for this path
        mockExistsSync.mockReturnValue(false);

        const showErrorSpy = jest.spyOn(vscode.window, 'showErrorMessage');
        const factory = new TrustDebugAdapterDescriptorFactory();

        expect(() => {
            factory.createDebugAdapterDescriptor({ configuration: {} });
        }).toThrow('binary not found');

        expect(showErrorSpy).toHaveBeenCalledWith(
            expect.stringContaining('binary not found')
        );

        showErrorSpy.mockRestore();
        config.get.mockRestore();
    });
});

describe('TrustDebugAdapterTracker', () => {
    beforeEach(() => {
        vscode.window._outputChannels = [];
    });

    test('logs request messages onWillReceiveMessage', () => {
        const tracker = new TrustDebugAdapterTracker();
        tracker.onWillReceiveMessage({
            type: 'request',
            command: 'initialize',
            seq: 42,
            arguments: { clientID: 'vscode' }
        });

        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
    });

    test('logs response messages onDidSendMessage', () => {
        const tracker = new TrustDebugAdapterTracker();
        tracker.onDidSendMessage({
            type: 'response',
            command: 'initialize',
            request_seq: 42,
            success: true,
            body: { supportsConfigurationDoneRequest: true }
        });

        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
    });

    test('logs event messages onDidSendMessage', () => {
        const tracker = new TrustDebugAdapterTracker();
        tracker.onDidSendMessage({
            type: 'event',
            event: 'stopped',
            body: { reason: 'breakpoint', threadId: 1 }
        });

        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
    });

    test('logs errors onError', () => {
        const tracker = new TrustDebugAdapterTracker();
        const error = new Error('DebugAdapterExecutable not constructed');
        const showSpy = jest.spyOn(vscode.window, 'showErrorMessage');

        tracker.onError(error);

        // Ошибки всегда выводятся в канал
        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
        expect(channel.content).toContain('[DAP-ERR]');
        expect(channel.content).toContain('DebugAdapterExecutable not constructed');
    });

    test('logs session start and stop events', () => {
        const tracker = new TrustDebugAdapterTracker();
        tracker.onWillStartSession();
        tracker.onWillStopSession();

        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
        expect(channel.content).toContain('[DAP-SESSION] Session starting');
        expect(channel.content).toContain('[DAP-SESSION] Session stopped');
    });

    test('handles null/undefined messages gracefully', () => {
        const tracker = new TrustDebugAdapterTracker();
        // Should not throw
        tracker.onWillReceiveMessage(null);
        tracker.onWillReceiveMessage(undefined);
        tracker.onDidSendMessage(null);
        tracker.onDidSendMessage(undefined);
        tracker.onError(null);
        tracker.onError(undefined);
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

// ═══════════════════════════════════════════════════════════════
// Developer settings group (trust.dev.*): --trace, highlightRanges middleware
// ═══════════════════════════════════════════════════════════════

// Активирует расширение с заданным lspPath. Spy на lspPath снимается сразу после
// activate (lspPath нужен только при старте), чтобы не конфликтовать со spy в тестах.
function activateWithLspPath(lspPath) {
    const config = vscode.workspace.getConfiguration('trust');
    const origGet = config.get.bind(config);
    const spy = jest.spyOn(config, 'get').mockImplementation((key, defaultValue) => {
        if (key === 'lspPath') return lspPath;
        return origGet(key, defaultValue);
    });
    const ctx = { subscriptions: [] };
    extension.activate(ctx);
    spy.mockRestore();
}

describe('Developer settings: dev.traceLSP → --trace', () => {
    beforeEach(() => {
        vscode.window._statusBarItems = [];
        vscode.window._outputChannels = [];
    });

    test('adds --trace to server args when dev.traceLSP is true', () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        const cfg = vscode.workspace.getConfiguration('trust');
        const origGet = cfg.get.bind(cfg);
        jest.spyOn(cfg, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            if (key === 'dev.traceLSP') return true;
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        expect(lastCall[2].args).toContain('--trace');

        cfg.get.mockRestore();
    });

    test('does NOT add --trace when dev.traceLSP is false (default)', () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        const cfg = vscode.workspace.getConfiguration('trust');
        const origGet = cfg.get.bind(cfg);
        jest.spyOn(cfg, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        expect(lastCall[2].args).not.toContain('--trace');

        cfg.get.mockRestore();
    });
});

describe('Developer settings: highlightRanges middleware (provideDocumentLinks)', () => {
    function getMiddleware() {
        const { LanguageClient } = require('vscode-languageclient/node');
        activateWithLspPath('trust-lsp');
        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        const middleware = lastCall[3].middleware;
        expect(middleware).toBeDefined();
        return middleware;
    }

    test('returns empty array (no underlining) when highlightRanges is off (default)', async () => {
        const middleware = getMiddleware();
        const next = jest.fn().mockResolvedValue([{ range: {}, target: 'file:///x.cppt' }]);
        const doc = { uri: 'file:///test.src' };
        const token = { isCancellationRequested: () => false };

        const result = await middleware.provideDocumentLinks(doc, token, next);

        expect(result).toEqual([]);
        expect(next).not.toHaveBeenCalled();
    });

    test('passes through to next() when highlightRanges is on', async () => {
        const middleware = getMiddleware();
        const cfg = vscode.workspace.getConfiguration('trust');
        const origGet = cfg.get.bind(cfg);
        const spy = jest.spyOn(cfg, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dev.highlightRanges') return true;
            return origGet(key, defaultValue);
        });

        const next = jest.fn().mockResolvedValue([{ range: {}, target: 'file:///x.cppt' }]);
        const doc = { uri: 'file:///test.src' };
        const token = { isCancellationRequested: () => false };

        const result = await middleware.provideDocumentLinks(doc, token, next);

        expect(result).toEqual([{ range: {}, target: 'file:///x.cppt' }]);
        expect(next).toHaveBeenCalledWith(doc, token);
        spy.mockRestore();
    });

    test('does not intercept hover/definition providers (only documentLinks middleware is set)', () => {
        const middleware = getMiddleware();
        expect(middleware.provideHover).toBeUndefined();
        expect(middleware.provideDefinition).toBeUndefined();
        expect(typeof middleware.provideDocumentLinks).toBe('function');
    });
});


// ═══════════════════════════════════════════════════════════════
// Trace output on error: DAP (writeTrace/writeDiag) и LSP (канал «Trust Lang LSP»)
// ═══════════════════════════════════════════════════════════════

describe('DAP trace output on error', () => {
    const { writeTrace, writeDiag, resetTraceChannel } = require('dap-adapter');

    beforeEach(() => {
        vscode.window._outputChannels = [];
        resetTraceChannel();
    });

    test('writeTrace(msg, isError=true) always writes to "Trust Lang" channel (even when traceDAP off)', () => {
        writeTrace('[DAP-ERR] boom', true);
        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
        expect(channel.content).toContain('[DAP-ERR] boom');
    });

    test('writeTrace(msg) (non-error) is suppressed when dev.traceDAP is off', () => {
        writeTrace('[DAP->] verbose request');
        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
        expect(channel.content).not.toContain('[DAP->] verbose request');
    });

    test('writeTrace(msg) (non-error) writes when dev.traceDAP is on', () => {
        const cfg = vscode.workspace.getConfiguration('trust');
        const origGet = cfg.get.bind(cfg);
        jest.spyOn(cfg, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'dev.traceDAP') return true;
            return origGet(key, defaultValue);
        });

        writeTrace('[DAP->] verbose request');
        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel.content).toContain('[DAP->] verbose request');

        cfg.get.mockRestore();
    });

    test('writeDiag always writes to "Trust Lang" channel', () => {
        writeDiag('[DAP-DESC] diagnostic');
        const channel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang');
        expect(channel).toBeDefined();
        expect(channel.content).toContain('[DAP-DESC] diagnostic');
    });
});

describe('LSP trace output on error (dev.traceLSP)', () => {
    beforeEach(() => {
        vscode.window._statusBarItems = [];
        vscode.window._outputChannels = [];
    });

    test('LSP client start() rejection writes [ERROR] to "Trust Lang LSP" channel', async () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        const cfg = vscode.workspace.getConfiguration('trust');
        const origGet = cfg.get.bind(cfg);
        jest.spyOn(cfg, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            if (key === 'dev.traceLSP') return true;
            return origGet(key, defaultValue);
        });

        LanguageClient.mockImplementationOnce(jest.fn().mockImplementation((id, name, serverOptions, clientOptions) => ({
            start: jest.fn().mockRejectedValue(new Error('ENOENT: trust-lsp not found')),
            stop: jest.fn().mockResolvedValue(undefined),
            dispose: jest.fn().mockResolvedValue(undefined),
            onDidChangeState: jest.fn().mockImplementation(() => ({ dispose: jest.fn() })),
            onNotification: jest.fn().mockImplementation(() => ({ dispose: jest.fn() }))
        })));

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        // Дать микротаску обработать rejected promise из start() (.catch).
        await new Promise(process.nextTick);

        const lspChannel = vscode.window._outputChannels.find(ch => ch.name === 'Trust Lang LSP');
        expect(lspChannel).toBeDefined();
        expect(lspChannel.content).toContain('[ERROR]');
        expect(lspChannel.content).toContain('failed to start');

        cfg.get.mockRestore();
    });

    test('server receives --trace when dev.traceLSP is true (trace to LSP channel)', () => {
        const { LanguageClient } = require('vscode-languageclient/node');
        const cfg = vscode.workspace.getConfiguration('trust');
        const origGet = cfg.get.bind(cfg);
        jest.spyOn(cfg, 'get').mockImplementation((key, defaultValue) => {
            if (key === 'lspPath') return 'trust-lsp';
            if (key === 'dev.traceLSP') return true;
            return origGet(key, defaultValue);
        });

        const ctx = { subscriptions: [] };
        extension.activate(ctx);

        const calls = LanguageClient.mock.calls;
        const lastCall = calls[calls.length - 1];
        expect(lastCall[2].args).toContain('--trace');

        cfg.get.mockRestore();
    });
});

