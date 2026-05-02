console.log('[TRUST-LANG] extension.js module loaded (top-level)');
const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { resolvePath, resolveDapVariables, updateStatusBar, updateLspStatusBar, LspStatus } = require('./extension-utils');
const { TrustDebugAdapterDescriptorFactory } = require('./dap-adapter');
const { LanguageClient } = require('vscode-languageclient/node');

let isDebugging = false;
let lspClient = null;
let buildStatusBar = null;

// ── Output channel for tracing ──
let traceChannel = null;

function getTraceChannel() {
    if (!traceChannel) {
        traceChannel = vscode.window.createOutputChannel('Trust Lang');
    }
    return traceChannel;
}

function isDapTraceEnabled() {
    return vscode.workspace.getConfiguration('trust').get('traceDAP', false);
}

function trace(msg) {
    if (!isDapTraceEnabled()) return;
    getTraceChannel().appendLine(msg);
}

function traceRequest(prefix, req) {
    if (!isDapTraceEnabled()) return;
    const args = req.arguments ? JSON.stringify(req.arguments, null, 2) : '(no arguments)';
    getTraceChannel().appendLine(`[REQ] ${prefix} seq=${req.seq || '?'}: ${args}`);
}

function traceResponse(prefix, requestSeq, success, body) {
    if (!isDapTraceEnabled()) return;
    const bodyStr = body ? JSON.stringify(body, null, 2) : '(empty)';
    getTraceChannel().appendLine(`[RES] ${prefix} req_seq=${requestSeq} success=${success}: ${bodyStr}`);
}

function traceEvent(prefix, eventName, body) {
    if (!isDapTraceEnabled()) return;
    const bodyStr = body ? JSON.stringify(body, null, 2) : '(empty)';
    getTraceChannel().appendLine(`[EVT] ${prefix}: ${eventName} ${bodyStr}`);
}


// ── DAP Wrapper for custom request with tracing ──
async function sendCustomRequest(session, command, args) {
    trace(`[DAP-CUSTOM] Sending '${command}': ${JSON.stringify(args)}`);
    try {
        const result = await session.customRequest(command, args);
        trace(`[DAP-CUSTOM] Response for '${command}': ${JSON.stringify(result)}`);
        return result;
    } catch (err) {
        trace(`[DAP-CUSTOM] Error for '${command}': ${err.message}`);
        throw err;
    }
}

// ── Reset setting helper ──
function registerResetCommand(context, settingKey, commandId) {
    const cmd = vscode.commands.registerCommand(commandId, () => {
        vscode.workspace.getConfiguration('trust').update(settingKey, undefined, vscode.ConfigurationTarget.Global);
        vscode.window.showInformationMessage(`Trust: ${settingKey} reset to default`);
    });
    context.subscriptions.push(cmd);
}

// ── Task Provider for building .src files ──
class TrustBuildTaskProvider {
    static buildSourceFileTaskName = 'Trust: Build .src file';
    static runSourceFileTaskName = 'Trust: Run .src file';

    provideTasks(token) {
        const editor = vscode.window.activeTextEditor;
        if (!editor || !editor.document.fileName.endsWith('.src')) {
            return [];
        }

        const sourceFile = editor.document.fileName;
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
        if (!workspaceFolder) return [];

        const buildTask = this.createBuildTask(workspaceFolder, sourceFile);
        const runTask = this.createRunTask(workspaceFolder, sourceFile);
        return [buildTask, runTask];
    }

    resolveTask(task, token) {
        return task;
    }

    createBuildTask(workspaceFolder, sourceFile) {
        const task = new vscode.Task(
            { type: 'trust', sourceFile: sourceFile },
            workspaceFolder,
            TrustBuildTaskProvider.buildSourceFileTaskName,
            'trust',
            new vscode.CustomExecution(async (resolvedDefinition) => {
                return await this.runBuildTask(resolvedDefinition, workspaceFolder);
            })
        );
        task.group = vscode.TaskGroup.Build;
        task.problemMatchers = [];
        return task;
    }

    createRunTask(workspaceFolder, sourceFile) {
        const task = new vscode.Task(
            { type: 'trust', sourceFile: sourceFile },
            workspaceFolder,
            TrustBuildTaskProvider.runSourceFileTaskName,
            'trust',
            new vscode.CustomExecution(async (resolvedDefinition) => {
                return await this.runRunTask(resolvedDefinition, workspaceFolder);
            })
        );
        task.group = vscode.TaskGroup.Test;
        task.problemMatchers = [];
        return task;
    }

