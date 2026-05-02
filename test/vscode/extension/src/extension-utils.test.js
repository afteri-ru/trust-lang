/**
 * Unit tests for extension-utils.js
 */

const path = require('path');
const os = require('os');

// moduleNameMapper in jest.config.js maps 'vscode' -> __mocks__/vscode.js
const utils = require('extension-utils');

describe('extension-utils: resolvePath', () => {
    test('resolves empty input to empty string', () => {
        expect(utils.resolvePath('', '/workspace')).toBe('');
        expect(utils.resolvePath(null, '/workspace')).toBe('');
        expect(utils.resolvePath(undefined, '/workspace')).toBe('');
    });

    test('replaces ${workspaceFolder} placeholder', () => {
        expect(utils.resolvePath('${workspaceFolder}/build', '/my/project')).toBe('/my/project/build');
    });

    test('interprets ~ as home directory', () => {
        const result = utils.resolvePath('~/tools/trust', '/workspace');
        expect(result).toBe(path.join(os.homedir(), 'tools/trust'));
    });

    test('returns absolute path unchanged', () => {
        expect(utils.resolvePath('/usr/local/bin/trust', '/workspace')).toBe('/usr/local/bin/trust');
    });

    test('returns relative path unchanged (no workspaceFolder substitution needed)', () => {
        expect(utils.resolvePath('./build', '/workspace')).toBe('./build');
    });
});

describe('extension-utils: resolveDapVariables', () => {
    const ws = '/home/user/project';
    const activeFile = '/home/user/project/src/main.src';

    test('returns undefined for null/undefined input', () => {
        expect(utils.resolveDapVariables(null, ws, activeFile)).toBeNull();
        expect(utils.resolveDapVariables(undefined, ws, activeFile)).toBeUndefined();
    });

    test('replaces ${workspaceFolder}', () => {
        expect(utils.resolveDapVariables('${workspaceFolder}/build', ws, activeFile))
            .toBe('/home/user/project/build');
    });

    test('replaces ${file} and variants', () => {
        expect(utils.resolveDapVariables('${file}', ws, activeFile)).toBe(activeFile);
        expect(utils.resolveDapVariables('${fileBasename}', ws, activeFile)).toBe('main.src');
        expect(utils.resolveDapVariables('${fileBasenameNoExtension}', ws, activeFile)).toBe('main');
        expect(utils.resolveDapVariables('${fileDirname}', ws, activeFile)).toBe('/home/user/project/src');
        expect(utils.resolveDapVariables('${fileExtname}', ws, activeFile)).toBe('.src');
    });

    test('replaces ~ with home directory', () => {
        expect(utils.resolveDapVariables('~/tools', ws, activeFile)).toBe(path.join(os.homedir(), 'tools'));
    });

    test('multiple variables in one string', () => {
        expect(utils.resolveDapVariables('${fileDirname}/${fileBasenameNoExtension}.cpp', ws, activeFile))
            .toBe('/home/user/project/src/main.cpp');
    });
});

describe('extension-utils: updateStatusBar', () => {
    let mockStatusBar;

    beforeEach(() => {
        mockStatusBar = {
            text: '',
            tooltip: '',
            show: jest.fn()
        };
    });

    test('shows debugging state', () => {
        utils.updateStatusBar(true, mockStatusBar);
        expect(mockStatusBar.text).toContain('Running');
        expect(mockStatusBar.show).toHaveBeenCalled();
    });

    test('shows idle state', () => {
        utils.updateStatusBar(false, mockStatusBar);
        expect(mockStatusBar.text).toContain('Idle');
        expect(mockStatusBar.show).toHaveBeenCalled();
    });

    test('does not crash with null status bar', () => {
        expect(() => utils.updateStatusBar(true, null)).not.toThrow();
        expect(() => utils.updateStatusBar(false, undefined)).not.toThrow();
    });
});

describe('extension-utils: updateLspStatusBar', () => {
    let mockStatusBar;

    beforeEach(() => {
        mockStatusBar = {
            text: '',
            tooltip: '',
            show: jest.fn()
        };
    });

    test('shows starting state', () => {
        utils.updateLspStatusBar(utils.LspStatus.STARTING, mockStatusBar);
        expect(mockStatusBar.text).toContain('Starting');
        expect(mockStatusBar.show).toHaveBeenCalled();
    });

    test('shows running state', () => {
        utils.updateLspStatusBar(utils.LspStatus.RUNNING, mockStatusBar);
        expect(mockStatusBar.text).toContain('Running');
        expect(mockStatusBar.show).toHaveBeenCalled();
    });

    test('shows error state', () => {
        utils.updateLspStatusBar(utils.LspStatus.ERROR, mockStatusBar);
        expect(mockStatusBar.text).toContain('Error');
        expect(mockStatusBar.show).toHaveBeenCalled();
    });

    test('shows stopped state for null/unknown status', () => {
        utils.updateLspStatusBar(null, mockStatusBar);
        expect(mockStatusBar.text).toContain('Stopped');
        expect(mockStatusBar.show).toHaveBeenCalled();
    });

    test('does not crash with null status bar', () => {
        expect(() => utils.updateLspStatusBar(utils.LspStatus.RUNNING, null)).not.toThrow();
        expect(() => utils.updateLspStatusBar(utils.LspStatus.STARTING, undefined)).not.toThrow();
    });

    test('LspStatus exports correct constants', () => {
        expect(utils.LspStatus.STARTING).toBe('starting');
        expect(utils.LspStatus.RUNNING).toBe('running');
        expect(utils.LspStatus.ERROR).toBe('error');
    });
});
