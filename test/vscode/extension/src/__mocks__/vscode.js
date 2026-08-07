/**
 * Mock for Visual Studio Code API
 * Used by Jest for unit testing extension.js functions
 */

const path = require('path');

class MockStatusBarItem {
    constructor() {
        this.text = '';
        this.tooltip = '';
        this.command = '';
    }
    show() {}
    hide() {}
    dispose() {}
}

class MockOutputChannel {
    constructor(name) {
        this.name = name;
        this.content = '';
        this.show = jest.fn();
    }
    appendLine(msg) { this.content += msg + '\n'; }
    append(msg) { this.content += msg; }
    dispose() {}
}

class MockTextEditor {
    constructor(fileName) {
        this.document = {
            fileName: fileName || '/test/workspace/test.src',
            getText: () => 'test content'
        };
        this.selection = {
            active: { line: 5 }
        };
    }
}

class MockDebugSession {
    constructor() {
        this.type = 'trust';
    }
    async customRequest(command) {
        switch (command) {
            case 'continue':
                return { body: {} };
            case 'next':
                return { body: {} };
            case 'stepIn':
                return { body: {} };
            case 'stepOut':
                return { body: {} };
            case 'pause':
                return { body: {} };
            case 'disconnect':
                return { body: {} };
            case 'variables':
                return { body: { variables: [] } };
            case 'stackTrace':
                return {
                    body: {
                        stackFrames: [
                            {
                                id: 0,
                                name: 'main',
                                source: { path: '/tmp/test.cpp' },
                                line: 42,
                                column: 0
                            }
                        ]
                    }
                };
            default:
                throw new Error(`Unknown request: ${command}`);
        }
    }
}

const StatusBarAlignment = { Left: 1, Right: 2 };
const ViewColumn = { Active: -1, Beside: 2, One: 1, Two: 2, Three: 3 };
const ConfigurationTarget = { Global: 1, Workspace: 2, WorkspaceFolder: 3 };
const TextEditorRevealType = { InCenter: 0, InCenterIfOutsideViewport: 1, AtTop: 2 };

// Track registered commands for testing
const registeredCommands = {};

const workspace = {
    workspaceFolders: [{ uri: { fsPath: '/test/workspace' } }],
    // Cache trust config singleton so spies work across calls
    getConfiguration: (section) => {
        if (section === 'trust') {
            return workspace._trustConfig || (workspace._trustConfig = new MockTrustConfiguration());
        }
        return new Map();
    },
    openTextDocument: (uri) => Promise.resolve({
        fileName: typeof uri === 'string' ? uri : uri.fsPath,
        getText: () => 'mocked cpp content'
    })
};

class MockTrustConfiguration {
    constructor() {
        this.settings = {
            compilerPath: 'trust',
            dapPath: '',
            lspPath: '',
            cppCompilerPath: 'clang++-22',
            cppCompilerOptions: '-std=c++23 -g3 -O0',
            tempDir: '.trust',
            gdbPath: '',
            dev: {
                traceDAP: false,
                traceLSP: false,
                highlightRanges: false
            }
        };
    }
    get(key, defaultValue) {
        // Поддержка точечных путей (trust.dev.*), как в реальном VS Code.
        const parts = String(key).split('.');
        let cur = this.settings;
        for (const part of parts) {
            if (cur === null || cur === undefined || typeof cur !== 'object' || !(part in cur)) {
                return defaultValue;
            }
            cur = cur[part];
        }
        return cur;
    }
    update(key, value, target) {
        this.settings[key] = value;
    }
}

const ProgressLocation = { Notification: 1, Window: 2 };

const window = {
    _statusBarItems: [],
    _outputChannels: [],
    createOutputChannel: (name) => {
        const ch = new MockOutputChannel(name);
        window._outputChannels.push(ch);
        return ch;
    },
    createStatusBarItem: (alignment, priority) => {
        const item = new MockStatusBarItem();
        window._statusBarItems.push(item);
        return item;
    },
    showInformationMessage: (msg) => Promise.resolve(msg),
    showWarningMessage: (msg) => Promise.resolve(msg),
    showErrorMessage: (msg) => Promise.resolve(msg),
    activeTextEditor: new MockTextEditor(),
    showTextDocument: (doc, options) => Promise.resolve({
        revealRange: (range, mode) => {}
    }),
    onDidChangeActiveTextEditor: () => ({ dispose: () => {} }),
    withProgress: (options, task) => task({ report: () => {} })
};

class DebugAdapterExecutable {
    constructor(command, args) {
        this.command = command;
        this.args = args;
    }
}

const debug = {
    _lastConfigProvider: null,
    _lastTrackerFactory: null,
    registerDebugAdapterDescriptorFactory: (type, factory) => ({
        dispose: () => {}
    }),
    registerDebugAdapterTrackerFactory: (type, factory) => {
        debug._lastTrackerFactory = factory;
        return { dispose: () => { debug._lastTrackerFactory = null; } };
    },
    registerDebugConfigurationProvider: (type, provider) => {
        debug._lastConfigProvider = provider;
        return {
            dispose: () => { debug._lastConfigProvider = null; }
        };
    },
    onDidStartDebugSession: () => ({ dispose: () => {} }),
    onDidTerminateDebugSession: () => ({ dispose: () => {} }),
    activeDebugSession: null,
    DebugAdapterExecutable
};

const commands = {
    registerCommand: (id, handler) => {
        registeredCommands[id] = handler;
        return { dispose: () => { delete registeredCommands[id]; } };
    },
    executeCommand: (id, ...args) => {
        if (registeredCommands[id]) {
            return Promise.resolve(registeredCommands[id](...args));
        }
        return Promise.resolve();
    }
};

const Uri = {
    file: (fspath) => ({ fsPath: fspath, path: fspath, scheme: 'file' })
};

const tasks = {
    registerTaskProvider: (type, provider) => ({
        dispose: () => {}
    })
};

const Range = class {
    constructor(startLine, startCol, endLine, endCol) {
        this.start = { line: startLine, character: startCol };
        this.end = { line: endLine, character: endCol };
    }
};

class CustomExecution {
    constructor(callback) {
        this.callback = callback;
    }
}

const TaskGroup = {
    Build: { isDefault: false, id: 'build' }
};

class Task {
    constructor(definition, scope, name, source, execution) {
        this.definition = definition;
        this.scope = scope;
        this.name = name;
        this.source = source;
        this.execution = execution;
        this.group = null;
        this.problemMatchers = [];
    }
}

module.exports = {
    StatusBarAlignment,
    ViewColumn,
    ConfigurationTarget,
    TextEditorRevealType,
    workspace,
    window,
    debug,
    commands,
    Uri,
    Range,
    DebugAdapterExecutable,
    tasks,
    ProgressLocation,
    CustomExecution,
    TaskGroup,
    Task,
    // Test helpers
    registeredCommands,
    // Mock classes
    MockStatusBarItem,
    MockOutputChannel,
    MockTextEditor,
    MockTrustConfiguration,
    MockDebugSession
};