    async runBuildTask(definition, workspaceFolder) {
        const config = vscode.workspace.getConfiguration('trust');
        const sourceFile = definition.sourceFile;
        const baseName = path.basename(sourceFile, '.src');
        const tempDir = config.get('tempDir', '.trust');
        const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(workspaceFolder.uri.fsPath, tempDir);

        const cppFile = path.join(resolvedTempDir, baseName + '.cpp');
        const mapFile = path.join(resolvedTempDir, baseName + '.map');

        // Ensure temp dir exists
        fs.mkdirSync(resolvedTempDir, { recursive: true });

        // Step 1: Transpile
        const compilerPath = config.get('compilerPath', 'trust');
        const transpileArgs = [
            sourceFile,
            '--emit-cpp', cppFile,
            '--emit-source-map', mapFile,
            '--temp-dir', resolvedTempDir
        ];

        return await runProcess(
            'Transpiling ' + baseName + '.src',
            compilerPath,
            transpileArgs,
            workspaceFolder.uri.fsPath
        );
    }

    async runRunTask(definition, workspaceFolder) {
        const config = vscode.workspace.getConfiguration('trust');
        const sourceFile = definition.sourceFile;
        const baseName = path.basename(sourceFile, '.src');
        const tempDir = config.get('tempDir', '.trust');
        const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(workspaceFolder.uri.fsPath, tempDir);

        const cppFile = path.join(resolvedTempDir, baseName + '.cpp');
        const mapFile = path.join(resolvedTempDir, baseName + '.map');
        const binaryFile = path.join(resolvedTempDir, baseName);

        fs.mkdirSync(resolvedTempDir, { recursive: true });

        // Step 1: Transpile
        const compilerPath = config.get('compilerPath', 'trust');
        const transpileResult = await runProcess(
            'Transpiling ' + baseName + '.src',
            compilerPath,
            [sourceFile, '--emit-cpp', cppFile, '--emit-source-map', mapFile, '--temp-dir', resolvedTempDir],
            workspaceFolder.uri.fsPath
        );
        if (transpileResult.exitCode !== 0) return transpileResult;

        // Step 2: Compile C++ to ELF
        const cppCompilerPath = config.get('cppCompilerPath', 'clang++-22');
        const cppCompilerOptions = (config.get('cppCompilerOptions', '-std=c++23 -g3 -O0') || '').split(/\s+/).filter(s => s);

        const compileArgs = [
            ...cppCompilerOptions,
            '-o', binaryFile,
            cppFile
        ];

        const compileResult = await runProcess(
            'Compiling ' + baseName + '.cpp',
            cppCompilerPath,
            compileArgs,
            workspaceFolder.uri.fsPath
        );
        if (compileResult.exitCode !== 0) return compileResult;

        // Step 3: Run
        return await runProcess(
            'Running ' + baseName,
            binaryFile,
            [],
            workspaceFolder.uri.fsPath
        );
    }
}

async function runProcess(name, command, args, cwd) {
    const terminal = vscode.window.createTerminal({
        name: name,
        cwd: cwd,
        shellPath: command,
        shellArgs: args
    });
    terminal.show();
    // CustomExecution requires a pseudoterminal; this is a simplified version.
    // The real runProcess returns a Pseudoterminal-like object.
    return createDummyTerminalForTask(name);
}

function createDummyTerminalForTask(name) {
    const writeEmitter = new vscode.EventEmitter();
    const closeEmitter = new vscode.EventEmitter();

    const pty = {
        onDidWrite: writeEmitter.event,
        onDidClose: closeEmitter.event,
        open: () => {
            writeEmitter.fire(`Running ${name}...\r\n`);
        },
        close: () => {}
    };

    const terminal = vscode.window.createTerminal({ name: name, pty: pty });
    terminal.show();
    return { exitCode: 0, terminal: terminal };
}


