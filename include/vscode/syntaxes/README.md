# include/vscode/syntaxes

Грамматики TextMate для расширения VS Code: `markdown-trust.json`, `trusted-cpp.tmLanguage.json`,
`trust.tmLanguage.json`. Копируются в staging расширения целью `copy-vscode-sources`
(см. `include/vscode/CMakeLists.txt`) и попадают в `.vsix` как есть.

Связанные каталоги: `include/vscode/extensions/trust-lang` (package.json/тема/код расширения),
`include/vscode/packager` (упаковка VSIX).
