console.log('[TRUST-LANG] extension.js module loaded (top-level)');
const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { resolvePath, resolveDapVariables, resolveConfigPath, resolveTempDir, computeBuildPaths, buildForDebug, updateStatusBar, updateLspStatusBar, LspStatus } = require('./extension-utils');
const { TrustDebugAdapterDescriptorFactory, TrustDebugAdapterTracker, getTraceChannel, writeTrace, writeDiag } = require('./dap-adapter');
const { TrustBuildTask } = require('./build-task');
const { LanguageClient } = require('vscode-languageclient/node');

let isDebugging = false;
let lspClient = null;
let buildStatusBar = null;

// ── Output channel for tracing (re-export from dap-adapter) ──
let traceChannel = null;

// Функция trace для совместимости с существующими вызовами (всегда пишет диагностику)
function trace(msg) {
    writeDiag(msg);
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
    updateLspStatusBar(null, lspStatusBar);
    // Show only when a .src file is active
    const updateLspStatusBarVisibility = () => {
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.fileName.endsWith('.src')) {
            lspStatusBar.show();
        } else {
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

    // ── Register DebugAdapterTrackerFactory for DAP tracing ──
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterTrackerFactory('trust', {
            createDebugAdapterTracker(session) {
                writeTrace(`[DAP-TRACKER] Creating tracker for session: ${session.name}`);
                return new TrustDebugAdapterTracker();
            }
        })
    );
    writeTrace('[ACTIVATE] DebugAdapterTrackerFactory registered for type "trust"');

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

    // ── Provide debug configuration (предлагаемые launch.json шаблоны) ──
    context.subscriptions.push(
        vscode.debug.registerDebugConfigurationProvider('trust', {
            provideDebugConfigurations(folder) {
                trace('[CONFIG] provideDebugConfigurations called for folder: ' + (folder?.uri?.fsPath || '(none)'));
                const workspaceFolder = folder?.uri?.fsPath || '';
                const config = vscode.workspace.getConfiguration('trust');
                const tempDir = config.get('tempDir', '.trust');
                const launchConfig = {
                    type: 'trust',
                    request: 'launch',
                    name: 'Trust Debug (current file)',
                    sourceFile: '${file}',
                    cppFile: '${workspaceFolder}/' + tempDir + '/${fileBasenameNoExtension}.cpp',
                    targetFile: '${workspaceFolder}/' + tempDir + '/${fileBasenameNoExtension}',
                    gdbPath: ''
                };
                trace(`[CONFIG] Returning initial configuration: ${JSON.stringify(launchConfig)}`);
                return [launchConfig];
            },
            async resolveDebugConfiguration(folder, debugConfiguration) {
                trace(`[CONFIG] resolveDebugConfiguration called`);
                trace(`[CONFIG] incoming debugConfiguration: ${JSON.stringify(debugConfiguration)}`);

                // If no configuration at all (F5 on .src without launch.json), create a default one
                if (!debugConfiguration || !debugConfiguration.type) {
                    const editor = vscode.window.activeTextEditor;
                    const fileName = editor?.document?.fileName || '';
                    const baseName = fileName ? path.basename(fileName, '.src') : 'program';
                    const workspaceFolder = folder?.uri?.fsPath || '';
                    const config = vscode.workspace.getConfiguration('trust');

                    if (!fileName.endsWith('.src')) {
                        vscode.window.showErrorMessage('Trust Debug: open a .src file and try again');
                        trace(`[CONFIG] No .src file active, aborting`);
                        return null;
                    }

                    // Resolve paths immediately
                    const tempDir = config.get('tempDir', '.trust');
                    const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(workspaceFolder, tempDir);

                    debugConfiguration = {
                        type: 'trust',
                        request: 'launch',
                        name: `Trust Debug (${baseName})`,
                        sourceFile: fileName,
                        cppFile: path.join(resolvedTempDir, baseName + '.cpp'),
                        targetFile: path.join(resolvedTempDir, baseName),
                        gdbPath: ''
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

                // Resolve DAP variables
                debugConfiguration.sourceFile = resolveDapVariables(debugConfiguration.sourceFile || activeFile, workspaceFolder, activeFile);

                // If cppFile or targetFile are still template-like, resolve them
                if (!debugConfiguration.cppFile || debugConfiguration.cppFile.includes('${')) {
                    const config = vscode.workspace.getConfiguration('trust');
                    const tempDir = config.get('tempDir', '.trust');
                    const baseName = path.basename(activeFile, '.src');
                    const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(workspaceFolder, tempDir);
                    debugConfiguration.cppFile = path.join(resolvedTempDir, baseName + '.cpp');
                    debugConfiguration.targetFile = path.join(resolvedTempDir, baseName);
                } else {
                    debugConfiguration.cppFile = resolveDapVariables(debugConfiguration.cppFile, workspaceFolder, activeFile);
                    debugConfiguration.targetFile = resolveDapVariables(debugConfiguration.targetFile, workspaceFolder, activeFile);
                }
                debugConfiguration.gdbPath = resolveDapVariables(debugConfiguration.gdbPath || '', workspaceFolder, activeFile);

                // ── Build pipeline: transpile + compile ──
                // NOTE: Если используется preLaunchTask в launch.json, этот блок всё равно выполняется
                // для случая без launch.json (F5 сразу). Когда есть preLaunchTask — VSCode запускает
                // сборку через него, но resolveDebugConfiguration всё равно вызывается.
                // Здесь buildForDebug — fallback, если preLaunchTask не отработал.
                trace(`[CONFIG] Starting build pipeline for: ${activeFile}`);
                const config = vscode.workspace.getConfiguration('trust');

                const buildResult = await vscode.window.withProgress({
                    location: vscode.ProgressLocation.Notification,
                    title: 'Trust: Building for debug...',
                    cancellable: false
                }, async (progress) => {
                    progress.report({ message: 'Transpiling and compiling...' });
                    return buildForDebug(activeFile, workspaceFolder, config);
                });

                if (!buildResult.success) {
                    const msg = `Trust Debug: build failed.\n${buildResult.error}`;
                    vscode.window.showErrorMessage(msg);
                    trace(`[CONFIG] Build failed: ${buildResult.error}`);
                    return null;
                }

                trace(`[CONFIG] Build succeeded: cppFile=${buildResult.cppFile}, targetFile=${buildResult.targetFile}`);

                // Заполняем конфигурацию
                debugConfiguration.cppFile = buildResult.cppFile;
                debugConfiguration.targetFile = buildResult.targetFile;

                trace(`[CONFIG] Final configuration: ${JSON.stringify(debugConfiguration)}`);
                trace(`[CONFIG] Build step completed, starting debug session`);

                return debugConfiguration;
            }
        })
    );

    // ── Build task provider для preLaunchTask ──
    // Предоставляет задачи "Trust: Transpile .src", "Trust: Compile .cpp" и "Trust: Build all"
    const buildTaskProvider = vscode.tasks.registerTaskProvider(TrustBuildTask.buildTaskType, {
        provideTasks(token) {
            const editor = vscode.window.activeTextEditor;
            if (!editor || !editor.document.fileName.endsWith('.src')) {
                return [];
            }
            const sourceFile = editor.document.fileName;
            const workspaceFolder = vscode.workspace.workspaceFolders?.[0];
            if (!workspaceFolder) return [];

            const transpileTask = TrustBuildTask.getBuildCppTask(sourceFile, workspaceFolder);
            const compileTask = TrustBuildTask.getCompileTask(sourceFile, workspaceFolder);
            const buildAllTask = TrustBuildTask.getBuildAllTask(sourceFile, workspaceFolder);
            return [transpileTask, compileTask, buildAllTask];
        },
        resolveTask(task, token) {
            return task;
        }
    });
    context.subscriptions.push(buildTaskProvider);

    // ── Reset commands ──
    registerResetCommand(context, 'dapPath', 'trust.resetDapPath');
    registerResetCommand(context, 'lspPath', 'trust.resetLspPath');

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
        // --temp-dir: каталог для временных транспилированных .cpp файлов
        const tempDir = config.get('tempDir', '.trust');
        const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(workspaceFolder, tempDir);
        lspArgs.push('--temp-dir', resolvedTempDir);
        // --trace если настройка включена
        if (config.get('traceLSP', false)) {
            lspArgs.push('--trace');
        }

        const serverOptions = {
            command: lspPath,
            args: lspArgs,
            options: { env: { ...process.env } }
        };

        // Промежуточное ПО: оставляем documentLink как есть (с целевым URI).
        // VSCode сам обработает клик по ссылке и откроет целевой файл.
        const lspMiddleware = {};

        const clientOptions = {
            documentSelector: [
                { scheme: 'file', language: 'trust' },
                { scheme: 'file', language: 'cpp' }
            ],
            synchronize: {
                configurationSection: 'trust'
            },
            middleware: lspMiddleware,
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
    deactivate,
    TrustBuildTask
};
