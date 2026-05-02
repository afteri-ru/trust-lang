const vscode = require('vscode');
const path = require('path');
const { resolvePath, resolveDapVariables } = require('./extension-utils');

/**
 * Factory creating DebugAdapterExecutable for the Trust DAP server.
 * 
 * Пути к файлам (sourceFile, cppFile, targetFile, mapFile) передаются
 * не через CLI-аргументы, а через DAP-запрос launch.
 * 
 * CLI-аргументы trust-dap:
 *   --project-dir <path>    рабочая директория проекта
 *   --lldb-server <path>    путь к lldb-server
 *   server[=<port>]         TCP-режим (если нужно)
 */
class TrustDebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(session) {
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    const config = vscode.workspace.getConfiguration('trust');
    let dapPath = resolvePath(config.get('dapPath', ''), workspaceFolder);

    if (!dapPath) {
        const msg = 'Trust Debug Adapter: path not configured. Set "trust.dapPath" in settings.';
        vscode.window.showErrorMessage(msg);
        throw new Error(msg);
    }

        const launchCfg = session.configuration || {};
        const activeFile = vscode.window.activeTextEditor?.document?.fileName || '';

        const args = [];

        // --project-dir: рабочая директория проекта (из конфигурации или workspace)
        const projectDir = resolveDapVariables(
            launchCfg.projectDir || workspaceFolder || '',
            workspaceFolder,
            activeFile
        );
        if (projectDir) {
            args.push('--project-dir', projectDir);
        }

        // --lldb-server: путь к lldb-server
        let lldbServerPath = launchCfg.lldbServerPath;
        if (!lldbServerPath) {
            lldbServerPath = config.get('lldbServerPath', '');
        }
        if (lldbServerPath) {
            args.push('--lldb-server', resolveDapVariables(lldbServerPath, workspaceFolder, activeFile));
        }

        return new vscode.debug.DebugAdapterExecutable(dapPath, args);
    }
}

module.exports = { TrustDebugAdapterDescriptorFactory };