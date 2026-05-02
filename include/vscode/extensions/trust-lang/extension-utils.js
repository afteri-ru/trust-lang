/**
 * extension-utils.js — Вынесенные из extension.js функции для unit-тестирования
 */

const path = require('path');
const fs = require('fs');
const os = require('os');

// ── Path resolution ──
function resolvePath(configPath, workspaceFolder) {
    let result = configPath || '';
    if (workspaceFolder && result) {
        result = result.replace('${workspaceFolder}', workspaceFolder);
    }
    if (result.startsWith('~/') || result === '~') {
        result = result.replace(/^~/, os.homedir());
    }
    return result;
}

function resolveDapVariables(value, workspaceFolder, activeFile) {
    if (!value) return value;
    let result = value;
    if (result.startsWith('~/') || result === '~') {
        result = result.replace(/^~/, os.homedir());
    }
    if (workspaceFolder) {
        result = result.replaceAll('${workspaceFolder}', workspaceFolder);
    }
    if (activeFile) {
        result = result.replaceAll('${file}', activeFile);
        result = result.replaceAll('${fileBasename}', path.basename(activeFile));
        result = result.replaceAll('${fileBasenameNoExtension}', path.basename(activeFile).replace(/\.[^.]+$/, ''));
        result = result.replaceAll('${fileDirname}', path.dirname(activeFile));
        result = result.replaceAll('${fileExtname}', path.extname(activeFile));
    }
    return result;
}

function updateStatusBar(isDebugging, buildStatusBar) {
    if (!buildStatusBar) return;
    if (isDebugging) {
        buildStatusBar.text = '$(debug-start) Trust Debug: Running';
        buildStatusBar.tooltip = 'Debug session is active';
    } else {
        buildStatusBar.text = '$(debug) Trust Debug: Idle';
        buildStatusBar.tooltip = 'Trust Debugger - Ready';
    }
    buildStatusBar.show();
}

// LSP status bar states
const LspStatus = {
    STARTING: 'starting',
    RUNNING: 'running',
    ERROR: 'error'
};

function updateLspStatusBar(status, lspStatusBar) {
    if (!lspStatusBar) return;
    switch (status) {
        case LspStatus.STARTING:
            lspStatusBar.text = '$(sync~spin) Trust LSP: Starting...';
            lspStatusBar.tooltip = 'LSP server is starting';
            break;
        case LspStatus.RUNNING:
            lspStatusBar.text = '$(check) Trust LSP: Running';
            lspStatusBar.tooltip = 'LSP server is active';
            break;
        case LspStatus.ERROR:
            lspStatusBar.text = '$(error) Trust LSP: Error';
            lspStatusBar.tooltip = 'LSP server encountered an error';
            break;
        default:
            lspStatusBar.text = '$(circle-slash) Trust LSP: Stopped';
            lspStatusBar.tooltip = 'LSP server is not running';
            break;
    }
    lspStatusBar.show();
}

module.exports = {
    resolvePath,
    resolveDapVariables,
    updateStatusBar,
    updateLspStatusBar,
    LspStatus
};
