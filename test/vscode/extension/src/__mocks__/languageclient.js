/**
 * Mock for vscode-languageclient/node
 * Used by Jest for unit testing extension.js LanguageClient usage.
 *
 * Supports:
 * - onDidChangeState: triggers callback on state changes
 * - onNotification: triggers callback on notifications
 * - start/stop/dispose
 */

const _stateChangeListeners = [];
const _notificationListeners = [];

const LanguageClient = jest.fn().mockImplementation((id, name, serverOptions, clientOptions) => {
    return {
        id,
        name,
        serverOptions,
        clientOptions,
        start: jest.fn().mockResolvedValue(undefined),
        stop: jest.fn().mockResolvedValue(undefined),
        dispose: jest.fn().mockResolvedValue(undefined),
        onDidChangeState: jest.fn().mockImplementation((listener) => {
            _stateChangeListeners.push(listener);
            return { dispose: jest.fn() };
        }),
        onNotification: jest.fn().mockImplementation((method, listener) => {
            if (typeof method === 'function') {
                // When only one argument (listener) is passed
                _notificationListeners.push(method);
            } else if (typeof listener === 'function') {
                // When method and listener are passed
                _notificationListeners.push({ method, listener });
            }
            return { dispose: jest.fn() };
        }),
        sendNotification: jest.fn(),
        sendRequest: jest.fn().mockResolvedValue({}),
        // Emit helpers for testing
        _emitDidChangeState(state) {
            for (const fn of _stateChangeListeners) {
                fn({ newState: state, oldState: '' });
            }
        },
        _emitNotification(method, params) {
            for (const entry of _notificationListeners) {
                if (typeof entry === 'function') {
                    entry(params);
                } else if (entry.method === method) {
                    entry.listener(params);
                }
            }
        }
    };
});

module.exports = { LanguageClient };