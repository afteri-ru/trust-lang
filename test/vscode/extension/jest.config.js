module.exports = {
    testEnvironment: 'node',
    testMatch: ['**/src/*.test.js'],
    moduleNameMapper: {
        '^vscode$': '<rootDir>/src/__mocks__/vscode.js',
        '^vscode-languageclient/node$': '<rootDir>/src/__mocks__/languageclient.js'
    },
    moduleDirectories: [
        'node_modules',
        '<rootDir>/../../../include/vscode/extensions/trust-lang'
    ],
    collectCoverage: false,
    verbose: false
};
