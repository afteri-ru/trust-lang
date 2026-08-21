/**
 * extension-utils.js - Вынесенные из extension.js функции для unit-тестирования
 */

const path = require('path');
const fs = require('fs');
const os = require('os');
const { execSync } = require('child_process');

// -- Path resolution --
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

// -- Build pipeline functions --

/**
 * Resolve a config value with workspace folder substitution.
 * @param {string} configValue - The setting value
 * @param {string} workspaceFolder - Absolute path to workspace root
 * @returns {string} Resolved absolute path
 */
function resolveConfigPath(configValue, workspaceFolder) {
    if (!configValue) return '';
    let result = configValue;
    if (result.startsWith('~/') || result === '~') {
        result = result.replace(/^~/, os.homedir());
    }
    if (workspaceFolder) {
        result = result.replace('${workspaceFolder}', workspaceFolder);
    }
    return result;
}

/**
 * Resolve temporary directory path.
 * @param {string} tempDir - Config value for tempDir (relative or absolute)
 * @param {string} workspaceFolder - Absolute path to workspace root
 * @returns {string} Resolved absolute temp directory path
 */
function resolveTempDir(tempDir, workspaceFolder) {
    if (!tempDir) tempDir = '.trust';
    const resolved = resolveConfigPath(tempDir, workspaceFolder);
    return path.isAbsolute(resolved) ? resolved : path.join(workspaceFolder, resolved);
}

/**
 * Compute build paths for a given source file.
 * @param {string} sourceFile - Absolute path to .src file
 * @param {string} workspaceFolder - Absolute path to workspace root
 * @param {object} config - VS Code trust configuration
 * @returns {{ cppFile: string, targetFile: string, tempDir: string }}
 */
function computeBuildPaths(sourceFile, workspaceFolder, config) {
    const baseName = path.basename(sourceFile, '.src');
    const tempDir = config.get('tempDir', '.trust');
    const resolvedTempDir = resolveTempDir(tempDir, workspaceFolder);
    const cppFile = path.join(resolvedTempDir, baseName + '.cpp');
    const targetFile = path.join(resolvedTempDir, baseName);
    return { cppFile, targetFile, tempDir: resolvedTempDir };
}

/**
 * Transpile a .src file to C++.
 * @param {string} sourceFile - Absolute path to .src file
 * @param {string} tempDir - Absolute path to temp directory
 * @param {string} compilerPath - Trust compiler executable path
 * @param {string} workspaceFolder - Absolute path to workspace root
 * @returns {{ success: boolean, cppFile: string, stdout: string, stderr: string }}
 */
function transpileSource(sourceFile, tempDir, compilerPath, workspaceFolder) {
    const baseName = path.basename(sourceFile, '.src');
    const cppFile = path.join(tempDir, baseName + '.cpp');

    fs.mkdirSync(tempDir, { recursive: true });

    const args = [
        sourceFile,
        '--emit-cpp', cppFile,
        '--temp-dir', tempDir
    ];

    try {
        const result = execSync(`"${compilerPath}" ${args.map(a => `"${a}"`).join(' ')}`, {
            cwd: workspaceFolder,
            encoding: 'utf-8',
            timeout: 60000,
            maxBuffer: 10 * 1024 * 1024
        });
        return { success: true, cppFile, stdout: result || '', stderr: '' };
    } catch (err) {
        return { success: false, cppFile, stdout: err.stdout || '', stderr: err.stderr || err.message };
    }
}

/**
 * Compile a C++ file to an ELF binary.
 * @param {string} cppFile - Absolute path to .cpp file
 * @param {string} targetFile - Absolute path for the output ELF binary
 * @param {string} cppCompilerPath - C++ compiler executable path
 * @param {string} cppCompilerOptions - Compiler option string (e.g. "-std=c++23 -g3 -O0")
 * @param {string} workspaceFolder - Absolute path to workspace root
 * @returns {{ success: boolean, targetFile: string, stdout: string, stderr: string }}
 */
