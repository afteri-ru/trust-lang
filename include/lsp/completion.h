#pragma once

// include/lsp/completion.h
// Логика автодополнения (textDocument/completion) LSP.
// ЕДИНЫЕ источники имён (без текстовых проходов по документу и пер-файловых копий
// реестра):
//  - пользовательский код (переменные/функции/типы/макросы) - таблица анализатора
//    SymbolIndex (SymbolCollectorHook + реестр макросов);
//  - встроенные типы/методы/функции/макросы - глобальный BuiltinCatalog
//    (shared иммутабельное ядро TypeRegistry + predef/DSL-макросы).
// Вставка идёт через textEdit с диапазоном набранного префикса - сигнатура не дублируется.

#include "lsp/builtin_catalog.h"
#include "semantic/symbol_index.hpp"
#include "types/registry.hpp"
#include "diag/context.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace trust::lsp::completion {

// LSP CompletionItemKind
inline constexpr int kKindMethod = 2;
inline constexpr int kKindVariable = 6;
inline constexpr int kKindClass = 7;
inline constexpr int kKindField = 5;
inline constexpr int kKindKeyword = 14;

// Снять ведущую сигнатуру (% $ : @) с имени.
std::string_view stripSigil(std::string_view s);
// Совпадение insertText с набранным префиксом (сигнатура может быть не набрана).
bool matchesPrefix(std::string_view insertText, std::string_view prefix);

// Item автодополнения с явным textEdit (диапазон = набранный префикс в UTF-16).
nlohmann::json makeCompletionItem(const std::string& insertText, int kind, const std::string& detail, int line, int utf16Start, int utf16End);

// Текст строки (0-based line) документа.
std::string lineAt(const std::string& doc, int line);
// Слово/префикс перед курсором (включая сигнатуру).
std::string wordPrefix(const std::string& line, int character);
// Объект/тип/литерал перед '.'.
std::string memberObjectName(const std::string& line, int dotIndex);

// Конвертация LSP-позиций (UTF-16 code units) ↔ байтовые смещения UTF-8.
int utf16ToByte(const std::string& s, int utf16pos);
int byteToUtf16(const std::string& s, int byteLen);

// Имена пользовательского кода - из таблицы анализатора (SymbolIndex).
void collectSymbolItems(const trust::SymbolIndex* symbols, const trust::Context* ctx, int line, const std::string& prefix, int utf16Start, int utf16End,
                        nlohmann::json& items);
// Типы (:Имя) - встроенные из каталога + пользовательские из реестра.
void collectTypeItems(const trust::TypeRegistry* reg, const trust::BuiltinCatalog* catalog, const std::string& prefix, int line, int utf16Start, int utf16End,
                      nlohmann::json& items);
// Макросы (@...) - predef/DSL из каталога + записанные анализатором (isMacro).
void collectMacroItems(const trust::BuiltinCatalog* catalog, const trust::SymbolIndex* symbols, const std::string& prefix, int line, int utf16Start,
                       int utf16End, nlohmann::json& items);
// Методы/поля для `obj.` - по TypeId из реестра + поля словаря из SymbolInfo::dictFields.
void collectMemberItems(const trust::TypeRegistry* reg, const trust::BuiltinCatalog* catalog, const trust::SymbolIndex* symbols, const std::string& objExpr,
                        const std::string& prefix, int line, int utf16Start, int utf16End, nlohmann::json& items);

// Стабильная сортировка по label.
nlohmann::json sortCompletionItems(nlohmann::json items);

} // namespace trust::lsp::completion
