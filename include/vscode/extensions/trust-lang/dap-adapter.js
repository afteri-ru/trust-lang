const vscode = require('vscode');
const fs = require('fs');
const { resolvePath, resolveDapVariables } = require('./extension-utils');

const TRACE_CHANNEL_NAME = 'Trust Lang';

let _traceChannel = null;
function getTraceChannel() {
    if (!_traceChannel) {
        _traceChannel = vscode.window.createOutputChannel(TRACE_CHANNEL_NAME);
    }
    return _traceChannel;
}

function resetTraceChannel() {
    _traceChannel = null;
}

function isDapTraceEnabled() {
    return vscode.workspace.getConfiguration('trust').get('traceDAP', false);
}

function writeTrace(msg, isError = false, dapTraceOnly = true) {
    const channel = getTraceChannel();
    if (isError) {
        channel.appendLine(msg);
        channel.show(true);
    } else if (!dapTraceOnly || isDapTraceEnabled()) {
        channel.appendLine(msg);
    }
}

function writeDiag(msg) {
    const channel = getTraceChannel();
    channel.appendLine(msg);
    channel.show(true);
}

class TrustDebugAdapterTracker {
    constructor() {
        this._startTime = Date.now();
        this._channel = getTraceChannel();
    }

    _log(msg) { this._channel.appendLine(msg); }

    onWillReceiveMessage(msg) {
        if (!msg) return;
        const type = msg.type || 'unknown';
        if (type === 'request') {
            const command = msg.command || '?';
            const seq = msg.seq || '?';
            const args = msg.arguments ? JSON.stringify(msg.arguments).substring(0, 300) : '';
            this._log(`[DAP->] ${command} seq=${seq} ${args}`);
        } else {
            this._log(`[DAP->] ${type} ${JSON.stringify(msg).substring(0, 200)}`);
        }
    }

    onDidSendMessage(msg) {
        if (!msg) return;
        const type = msg.type || 'unknown';
        if (type === 'response') {
            const command = msg.command || '?';
            const reqSeq = msg.request_seq || '?';
            const success = msg.success !== false;
            const body = msg.body ? JSON.stringify(msg.body).substring(0, 300) : '';
            this._log(`[DAP<-] ${command} req_seq=${reqSeq} success=${success} ${body}`);
        } else if (type === 'event') {
            const event = msg.event || '?';
            const body = msg.body ? JSON.stringify(msg.body).substring(0, 300) : '';
            this._log(`[DAP-EVT] ${event} ${body}`);
        } else {
            this._log(`[DAP<-] ${type} ${JSON.stringify(msg).substring(0, 200)}`);
        }
    }

    onWillStartSession() { this._log('[DAP-SESSION] Session starting...'); }
    onWillStopSession() { this._log('[DAP-SESSION] Session stopped'); }
    onError(error) {
        const msg = error ? (error.message || String(error)) : 'Unknown error';
        this._log(`[DAP-ERR] ${msg}`);
        if (error && error.stack) { this._log(`[DAP-ERR] Stack: ${error.stack.substring(0, 500)}`); }
    }
}

class TrustDebugAdapterDescriptorFactory {
    createDebugAdapterDescriptor(session) {
        const workspaceFolder = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
        const config = vscode.workspace.getConfiguration('trust');
        const dapPath = resolvePath(config.get('dapPath', ''), workspaceFolder);

        writeDiag(`[DAP-DESC] createDebugAdapterDescriptor called`);
        writeDiag(`[DAP-DESC] workspaceFolder=${workspaceFolder}`);
        writeDiag(`[DAP-DESC] dapPath=${dapPath}`);

        if (!dapPath) {
            const msg = 'Trust Debug Adapter: path not configured.';
            writeTrace(`[DAP-DESC] ERROR: ${msg}`, true);
            vscode.window.showErrorMessage(msg);
            throw new Error(msg);
        }

        const dapPathResolved = dapPath.startsWith('~')
            ? dapPath.replace(/^~/, require('os').homedir())
            : dapPath;
        const exists = fs.existsSync(dapPathResolved);
        writeDiag(`[DAP-DESC] resolvedPath=${dapPathResolved} exists=${exists}`);

        if (!exists) {
            const msg = `Trust Debug Adapter: binary not found at "${dapPathResolved}".`;
            writeTrace(`[DAP-DESC] ERROR: ${msg}`, true);
            vscode.window.showErrorMessage(msg);
            throw new Error(msg);
        }

        const launchCfg = session.configuration || {};
        const activeFile = vscode.window.activeTextEditor?.document?.fileName || '';

        const projectDir = resolveDapVariables(
            launchCfg.projectDir || workspaceFolder || '',
            workspaceFolder,
            activeFile
        );

        writeDiag(`[DAP-DESC] projectDir=${projectDir}`);

        // Use DebugAdapterExecutable - stable API that spawns trust-dap as a process
        const execArgs = ['--project-dir', projectDir];
        const exec = new vscode.DebugAdapterExecutable(dapPathResolved, execArgs);
        writeDiag(`[DAP-DESC] Created DebugAdapterExecutable: ${dapPathResolved} ${JSON.stringify(execArgs)}`);
        return exec;
    }
}

module.exports = { TrustDebugAdapterDescriptorFactory, TrustDebugAdapterTracker, getTraceChannel, writeTrace, writeDiag, resetTraceChannel };