function activate(context) {
    console.log('Trust Lang extension is now active');
    trace('[ACTIVATE] Trust Lang extension activated');

    // Build status bar (from extension-utils)
    buildStatusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 10);
    buildStatusBar.text = '$(debug) Trust Debug: Idle';
    buildStatusBar.tooltip = 'Trust Debugger - Ready';
    buildStatusBar.command = 'trust.openCppFile';
    buildStatusBar.show();

    // LSP status bar
    const lspStatusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 9);
    console.log('[TRUST-LSP] lspStatusBar created:', lspStatusBar ? 'exists' : 'null');
    updateLspStatusBar(null, lspStatusBar);
    console.log('[TRUST-LSP] after updateLspStatusBar(null), text:', lspStatusBar.text);
    // Show only when a .src file is active
    const updateLspStatusBarVisibility = () => {
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.fileName.endsWith('.src')) {
            console.log('[TRUST-LSP] updateLspStatusBarVisibility: .src file active, calling show()');
            lspStatusBar.show();
        } else {
            console.log('[TRUST-LSP] updateLspStatusBarVisibility: no .src file, calling hide()');
            lspStatusBar.hide();
        }
    };
    updateLspStatusBarVisibility();
    context.subscriptions.push(lspStatusBar);
    context.subscriptions.push(
        vscode.window.onDidChangeActiveTextEditor(() => {
            console.log('[TRUST-LSP] onDidChangeActiveTextEditor fired, visible editors:', vscode.window.visibleTextEditors.length);
            updateLspStatusBarVisibility();
        })
    );

    // Clean up trace channel on deactivation
    context.subscriptions.push({
        dispose: () => {
            if (traceChannel) {
                traceChannel.dispose();
                traceChannel = null;
            }
        }
    });

    // Register DAP factory
    const factory = new TrustDebugAdapterDescriptorFactory();
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('trust', factory)
    );

    // Debug session state tracking
    context.subscriptions.push(
        vscode.debug.onDidStartDebugSession((session) => {
            if (session.type === 'trust') {
                trace(`[SESSION] Started: type=${session.type}, name=${session.name}`);
                isDebugging = true;
                updateStatusBar(true, buildStatusBar);
                trace(`[SESSION] no build step (compile is separate)`);
            }
        })
    );

    context.subscriptions.push(
        vscode.debug.onDidTerminateDebugSession((session) => {
            if (session.type === 'trust') {
                trace(`[SESSION] Terminated: type=${session.type}, name=${session.name}`);
                isDebugging = false;
                updateStatusBar(false, buildStatusBar);
            }
        })
    );

    // ── Open C++ File command ──
    const openCppCmd = vscode.commands.registerCommand('trust.openCppFile', async () => {
        trace(`[CMD] trust.openCppFile triggered`);

        const editor = vscode.window.activeTextEditor;
        if (!editor || !editor.document.fileName.endsWith('.src')) {
            vscode.window.showWarningMessage('No active .src file in editor');
            return;
        }

        let cppFile, cppLine;
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri?.fsPath;
        if (!workspaceFolder) {
            vscode.window.showErrorMessage('No workspace folder open');
            return;
        }

        // 1. Если активна debug-сессия trust → стандартный DAP stackTrace
        const session = vscode.debug.activeDebugSession;
        if (session && session.type === 'trust') {
            try {
                const result = await sendCustomRequest(session, 'stackTrace', { startFrame: 0, levels: 1 });
                const stackFrame = result?.body?.stackFrames?.[0];
                if (stackFrame?.source?.path) {
                    cppFile = stackFrame.source.path;
                    cppLine = stackFrame.line || 1;
                    trace(`[OPEN-CPP] From session stackTrace: cppFile=${cppFile}, cppLine=${cppLine}`);
                }
            } catch (err) {
                console.error('trust: stackTrace custom request failed:', err.message);
                trace(`[OPEN-CPP] stackTrace failed: ${err.message}, fallback`);
            }
        }

        // 2. Fallback: используем настройки расширения
        if (!cppFile) {
            const config = vscode.workspace.getConfiguration('trust');
            const tempDir = config.get('tempDir', '.trust');
            const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(workspaceFolder, tempDir);
            const baseName = path.basename(editor.document.fileName, '.src');
            cppFile = path.join(resolvedTempDir, baseName + '.cpp');
            cppLine = cppLine || 1;
            trace(`[OPEN-CPP] Fallback computed path: ${cppFile}`);
        }

        // Проверяем, существует ли C++ файл перед открытием
        if (!fs.existsSync(cppFile)) {
            const msg = `Trust: C++ file not found at "${cppFile}". Build the .src file first (e.g., using the Trust: Build task).`;
            vscode.window.showWarningMessage(msg);
            trace(`[OPEN-CPP] File not found: ${cppFile}`);
            return;
        }

        // Открываем файл в новой панели (соседняя колонка)
        try {
            const doc = await vscode.workspace.openTextDocument(cppFile);
            const cppLineZeroBased = Math.max(0, (cppLine || 1) - 1);
            const editorPane = await vscode.window.showTextDocument(doc, {
                viewColumn: vscode.ViewColumn.Beside,
                selection: new vscode.Range(cppLineZeroBased, 0, cppLineZeroBased, 0)
            });
            editorPane.revealRange(new vscode.Range(cppLineZeroBased, 0, cppLineZeroBased, 10), vscode.TextEditorRevealType.InCenter);
            trace(`[OPEN-CPP] Opened ${cppFile}:${cppLine} in new editor column`);
        } catch (err) {
            vscode.window.showErrorMessage(`Trust: Could not open C++ file: ${err.message}`);
            trace(`[OPEN-CPP] Error opening file: ${err.message}`);
        }
    });

    context.subscriptions.push(openCppCmd);

    // ── Resolve debug configuration ──
    // ── Provide default launch configurations ──
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider('trust', {
            provideDebugConfigurations(folder) {
                trace('[CONFIG] provideDebugConfigurations called for folder: ' + (folder?.uri?.fsPath || '(none)'));
                return [
                    {
                        type: 'trust',
                        request: 'launch',
                        name: 'Trust Debug (current file)',
                        sourceFile: '${file}',
                        cppFile: '${workspaceFolder}/${config:trust.tempDir}/${fileBasenameNoExtension}.cpp',
                        targetFile: '${workspaceFolder}/${config:trust.tempDir}/${fileBasenameNoExtension}',
                        lldbServerPath: '',
                        mapFile: ''
                    }
                ];
            },
            async resolveDebugConfiguration(folder, debugConfiguration) {
                trace(`[CONFIG] resolveDebugConfiguration called`);
                trace(`[CONFIG] incoming debugConfiguration: ${JSON.stringify(debugConfiguration)}`);

                // If no configuration at all, create a default one
                if (!debugConfiguration) {
                    const editor = vscode.window.activeTextEditor;
                    const fileName = editor?.document?.fileName;
                    const baseName = fileName ? path.basename(fileName) : 'current file';
                    debugConfiguration = {
                        type: 'trust',
                        request: 'launch',
                        name: `Trust Debug (${baseName})`,
                        sourceFile: fileName || '${file}',
                        cppFile: '',
                        targetFile: '',
                        lldbServerPath: '',
                        mapFile: ''
                    };
                    trace(`[CONFIG] Created default configuration: ${JSON.stringify(debugConfiguration)}`);
                }

                // Проверяем, что есть активный .src файл
                const editor = vscode.window.activeTextEditor;
                if (!editor || !editor.document.fileName.endsWith('.src')) {
                    vscode.window.showErrorMessage('Trust Debug: open a .src file and try again');
                    trace(`[CONFIG] No .src file active, aborting`);
                    return null;
                }

                const workspaceFolder = folder?.uri?.fsPath || '';
                const activeFile = editor.document.fileName;

                if (!debugConfiguration.sourceFile) {
                    debugConfiguration.sourceFile = activeFile;
                }

                if (!debugConfiguration.cppFile) debugConfiguration.cppFile = '';
                if (!debugConfiguration.targetFile) debugConfiguration.targetFile = '';
                if (!debugConfiguration.lldbServerPath) debugConfiguration.lldbServerPath = '';
                if (!debugConfiguration.mapFile) debugConfiguration.mapFile = '';

                // Resolve all DAP variables
                debugConfiguration.sourceFile = resolveDapVariables(debugConfiguration.sourceFile, workspaceFolder, activeFile);
                debugConfiguration.cppFile = resolveDapVariables(debugConfiguration.cppFile, workspaceFolder, activeFile);
                debugConfiguration.targetFile = resolveDapVariables(debugConfiguration.targetFile, workspaceFolder, activeFile);
                debugConfiguration.lldbServerPath = resolveDapVariables(debugConfiguration.lldbServerPath, workspaceFolder, activeFile);
                debugConfiguration.mapFile = resolveDapVariables(debugConfiguration.mapFile, workspaceFolder, activeFile);

                trace(`[CONFIG] Resolved configuration: ${JSON.stringify(debugConfiguration)}`);
                trace(`[CONFIG] No build step (compile is separate). Using configuration as-is.`);

                return debugConfiguration;
            }
        })
    );

    // ── Reset commands ──
    registerResetCommand(context, 'dapPath', 'trust.resetDapPath');
    registerResetCommand(context, 'lspPath', 'trust.resetLspPath');

    // ── Task Provider for build/run ──
    const taskProvider = vscode.tasks.registerTaskProvider('trust', new TrustBuildTaskProvider());
    context.subscriptions.push(taskProvider);

    // ── LSP Client (vscode-languageclient) ──
    const config = vscode.workspace.getConfiguration('trust');
    const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri?.fsPath || '';
    const lspOutputChannel = vscode.window.createOutputChannel('Trust Lang LSP');

    // Путь к LSP серверу берется только из настройки trust.lspPath, без fallback
    const lspPath = resolvePath(config.get('lspPath', ''), workspaceFolder);

    if (!lspPath) {
        const msg = 'Trust LSP: path not configured. Set "trust.lspPath" in settings.';
        lspOutputChannel.appendLine(`[ERROR] ${msg}`);
        lspOutputChannel.show(true);
        updateLspStatusBar(LspStatus.ERROR, lspStatusBar);
        vscode.window.showErrorMessage(msg);
        // Пропускаем, но не прерываем активацию расширения — остальные функции продолжают работать
    } else {
        const lspArgs = [];
        if (workspaceFolder) {
            lspArgs.push('--project-dir', workspaceFolder);
        }
        // --trace если настройка включена
        if (config.get('traceLSP', false)) {
            lspArgs.push('--trace');
        }

        const serverOptions = {
            command: lspPath,
            args: lspArgs,
            options: { env: { ...process.env } }
        };

        const clientOptions = {
            documentSelector: [{ scheme: 'file', language: 'trust' }],
            synchronize: {
                configurationSection: 'trust'
            },
            outputChannel: lspOutputChannel,
            traceOutputChannel: vscode.window.createOutputChannel('Trust Lang LSP Trace'),
        };

        lspClient = new LanguageClient('trust-lsp', 'Trust Lang LSP', serverOptions, clientOptions);

        // Update LSP status bar on state changes
        updateLspStatusBar(LspStatus.STARTING, lspStatusBar);
        context.subscriptions.push(
            lspClient.onDidChangeState((stateChange) => {
                const newState = stateChange.newState;
                const oldState = stateChange.oldState;
        if (newState === 'running') {
            console.log('[TRUST-LSP] onDidChangeState: running, calling updateLspStatusBar(RUNNING)');
            updateLspStatusBar(LspStatus.RUNNING, lspStatusBar);
            lspOutputChannel.appendLine(`[INFO] LSP client state changed: ${oldState || 'initial'} → running`);
                } else if (newState === 'starting' || newState === 'initializing') {
                    updateLspStatusBar(LspStatus.STARTING, lspStatusBar);
                    lspOutputChannel.appendLine(`[INFO] LSP client state changed: ${oldState || 'initial'} → ${newState}`);
                } else if (newState === 'stopped' && (oldState === 'running' || oldState === 'starting')) {
                    // Неожиданное падение или отказ запуска процесса LSP
                    const msg = `Trust LSP: server process exited unexpectedly (state: ${oldState || 'initial'} → stopped). Check "trust.lspPath" setting.`;
                    lspOutputChannel.appendLine(`[ERROR] ${msg}`);
                    lspOutputChannel.show(true);
                    updateLspStatusBar(LspStatus.ERROR, lspStatusBar);
                } else {
                    updateLspStatusBar(null, lspStatusBar);
                    lspOutputChannel.appendLine(`[WARN] LSP client state changed: ${oldState || 'initial'} → ${newState}`);
                }
            })
        );

        // Handle LSP errors
        context.subscriptions.push(
            lspClient.onNotification('window/showMessage', (params) => {
                if (params.type === 1) { // Error
                    updateLspStatusBar(LspStatus.ERROR, lspStatusBar);
                }
            })
        );

        lspClient.start().catch((err) => {
            console.log('[TRUST-LSP] lspClient.start() rejected:', err.message);
            const msg = `Trust LSP: failed to start: ${err.message}`;
            lspOutputChannel.appendLine(`[ERROR] ${msg}`);
            lspOutputChannel.show(true);
            updateLspStatusBar(LspStatus.ERROR, lspStatusBar);
            vscode.window.showErrorMessage(msg);
        });
    }

    trace('[ACTIVATE] Extension fully initialized');
}

function deactivate() {
    // LanguageClient управляется через context.subscriptions
    // и автоматически останавливается при деактивации.
    if (traceChannel) {
        traceChannel.dispose();
        traceChannel = null;
    }
}

module.exports = {
    activate,
    deactivate
};