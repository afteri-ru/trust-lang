/**
 * build-task.js - TrustBuildTask для preLaunchTask
 * Предоставляет задачи "Trust: Transpile .src", "Trust: Compile .cppt" и "Trust: Build all"
 */

const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const { resolveConfigPath, createPseudoterminal } = require('./extension-utils');

class TrustBuildTask {
    static buildTaskType = 'trust-build';

    static getBuildCppTask(sourceFile, workspaceFolder) {
        const task = new vscode.Task(
            { type: TrustBuildTask.buildTaskType },
            workspaceFolder,
            'Trust: Transpile .src',
            'trust',
            new vscode.CustomExecution(async () => {
                const config = vscode.workspace.getConfiguration('trust');
                const baseName = path.basename(sourceFile, '.src');
                const tempDir = config.get('tempDir', '.trust');
                const cwd = workspaceFolder.uri.fsPath;
                const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(cwd, tempDir);
                const cppFile = path.join(resolvedTempDir, baseName + '.cppt');

                fs.mkdirSync(resolvedTempDir, { recursive: true });

                const compilerPath = resolveConfigPath(config.get('compilerPath', 'trust'), cwd);
                return createPseudoterminal(
                    'Transpiling ' + baseName + '.src',
                    compilerPath,
                    [sourceFile, '--emit-cpp', cppFile, '--temp-dir', resolvedTempDir],
                    cwd
                ).pty;
            })
        );
        task.group = vscode.TaskGroup.Build;
        task.problemMatchers = [];
        return task;
    }

    static getCompileTask(sourceFile, workspaceFolder) {
        const task = new vscode.Task(
            { type: TrustBuildTask.buildTaskType },
            workspaceFolder,
            'Trust: Compile .cppt',
            'trust',
            new vscode.CustomExecution(async () => {
                const config = vscode.workspace.getConfiguration('trust');
                const baseName = path.basename(sourceFile, '.src');
                const tempDir = config.get('tempDir', '.trust');
                const cwd = workspaceFolder.uri.fsPath;
                const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(cwd, tempDir);
                const cppFile = path.join(resolvedTempDir, baseName + '.cppt');
                const targetFile = path.join(resolvedTempDir, baseName);

                const cppCompilerPath = resolveConfigPath(config.get('cppCompilerPath', 'clang++-22'), cwd);
                const cppCompilerOptions = (config.get('cppCompilerOptions', '-std=c++23 -g3 -O0') || '').split(/\s+/).filter(s => s);
                const compileArgs = ['-x', 'c++', ...cppCompilerOptions, '-o', targetFile, cppFile];

                return createPseudoterminal(
                    'Compiling ' + baseName + '.cppt',
                    cppCompilerPath,
                    compileArgs,
                    cwd
                ).pty;
            })
        );
        task.group = vscode.TaskGroup.Build;
        task.problemMatchers = [];
        return task;
    }

    static getBuildAllTask(sourceFile, workspaceFolder) {
        const task = new vscode.Task(
            { type: TrustBuildTask.buildTaskType },
            workspaceFolder,
            'Trust: Build all',
            'trust',
            new vscode.CustomExecution(async () => {
                const config = vscode.workspace.getConfiguration('trust');
                const baseName = path.basename(sourceFile, '.src');
                const tempDir = config.get('tempDir', '.trust');
                const cwd = workspaceFolder.uri.fsPath;
                const resolvedTempDir = path.isAbsolute(tempDir) ? tempDir : path.join(cwd, tempDir);
                const cppFile = path.join(resolvedTempDir, baseName + '.cppt');
                const targetFile = path.join(resolvedTempDir, baseName);

                fs.mkdirSync(resolvedTempDir, { recursive: true });

                const compilerPath = resolveConfigPath(config.get('compilerPath', 'trust'), cwd);
                const cppCompilerPath = resolveConfigPath(config.get('cppCompilerPath', 'clang++-22'), cwd);
                const cppCompilerOptions = (config.get('cppCompilerOptions', '-std=c++23 -g3 -O0') || '').split(/\s+/).filter(s => s);
                const compileArgs = ['-x', 'c++', ...cppCompilerOptions, '-o', targetFile, cppFile];

                const writeEmitter = new vscode.EventEmitter();
                const closeEmitter = new vscode.EventEmitter();
                const pty = {
                    onDidWrite: writeEmitter.event,
                    onDidClose: closeEmitter.event,
                    open: () => { writeEmitter.fire('[Trust] Building all...\r\n'); },
                    close: () => {},
                    handleInput: () => {}
                };

                const runStep = (label, cmd, args) => {
                    const term = createPseudoterminal(label, cmd, args, cwd);
                    return new Promise((resolve, reject) => {
                        (async () => {
                            try {
                                const { execSync } = require('child_process');
                                const cmdStr = `"${cmd}" ${args.map(a => `"${a}"`).join(' ')}`;
                                writeEmitter.fire(`$ ${cmdStr}\r\n`);
                                const stdout = execSync(cmdStr, {
                                    cwd: cwd,
                                    encoding: 'utf-8',
                                    timeout: 120000,
                                    maxBuffer: 10 * 1024 * 1024
                                });
                                if (stdout) writeEmitter.fire(stdout);
                                writeEmitter.fire(`\r\n[Trust] ${label} succeeded\r\n`);
                                resolve();
                            } catch (err) {
                                writeEmitter.fire(`\r\n${err.stderr || err.message}\r\n`);
                                writeEmitter.fire(`\r\n[Trust] ${label} FAILED\r\n`);
                                reject(err);
                            }
                        })();
                    });
                };

                try {
                    await runStep('Transpile', compilerPath, [sourceFile, '--emit-cpp', cppFile, '--temp-dir', resolvedTempDir]);
                    await runStep('Compile', cppCompilerPath, compileArgs);
                    writeEmitter.fire(`\r\n[Trust] Build all completed successfully\r\n`);
                } catch (err) {
                    writeEmitter.fire(`\r\n[Trust] Build all FAILED\r\n`);
                } finally {
                    setTimeout(() => closeEmitter.fire(0), 500);
                }

                return pty;
            })
        );
        task.group = vscode.TaskGroup.Build;
        task.problemMatchers = [];
        return task;
    }
}

module.exports = { TrustBuildTask };