function compileCpp(cppFile, targetFile, cppCompilerPath, cppCompilerOptions, workspaceFolder) {
    const options = (cppCompilerOptions || '-std=c++23 -g3 -O0').split(/\s+/).filter(s => s);
    const args = [...options, '-o', targetFile, cppFile];

    try {
        const result = execSync(`"${cppCompilerPath}" ${args.map(a => `"${a}"`).join(' ')}`, {
            cwd: workspaceFolder,
            encoding: 'utf-8',
            timeout: 120000,
            maxBuffer: 10 * 1024 * 1024
        });
        return { success: true, targetFile, stdout: result || '', stderr: '' };
    } catch (err) {
        return { success: false, targetFile, stdout: err.stdout || '', stderr: err.stderr || err.message };
    }
}

/**
 * Full build pipeline: transpile + compile for debugging.
 * @param {string} sourceFile - Absolute path to .src file
 * @param {string} workspaceFolder - Absolute path to workspace root
 * @param {object} config - VS Code workspace configuration section
 * @returns {{ success: boolean, cppFile?: string, targetFile?: string, error?: string }}
 */
function buildForDebug(sourceFile, workspaceFolder, config) {
    const compilerPath = resolveConfigPath(config.get('compilerPath', 'trust'), workspaceFolder);
    const cppCompilerPath = resolveConfigPath(config.get('cppCompilerPath', 'clang++-22'), workspaceFolder);
    const cppCompilerOptions = config.get('cppCompilerOptions', '-std=c++23 -g3 -O0');
    const { cppFile, targetFile, tempDir } = computeBuildPaths(sourceFile, workspaceFolder, config);

    // Step 1: Transpile
    const transpileResult = transpileSource(sourceFile, tempDir, compilerPath, workspaceFolder);
    if (!transpileResult.success) {
        return { success: false, error: `Transpile failed:\n${transpileResult.stderr}` };
    }

    // Step 2: Compile C++ to ELF
    const compileResult = compileCpp(cppFile, targetFile, cppCompilerPath, cppCompilerOptions, workspaceFolder);
    if (!compileResult.success) {
        return { success: false, error: `Compile failed:\n${compileResult.stderr}` };
    }

    return { success: true, cppFile, targetFile };
}


// -- Pseudoterminal для custom execution задач --
function createPseudoterminal(name, command, args, cwd) {
    const writeEmitter = new vscode.EventEmitter();
    const closeEmitter = new vscode.EventEmitter();
    let killed = false;

    const pty = {
        onDidWrite: writeEmitter.event,
        onDidClose: closeEmitter.event,
        open: () => {
            writeEmitter.fire(`[Trust] ${name}\r\n`);
        },
        close: () => {},
        handleInput: (data) => {
            // Ignore input
        }
    };

    // Run the actual command in the background
    (async () => {
        try {
            const { execSync } = require('child_process');
            const cmdStr = `"${command}" ${args.map(a => `"${a}"`).join(' ')}`;
            writeEmitter.fire(`$ ${cmdStr}\r\n`);
            const stdout = execSync(cmdStr, {
                cwd: cwd,
                encoding: 'utf-8',
                timeout: 120000,
                maxBuffer: 10 * 1024 * 1024
            });
            if (stdout) {
                writeEmitter.fire(stdout);
            }
            writeEmitter.fire(`\r\n[Trust] ${name} completed successfully\r\n`);
        } catch (err) {
            writeEmitter.fire(`${err.stderr || err.message}\r\n`);
            writeEmitter.fire(`\r\n[Trust] ${name} FAILED\r\n`);
        } finally {
            if (!killed) {
                setTimeout(() => closeEmitter.fire(0), 500);
            }
        }
    })();

    return { pty, kill: () => { killed = true; } };
}

module.exports = {
    resolvePath,
    resolveDapVariables,
    resolveConfigPath,
    resolveTempDir,
    computeBuildPaths,
    transpileSource,
    compileCpp,
    buildForDebug,
    updateStatusBar,
    updateLspStatusBar,
    LspStatus,
    createPseudoterminal
};
