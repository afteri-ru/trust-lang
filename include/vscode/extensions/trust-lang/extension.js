const vscode = require('vscode');
const path = require('path');
const fs = require('fs');

let buildStatusBar;
let isBuilding = false;
let isDebugging = false;

function resolvePath(configPath, workspaceFolder) {
    if (workspaceFolder) {
        return configPath.replace('${workspaceFolder}', workspaceFolder);
    }
    return configPath;
}

function updateStatusBar() {
    if (!buildStatusBar) return;

    if (isBuilding) {
        buildStatusBar.text = '$(sync~spin) Trust: Building...';
        buildStatusBar.tooltip = 'Compiling Trust source files';
    } else if (isDebugging) {
        buildStatusBar.text = '$(debug-start) Trust Debug: Running';
        buildStatusBar.tooltip = 'Debug session is active';
    } else {
        buildStatusBar.text = '$(debug) Trust Debug: Idle';
        buildStatusBar.tooltip = 'Trust Debugger - Ready';
    }
    buildStatusBar.show();
}

class TrustDebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(session) {
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        const config = vscode.workspace.getConfiguration('trust');
        let dapPath = config.get('dapPath', 'trust-dap');
        dapPath = resolvePath(dapPath, workspaceFolder);

        if (!fs.existsSync(dapPath)) {
            vscode.window.showErrorMessage(`Trust Debug Adapter not found at: ${dapPath}`);
            throw new Error(`Trust Debug Adapter not found at: ${dapPath}`);
        }

        return new vscode.DebugAdapterExecutable(dapPath, []);
    }
}

function activate(context) {
    console.log('Trust Lang extension is now active');

    buildStatusBar = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    buildStatusBar.command = 'trust.build';
    context.subscriptions.push(buildStatusBar);
    updateStatusBar();

    const factory = new TrustDebugAdapterDescriptorFactory();
    context.subscriptions.push(
        vscode.debug.registerDebugAdapterDescriptorFactory('trust', factory)
    );

    context.subscriptions.push(
        vscode.debug.onDidStartDebugSession((session) => {
            if (session.type === 'trust') {
                isDebugging = true;
                isBuilding = false;
                updateStatusBar();
            }
        })
    );

    context.subscriptions.push(
        vscode.debug.onDidTerminateDebugSession((session) => {
            if (session.type === 'trust') {
                isDebugging = false;
                updateStatusBar();
            }
        })
    );

    const buildCmd = vscode.commands.registerCommand('trust.build', async () => {
        if (!vscode.workspace.workspaceFolders?.length) {
            vscode.window.showErrorMessage('No workspace folder open');
            return;
        }

        if (isDebugging) {
            vscode.window.showWarningMessage('Cannot build while debugging. Stop the debug session first.');
            return;
        }

        const workspaceFolder = vscode.workspace.workspaceFolders[0].uri.fsPath;
        const config = vscode.workspace.getConfiguration('trust');
        let compilerPath = config.get('compilerPath', 'trust');
        compilerPath = resolvePath(compilerPath, workspaceFolder);

        const outDir = path.join(workspaceFolder, '.trust');

        if (!fs.existsSync(compilerPath)) {
            vscode.window.showErrorMessage(`Trust compiler not found at: ${compilerPath}`);
            return;
        }

        if (!fs.existsSync(outDir)) {
            fs.mkdirSync(outDir, { recursive: true });
        }

        const srcSearchDirs = [
            path.join(workspaceFolder, 'tests', 'dap'),
            outDir
        ];

        let srcFiles = [];
        for (const dir of srcSearchDirs) {
            if (fs.existsSync(dir)) {
                const found = fs.readdirSync(dir).filter(f => f.endsWith('.src'));
                srcFiles = srcFiles.concat(found.map(f => path.join(dir, f)));
            }
        }

        if (srcFiles.length === 0) {
            vscode.window.showWarningMessage('No .src files found');
            return;
        }

        isBuilding = true;
        updateStatusBar();

        const terminal = vscode.window.createTerminal({ name: 'Trust Build' });
        terminal.show(true);

        for (const srcPath of srcFiles) {
            const baseName = path.basename(srcPath, '.src');
            const cppPath = path.join(outDir, baseName + '.cpp');
            const elfPath = path.join(outDir, baseName);

            terminal.sendText(`"${compilerPath}" "${srcPath}" "${cppPath}"`);
            await new Promise(resolve => setTimeout(resolve, 1000));

            terminal.sendText(`clang++ -std=c++17 -g3 -O0 -o "${elfPath}" "${cppPath}"`);
            await new Promise(resolve => setTimeout(resolve, 1000));
        }

        isBuilding = false;
        updateStatusBar();
        vscode.window.showInformationMessage('Trust build completed');
    });

    const stopCmd = vscode.commands.registerCommand('trust.stop', () => {
        if (isDebugging) {
            vscode.commands.executeCommand('workbench.action.debug.stop');
        } else {
            vscode.window.showInformationMessage('No active debug session');
        }
    });

    context.subscriptions.push(buildCmd, stopCmd);
}

function deactivate() {
    buildStatusBar?.dispose();
}

module.exports = {
    activate,
    deactivate
};