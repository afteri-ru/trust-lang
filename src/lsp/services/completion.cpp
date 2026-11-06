#include "lsp/services/completion.h"

#include "location/namespace_alias.hpp"
#include "lsp/services/document_manager.hpp"
#include "lsp/catalog/lsp_options.hpp"
#include "lsp/lsp_protocol.h"
#include "lsp/catalog/lsp_utils.hpp"
#include "types/names.hpp"
#include "utils/utils.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

using json = nlohmann::json;
using trust::utils::uriToFilePath;

namespace trust {
namespace lsp {
namespace completion {

// Байты >= 0x80 - UTF-8 (кириллица и др. письменности). isalpha/isalnum в локали
// "C" понимают только ASCII, поэтому для Unicode-имён добавляем ручную проверку.
bool isWordChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '%' || c == ':' ||
           c == '@' || c == '\\';
}
bool isSigil(char c) {
    return c == '@' || c == '$' || c == ':';
}

// Позиции в LSP - UTF-16 code units; сканирование строки ведём по байтам.
// Длина UTF-8 последовательности по первому байту (продолжающие байты не проверяются:
// позиции LSP приходят для уже валидного текста).
int utf8SeqLen(unsigned char c) {
    if (c < 0x80) {
        return 1;
    }
    if ((c & 0xE0) == 0xC0) {
        return 2;
    }
    if ((c & 0xF0) == 0xE0) {
        return 3;
    }
    return 4;
}

int utf16ToByte(const std::string& s, int utf16pos) {
    int b = 0, u = 0;
    const int n = static_cast<int>(s.size());
    while (b < n && u < utf16pos) {
        b += utf8SeqLen(static_cast<unsigned char>(s[b]));
        ++u;
    }
    return b;
}
int byteToUtf16(const std::string& s, int byteLen) {
    int b = 0, u = 0;
    const int n = static_cast<int>(s.size());
    while (b < byteLen && b < n) {
        b += utf8SeqLen(static_cast<unsigned char>(s[b]));
        ++u;
    }
    return u;
}

std::string_view stripSigil(std::string_view s) {
    if (!s.empty() && isSigil(s.front())) {
        return s.substr(1);
    }
    return s;
}

// Совпадение insertText с набранным префиксом. Учитывает, что сигнатура может
// быть не набрана: «calc» подходит к «%calc», «val» к «$value», «Int» к «:Int32».
bool matchesPrefix(std::string_view insertText, std::string_view prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (insertText.starts_with(prefix)) {
        return true;
    }
    std::string_view p2 = stripSigil(prefix);
    if (p2.empty()) {
        return false;
    }
    return stripSigil(insertText).starts_with(p2);
}

// Item автодополнения с явным textEdit (диапазон = набранный префикс в UTF-16),
// чтобы при вставке сигнатура (@, %, :, $) не дублировалась.
json makeCompletionItem(const std::string& insertText, int kind, const std::string& detail, int line, int utf16Start, int utf16End,
                        const std::string& documentation = "") {
    json item = {{"label", insertText}, {"insertText", insertText}, {"kind", kind}, {"detail", detail}};
    if (!documentation.empty()) {
        // documentation как MarkupContent{kind:plaintext} - VS Code показывает его как обычный текст
        // (markdown-разметка в доке DSL-макросов, напр. `##`, не превращается в заголовки).
        item["documentation"] = {{"kind", "plaintext"}, {"value", documentation}};
    }
    item["textEdit"] = {{"range", {{"start", {{"line", line}, {"character", utf16Start}}}, {"end", {{"line", line}, {"character", utf16End}}}}},
                        {"newText", insertText}};
    return item;
}

std::string lineAt(const std::string& doc, int line) {
    size_t start = 0;
    for (int i = 0; i < line && start < doc.size(); ++i) {
        size_t nl = doc.find('\n', start);
        if (nl == std::string::npos) {
            start = doc.size();
            break;
        }
        start = nl + 1;
    }
    size_t nl = doc.find('\n', start);
    std::string s = doc.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
    if (!s.empty() && s.back() == '\r') {
        s.pop_back();
    }
    return s;
}

std::string wordPrefix(const std::string& line, int character) {
    int i = character - 1;
    while (i >= 0 && isWordChar(line[static_cast<size_t>(i)])) {
        --i;
    }
    return line.substr(static_cast<size_t>(i + 1), static_cast<size_t>(character - i - 1));
}

// Объект/тип/литерал перед '.' (строковый литерал или идентификатор с сигнатурами).
std::string memberObjectName(const std::string& line, int dotIndex) {
    int i = dotIndex - 1;
    if (i >= 0 && (line[static_cast<size_t>(i)] == '\'' || line[static_cast<size_t>(i)] == '"')) {
        char q = line[static_cast<size_t>(i)];
        int j = i;
        while (j >= 0 && line[static_cast<size_t>(j)] != q) {
            --j;
        }
        if (j >= 0) {
            return line.substr(static_cast<size_t>(j), static_cast<size_t>(i - j + 1));
        }
        return {};
    }
    while (i >= 0) {
        char c = line[static_cast<size_t>(i)];
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == ':' || c == '%')) {
            break;
        }
        --i;
    }
    return line.substr(static_cast<size_t>(i + 1), static_cast<size_t>(dotIndex - i - 1));
}

// Категория имени по сигнатуре.
int nameKind(const std::string& name) {
    char c = name.empty() ? '\0' : name[0];
    if (c == '@') {
        return kKindKeyword;
    }
    if (c == ':') {
        return kKindClass;
    }
    return kKindVariable; // %, $, _, plain
}

// collectTypeItems: элементы пер-файлового реестра пользовательских типов (заполняет анализатор).
void collectTypeItems(const trust::TypeSystem* reg, const trust::BuiltinCatalog* catalog, trust::ModuleIndex mainModule, const std::string& prefix, int line,
                      int utf16Start, int utf16End, json& items) {
    std::set<std::string> seen;
    auto add = [&](const std::string& name, bool userDefined) {
        std::string insert = ":" + name;
        if (!matchesPrefix(insert, prefix) || !seen.insert(insert).second) {
            return;
        }
        items.push_back(makeCompletionItem(insert, kKindClass, userDefined ? "type" : "builtin type", line, utf16Start, utf16End));
    };
    if (catalog) {
        for (const auto& [name, info] : catalog->types()) {
            (void)info;
            add(name, false);
        }
    }
    if (reg) {
        reg->read().forEachType([&](const trust::StorageKey& key, std::string_view display, bool userDefined) {
            (void)display;
            if (!userDefined) {
                return;
            }
            // module-qualified ключ = private/import-hidden. Предлагается только private
            // СОБСТВЕННОГО модуля: в файле, который объявляет тип, он видим. Import-hidden
            // всегда принадлежит чужому модулю (свой экспорт регистрируется плоским ключом).
            if (key.isModuleQualified() && key.module != mainModule) {
                return;
            }
            const auto id = reg->read().findType(key);
            if (!id.has_value()) {
                return;
            }
            add(reg->read().printer().typeNameOf(*id), true);
        });
    }
}

// collectMacroItems: имена встроенных макросов (каталог) + макроопределения анализатора.
// Доки — из хранилища прогона (Context файла): predef/DSL-сидинг, `##`-доки, прагма-переопределения.
void collectMacroItems(const trust::BuiltinCatalog* catalog, const trust::Context* ctx, const trust::SymbolIndex* symbols, trust::ModuleIndex mainModule,
                       const std::string& prefix, int line, int utf16Start, int utf16End, json& items) {
    std::map<std::string, std::string> nameDoc;
    if (catalog) {
        for (const auto& n : catalog->predefMacros()) {
            nameDoc.emplace(n, std::string());
        }
        for (const auto& n : catalog->dslMacros()) {
            nameDoc.emplace(n, std::string());
        }
    }
    if (symbols) {
        for (const auto& si : *symbols) {
            if (!si.isMacro || si.name.empty()) {
                continue;
            }
            // Видимость per-module: макрос импортированного модуля не предлагается (семантика
            // его тоже не видит); собственный макрос предлагается независимо от наличия дока.
            if (!trust::isMacroVisible(si, mainModule)) {
                continue;
            }
            // Пустая документация анализатора не затирает док из хранилища прогона.
            auto [it, inserted] = nameDoc.emplace(std::string(si.name), std::string(si.documentation));
            if (!inserted && !si.documentation.empty()) {
                it->second = std::string(si.documentation);
            }
        }
    }
    if (ctx != nullptr) {
        for (const auto& [name, doc] : ctx->macroDocs()) {
            if (!doc.empty()) {
                nameDoc[std::string(name)] = std::string(doc);
            }
        }
    }
    std::set<std::string> seen;
    for (const auto& [n, doc] : nameDoc) {
        // Имя может уже содержать сигнатуру (@, $, %, :), иначе - макрос без неё.
        std::string insert = (isSigil(n.front()) ? std::string() : "@") + n;
        if (!matchesPrefix(insert, prefix) || !seen.insert(insert).second) {
            continue;
        }
        items.push_back(makeCompletionItem(insert, kKindKeyword, "macro", line, utf16Start, utf16End, doc));
    }
}

// Определяет тип ЛИТЕРАЛА по самому объекту ('...', число, true/false) - без
// сканирования документа. Возвращает "" если objExpr - не литерал.
std::string literalTypeOf(const std::string& objExpr) {
    if (objExpr.empty()) {
        return "";
    }
    const char c0 = objExpr[0];
    if (c0 == '\'' || c0 == '"') {
        return "StrChar";
    }
    if (isdigit(static_cast<unsigned char>(c0)) || (c0 == '-' && objExpr.size() > 1 && isdigit(static_cast<unsigned char>(objExpr[1])))) {
        if (objExpr.find('\\') != std::string::npos) {
            return "Rational";
        }
        if (objExpr.find('.') != std::string::npos) {
            return "Float64";
        }
        return "Integer";
    }
    if (objExpr == "true" || objExpr == "false" || objExpr == "yes" || objExpr == "no") {
        return "Bool";
    }
    return "";
}

// Аналог clangd: кандидаты фильтруются по namespace-пути объявления, а не по подстроке.
// Контекст завершения: проекция видимости/поисковых областей, посчитанная семантикой.
struct CompletionScope {
    const trust::Context* ctx = nullptr;
    trust::VisibilitySites visibility{};
    const std::vector<trust::UsingScopeInfo>* usingScopes = nullptr;
    const std::vector<trust::NamespaceAliasInfo>* namespaceAliases = nullptr;
    const std::vector<std::string>* moduleNames = nullptr;
};

// Курсор на строке `line` внутри диапазона скоупа (невалидный диапазон = уровень файла).
bool lineInScope(const trust::Context& ctx, const trust::MapperRange& range, int line) {
    if (range.isInvalid()) {
        return true;
    }
    const int startLine = static_cast<int>(ctx.source().line_column(range.begin).line) - 1;
    const int endLine = static_cast<int>(ctx.source().line_column(range.end).line) - 1;
    return line >= startLine && line <= endLine;
}

// Самый длинный namespace-алиас, чей префикс совпадает с `path` (вложенные алиасы).
const trust::NamespaceAliasInfo* longestAliasPrefix(const std::vector<trust::NamespaceAliasInfo>& aliases, std::string_view path,
                                                    std::optional<trust::ModuleIndex> importer, const trust::Context& ctx, int line) {
    const trust::NamespaceAliasInfo* best = nullptr;
    for (const trust::NamespaceAliasInfo& alias : aliases) {
        if (importer.has_value() && alias.importer != *importer) {
            continue;
        }
        if (!lineInScope(ctx, alias.scopeRange, line) || !trust::matchesAliasPrefix(alias.alias, path)) {
            continue;
        }
        if (best == nullptr || alias.alias.size() > best->alias.size()) {
            best = &alias;
        }
    }
    return best;
}

// Набранный qualified-префикс, разобранный в область поиска. `insertBase` — набранный текст
// до короткого имени (вставка = insertBase + shortName, поэтому квалификация не дублируется).
struct QualifiedSearch {
    std::optional<trust::ModuleIndex> module;
    std::string namespacePath;
    std::string partial;
    std::string insertBase;
    bool viaAlias = false; ///< область получена через namespace-алиас (он же авторизует доступ)
};

// Разбор поддерживаемых форм: `ns::`, `ns::sub::`, `::ns::`,
// `alias::` (namespace-алиас, привязанный в активном скоупе).
std::optional<QualifiedSearch> parseQualifiedSearch(const CompletionScope& scope, const std::string& prefix, int line) {
    const std::size_t sep = prefix.rfind("::");
    if (sep == std::string::npos) {
        return std::nullopt;
    }
    QualifiedSearch search;
    search.insertBase = prefix.substr(0, sep + 2);
    search.partial = prefix.substr(sep + 2);
    std::string rest = prefix.substr(0, sep);
    if (rest.starts_with("::")) {
        rest.erase(0, 2); // `::ns::` — глобальная область: путь без ведущего разделителя
    }
    std::string path = rest;
    if (!search.module.has_value() && scope.namespaceAliases != nullptr) {
        if (const trust::NamespaceAliasInfo* best = longestAliasPrefix(*scope.namespaceAliases, path, std::nullopt, *scope.ctx, line)) {
            search.module = best->targetModule;
            search.viaAlias = true;
            const std::string tail = path.size() == best->alias.size() ? std::string() : path.substr(best->alias.size() + 2);
            path = tail.empty() ? best->path : trust::utils::qualifyTrustName(best->path, tail);
        }
    }
    search.namespacePath = path;
    return search;
}

void addQualifiedItem(json& items, std::set<std::string>& seen, std::string_view partial, std::string_view shortName, int kind, const std::string& insertText,
                      int line, int utf16Start, int utf16End) {
    if (!shortName.starts_with(partial) || !seen.insert(insertText).second) {
        return;
    }
    items.push_back(makeCompletionItem(insertText, kind, "", line, utf16Start, utf16End));
}

void addAliasCandidates(json& items, std::set<std::string>& seen, const std::vector<trust::NamespaceAliasInfo>& aliases,
                        std::optional<trust::ModuleIndex> importer, const std::string& namespacePath, const std::string& partial, const std::string& insertBase,
                        int line, const trust::Context& ctx, int utf16Start, int utf16End) {
    for (const trust::NamespaceAliasInfo& alias : aliases) {
        if (importer.has_value() && alias.importer != *importer) {
            continue;
        }
        if (!lineInScope(ctx, alias.scopeRange, line)) {
            continue;
        }
        const std::size_t sep = alias.alias.rfind("::");
        const std::string_view parent = sep == std::string::npos ? std::string_view{} : std::string_view(alias.alias).substr(0, sep);
        const std::string_view shortName = sep == std::string::npos ? std::string_view(alias.alias) : std::string_view(alias.alias).substr(sep + 2);
        if (parent != namespacePath) {
            continue;
        }
        addQualifiedItem(items, seen, partial, shortName, kKindNamespace, insertBase + std::string(shortName), line, utf16Start, utf16End);
    }
}

void collectQualifiedItems(const trust::TypeSystem* reg, const trust::SymbolIndex* symbols, const CompletionScope& scope, const std::string& prefix, int line,
                           int utf16Start, int utf16End, json& items) {
    const std::optional<QualifiedSearch> search = parseQualifiedSearch(scope, prefix, line);
    if (!search.has_value()) {
        return;
    }
    std::set<std::string> seen;
    if (symbols) {
        for (const auto& si : *symbols) {
            if (si.isMacro || trust::utils::isQualifiedName(si.name) || si.namespacePath != search->namespacePath) {
                continue;
            }
            if (search->module.has_value()) {
                if (!si.module.has_value() || *si.module != *search->module) {
                    continue;
                }
                // Namespace-алиас авторизует доступ к области без прямого сайта импорта
                // (семантика: findVisibleFromModule(..., viaAlias=true)).
                if (!search->viaAlias && !trust::visibleSite(si, scope.visibility, *search->module).has_value()) {
                    continue;
                }
            } else if (!trust::isSymbolVisible(si, scope.visibility)) {
                continue;
            }
            addQualifiedItem(items, seen, search->partial, si.name, kKindVariable, search->insertBase + std::string(si.name), line, utf16Start, utf16End);
        }
    }
    if (reg) {
        reg->read().forEachType([&](const trust::StorageKey& key, std::string_view displayName, bool userDefined) {
            (void)displayName;
            if (!userDefined || key.namespacePath != search->namespacePath) {
                return;
            }
            if (search->module.has_value()) {
                // Имена чужого модуля: только этот модуль (маски сайта к типам не применяем —
                // семантика проверит видимость на вставленном qualified-имени).
                if (!key.isModuleQualified() || key.module != *search->module) {
                    return;
                }
            } else if (key.isModuleQualified() && key.module != scope.visibility.mainModule) {
                return; // private/import-hidden видны только в СВОЁМ модуле
            }
            const auto id = reg->read().findType(key);
            if (!id.has_value()) {
                return;
            }
            const std::string display = reg->read().printer().typeNameOf(*id);
            addQualifiedItem(items, seen, search->partial, display, kKindClass, search->insertBase + display, line, utf16Start, utf16End);
        });
    }
    if (scope.namespaceAliases != nullptr) {
        addAliasCandidates(items, seen, *scope.namespaceAliases, std::nullopt, search->namespacePath, search->partial, search->insertBase, line, *scope.ctx,
                           utf16Start, utf16End);
    }
}

// Единый источник имён пользовательского кода: объявленные переменные/функции/типы
// с типами и фильтром видимости по позиции курсора. Сигнатура (% $ : @) сохраняется
// в label - вставка через textEdit не дублирует набранную сигнатуру.
std::string qualifiedAt(const CompletionScope& scope, trust::ModuleIndex module, const std::string& name) {
    if (module == scope.visibility.mainModule) {
        return name;
    }
    EXPECT(module.value < scope.moduleNames->size() && "module logical name must be projected for every loaded module");
    const std::string& spelling = (*scope.moduleNames)[module.value];
    EXPECT(!spelling.empty() && "module logical name must not be empty");
    return trust::utils::qualifyTrustName(spelling, name);
}

void collectSymbolItems(const trust::SymbolIndex* symbols, const trust::Context* ctx, trust::ReaderFile currentFile, const CompletionScope& scope, int line,
                        const std::string& prefix, int utf16Start, int utf16End, json& items) {
    if (!symbols || !ctx) {
        return;
    }
    struct Candidate {
        const trust::SymbolInfo* info;
        std::optional<trust::ModuleIndex> site; ///< сайт доступа (nullopt — короткое имя через using)
    };
    std::map<std::string, std::vector<Candidate>> groups;
    for (const auto& si : *symbols) {
        if (si.isMacro) {
            continue;
        }
        // Видимость per-site: private чужого модуля и скрытый маской экспорт не предлагаются.
        if (!trust::isSymbolVisible(si, scope.visibility)) {
            continue;
        }
        const std::string name(si.name);
        if (name.empty() || trust::utils::isQualifiedName(name)) {
            continue; // квалифицированные имена областей имён показываем как тип, не как переменную
        }
        if (!matchesPrefix(name, prefix)) {
            continue;
        }
        // Позиционные ограничения применимы только к объявлениям ТЕКУЩЕГО файла: диапазоны
        // импортированных модулей принадлежат другим файлам, а line_column учитывает fileIdx
        // (сравнение кросс-файловых строк бессмысленно).
        const bool declaredHere = !si.nameRange.isInvalid() && si.nameRange.begin.fileIdx().as_index() == currentFile.as_index();
        // Однопроходная семантика: имя, объявленное на строке курсора или ПОСЛЕ него,
        // ещё не видимо в точке завершения (не предлагаем незавершённое/будущее объявление).
        if (declaredHere) {
            const int declLine = ctx->source().line_column(si.nameRange.begin).line - 1;
            if (declLine >= line) {
                continue;
            }
        }
        // Локальные по позиции: символ виден, если курсор внутри его скоупа (иначе - глобальный).
        const bool scopedHere = !si.scopeRange.isInvalid() && si.scopeRange.begin.fileIdx().as_index() == currentFile.as_index();
        if (declaredHere && scopedHere) {
            const int startLine = ctx->source().line_column(si.scopeRange.begin).line - 1;
            const int endLine = ctx->source().line_column(si.scopeRange.end).line - 1;
            if (line < startLine || line > endLine) {
                continue;
            }
        }
        groups[name].push_back(Candidate{&si, trust::visibleSite(si, scope.visibility)});
    }
    // Имена, доступные КОРОТКО через активные области поиска `... = X` (using): объявление может
    // лежать в другой области имён (в т.ч. другого модуля), поэтому позиционный фильтр по
    // nameRange/scopeRange к ним не применяется — область поиска уже проверена по курсору.
    for (const trust::UsingScopeInfo& search : *scope.usingScopes) {
        if (!lineInScope(*ctx, search.scopeRange, line)) {
            continue;
        }
        for (const auto& si : *symbols) {
            if (si.isMacro || si.namespacePath != search.path) {
                continue;
            }
            const std::string name(si.name);
            if (name.empty() || trust::utils::isQualifiedName(name) || !matchesPrefix(name, prefix)) {
                continue;
            }
            const bool visible = si.module.has_value() && *si.module == scope.visibility.mainModule;
            if (visible) {
                groups[name].push_back(Candidate{&si, std::nullopt});
            }
        }
    }
    // Одноимённые объявления РАЗНЫХ модулей не смешиваются: каждое вставляется квалифицированно
    // (`ns::f`) и получает различимую метку. Единственный кандидат — обычное короткое имя.
    for (const auto& [name, candidates] : groups) {
        std::set<std::string> distinctSites;
        bool hasOwn = false;
        for (const Candidate& candidate : candidates) {
            if (candidate.site.has_value() && *candidate.site == scope.visibility.mainModule) {
                hasOwn = true;
            }
            if (candidate.site.has_value() && *candidate.site != scope.visibility.mainModule) {
                EXPECT(candidate.site->value < scope.moduleNames->size() && "module logical name must be projected for every loaded module");
                distinctSites.insert((*scope.moduleNames)[candidate.site->value]);
            }
        }
        if (hasOwn) {
            distinctSites.insert(std::string()); // «свой модуль»: короткое имя
        }
        const bool needQualification = distinctSites.size() > 1;
        std::set<std::string> emitted;
        for (const Candidate& candidate : candidates) {
            const std::string insert = (needQualification && candidate.site.has_value()) ? qualifiedAt(scope, *candidate.site, name) : name;
            if (!emitted.insert(insert).second) {
                continue;
            }
            const std::string detail = candidate.info->typeName.empty() ? "" : candidate.info->typeName;
            items.push_back(makeCompletionItem(insert, nameKind(insert), detail, line, utf16Start, utf16End));
        }
    }
}

// Тип объекта резолвится из ЕДИНЫХ источников (без сканирования документа):
//  1) objExpr - литерал → тип по значению (literalTypeOf);
//  2) objExpr - имя типа → TypeId из пер-файлового реестра;
//  3) objExpr - переменная → SymbolInfo::type (TypeId) из таблицы анализатора.
// Методы/поля берутся по TypeId из реестра (descriptor::methods + TupleTypeData::elements);
// при отсутствии реестра (нет кеша) - из глобального каталога встроенных типов.
void collectMemberItems(const trust::TypeSystem* reg, const trust::BuiltinCatalog* catalog, const trust::SymbolIndex* symbols, const std::string& objExpr,
                        const std::string& prefix, int line, int utf16Start, int utf16End, json& items) {
    if (objExpr.empty()) {
        return;
    }
    std::set<std::string> seen;
    trust::TypeId typeId = trust::INVALID_TYPE_ID;
    std::string typeName;
    std::vector<std::string> dictFields; // имена полей словаря (из SymbolInfo::dictFields)

    // 1) Литерал: '...', число, true/false → тип по значению.
    typeName = literalTypeOf(objExpr);
    if (!typeName.empty() && reg) {
        if (auto tid = reg->read().findType(typeName)) {
            typeId = *tid;
        }
    }

    // 2) Имя типа (с сигнатурой ':'/'%'/без) → члены типа напрямую.
    if (typeId == trust::INVALID_TYPE_ID) {
        std::string key = objExpr;
        if (!key.empty() && isSigil(key.front())) {
            key.erase(key.begin());
        }
        if (reg) {
            if (auto tid = reg->read().findType(key)) {
                typeId = *tid;
                typeName = key;
            }
        } else if (catalog && catalog->types().count(key)) {
            typeName = key;
        }
    }

    // 3) Переменная в таблице анализатора → её TypeId и имя типа.
    if (typeId == trust::INVALID_TYPE_ID && symbols) {
        std::string key = objExpr;
        if (!key.empty() && isSigil(key.front())) {
            key.erase(key.begin());
        }
        for (const auto& si : *symbols) {
            if (si.isMacro) {
                continue;
            }
            std::string sname(si.name);
            if (!sname.empty() && isSigil(sname.front())) {
                sname.erase(sname.begin());
            }
            if (sname != key) {
                continue;
            }
            typeId = si.type;
            typeName = si.typeName;
            dictFields = si.dictFields;
            break;
        }
    }

    auto emitMember = [&](const std::string& raw, bool isFunc) {
        if (!seen.insert(raw).second) {
            return;
        }
        const std::string label = raw;
        const std::string insert = label + (isFunc ? "()" : "");
        if (!matchesPrefix(insert, prefix)) {
            return;
        }
        items.push_back(makeCompletionItem(insert, isFunc ? kKindMethod : kKindField, typeName, line, utf16Start, utf16End));
    };

    // Поля словаря из таблицы анализатора (x := (a=1, b=2,) → x.a / x.b).
    for (const auto& f : dictFields) {
        emitMember(f, false);
    }

    if (typeId != trust::INVALID_TYPE_ID && reg) {
        // Эмит методов дескриптора (методы + алиасы) с bare-именами (для автодополнения показываем
        // доверенные имена: "size", "length", "count" - без '%'/'^').
        auto emitDescMethods = [&](const trust::TypeStore& store, trust::TypeId id) {
            const auto* methods = store.methodsOf(id);
            if (methods == nullptr) {
                return;
            }
            for (const auto& [mname, funcType] : *methods) {
                (void)funcType;
                emitMember(mname.name, true);
            }
            const auto* aliases = store.methodAliasesOf(id);
            if (aliases != nullptr) {
                for (const auto& [alias, target] : *aliases) {
                    (void)target;
                    emitMember(alias, true);
                }
            }
        };
        emitDescMethods(reg->read().store(), typeId);
        // Параметризованный Range<Elem> собственных методов не несёт: они объявлены на абстрактном
        // `:Range` (методы с типовым параметром T; см. findMethodInfo/registry). Для автодополнения
        // `$a.` (переменная диапазона) подмешиваем методы `:Range` (dedup через seen в emitMember).
        if (reg->read().store().isRangeType(typeId)) {
            emitDescMethods(reg->read().store(), reg->read().getType(trust::type_category::Range));
        }
        // Члены классов/типов (TupleTypeData): имя → поле/метод (функциональный тип - метод).
        if (const auto* td = reg->read().store().getTypeDataAs<trust::TupleTypeData>(typeId)) {
            for (const auto& el : td->elements) {
                if (!el.name.empty()) {
                    emitMember(el.name, reg->read().store().getTypeDataAs<trust::FunctionTypeData>(el.type.getTypeId()) != nullptr);
                }
            }
        }
    } else if (catalog && !typeName.empty()) {
        // Реестр недоступен (нет кеша) - члены встроенного типа из каталога.
        auto tit = catalog->types().find(typeName);
        if (tit != catalog->types().end()) {
            for (const auto& [mname, isFunc] : tit->second.methods) {
                emitMember(mname, isFunc);
            }
        }
    }
}

// Сортировка по label для стабильного детерминированного вывода.
json sortCompletionItems(json items) {
    std::vector<std::pair<std::string, json>> vec;
    vec.reserve(items.size());
    for (auto& it : items) {
        vec.emplace_back(it["label"].get<std::string>(), std::move(it));
    }
    std::sort(vec.begin(), vec.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    json sorted = json::array();
    for (auto& [label, it] : vec) {
        (void)label;
        sorted.push_back(std::move(it));
    }
    return sorted;
}

// textDocument/completion: glue-обработчик поверх collect*Items (см. completion.h).
// Вынесен из монолита TrustLsp (src/lsp/catalog/trust_lsp.cpp). Stateless.
void handleCompletion(trust::transport::Transport& transport, DocumentManager& documents, LspOptions& opts, const json& req) {
    json id = req.value("id", json());
    json params = req.value("params", json());
    std::string uri = params.value("textDocument", json()).value("uri", "");
    std::string filePath = uriToFilePath(uri);
    json pos = params.value("position", json());
    int line = pos.value("line", 0);
    int character = pos.value("character", 0);

    lspLog(opts, "completion: " + filePath + " " + std::to_string(line) + ":" + std::to_string(character));

    json items = json::array();
    try {
        std::string trustFilePath = filePath;
        auto cppIt = documents.cppToTrustCache().find(filePath);
        if (cppIt != documents.cppToTrustCache().end()) {
            trustFilePath = cppIt->second;
        }

        // Текст документа из буфера (актуальные правки). Не зависим от успешной
        // транспиляции - завершение работает и на недописанном коде.
        std::string docText;
        auto docIt = documents.openDocuments().find(trustFilePath);
        if (docIt != documents.openDocuments().end()) {
            docText = docIt->second;
        } else if (auto content = trust::utils::FileIO::read<std::string>(filePath)) {
            docText = *content;
        }

        // Единый каталог встроенных имён (типы/методы/функции/predef+DSL-макросы).
        const trust::BuiltinCatalog& catalog = trust::BuiltinCatalog::instance();
        // Пер-файловый реестр + таблица символов из кеша (последняя транспиляция).
        // Встроенные имена - из каталога; пользовательские - из SymbolIndex/реестра.
        const trust::TypeSystem* reg = nullptr;
        const trust::SymbolIndex* symbols = nullptr;
        const trust::Context* ctx = nullptr;
        trust::ReaderFile currentFile; ///< индекс текущего trust-файла (для позиционных фильтров)
        trust::ModuleIndex mainModule{};
        std::vector<trust::ImportSite> noSites;
        const std::vector<trust::ImportSite>* importSites = &noSites;
        std::vector<trust::ImportSite> activeSites;
        std::vector<trust::ReExportedNameInfo> noReExports;
        const std::vector<trust::ReExportedNameInfo>* reExports = &noReExports;
        std::vector<trust::UsingScopeInfo> noUsingScopes;
        const std::vector<trust::UsingScopeInfo>* usingScopes = &noUsingScopes;
        std::vector<trust::NamespaceAliasInfo> noAliases;
        const std::vector<trust::NamespaceAliasInfo>* namespaceAliases = &noAliases;
        std::vector<std::string> noModuleNames;
        const std::vector<std::string>* moduleNames = &noModuleNames;
        auto cit = documents.sourceCache().find(trustFilePath);
        if (cit != documents.sourceCache().end() && cit->second.model.has_value()) {
            const trust::SemanticModel& model = *cit->second.model;
            reg = &model.typeSystem();
            symbols = &model.symbols();
            ctx = cit->second.sourceMap.get();
            currentFile = cit->second.trustReaderIdx;
            mainModule = model.mainModuleIndex();
            activeSites.reserve(model.importSites().size());
            for (const trust::ImportSite& site : model.importSites()) {
                if (lineInScope(*ctx, site.scopeRange, line)) {
                    activeSites.push_back(site);
                }
            }
            importSites = &activeSites;
            reExports = &model.reExports();
            usingScopes = &model.usingScopes();
            namespaceAliases = &model.namespaceAliases();
            moduleNames = &model.modules().moduleNames;
        }
        const CompletionScope search{ctx, mainModule, importSites, reExports, usingScopes, namespaceAliases, moduleNames};

        std::string lineStr = lineAt(docText, line);
        // Позиция курсора в байтах UTF-8 (LSP даёт UTF-16 code units).
        const int byteChar = utf16ToByte(lineStr, character);

        // Режим: member-доступ (obj.<имя>) или обычное имя.
        std::string objName;
        std::string prefix;
        bool member = false;
        {
            int dot = -1;
            for (int i = 0; i < byteChar; ++i) {
                if (lineStr[static_cast<size_t>(i)] == '.') {
                    dot = i;
                }
            }
            if (dot >= 0) {
                bool onlyName = true;
                for (int i = dot + 1; i < byteChar; ++i) {
                    char c = lineStr[static_cast<size_t>(i)];
                    if (!(isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$')) {
                        onlyName = false;
                        break;
                    }
                }
                if (onlyName) {
                    member = true;
                    objName = memberObjectName(lineStr, dot);
                    prefix = lineStr.substr(static_cast<size_t>(dot + 1), static_cast<size_t>(byteChar - dot - 1));
                }
            }
            if (!member) {
                prefix = wordPrefix(lineStr, byteChar);
            }
        }
        // Начало набранного префикса в UTF-16 для textEdit-диапазона.
        const int utf16Start = byteToUtf16(lineStr, byteChar - static_cast<int>(prefix.size()));

        if (member) {
            collectMemberItems(reg, &catalog, symbols, objName, prefix, line, utf16Start, character, items);
        } else if (trust::utils::isQualifiedName(prefix)) {
            // Квалифицированное имя: только имена указанной области (типы + символы).
            collectQualifiedItems(reg, symbols, search, prefix, line, utf16Start, character, items);
        } else {
            // Имена пользовательского кода - из таблицы анализатора (SymbolIndex).
            collectSymbolItems(symbols, ctx, currentFile, search, line, prefix, utf16Start, character, items);
            // Типы: встроенные (каталог) + пользовательские (реестр).
            collectTypeItems(reg, &catalog, mainModule, prefix, line, utf16Start, character, items);
            // Макросы: predef/DSL (каталог) + записанные анализатором (SymbolIndex).
            collectMacroItems(&catalog, ctx, symbols, mainModule, prefix, line, utf16Start, character, items);
        }
        items = sortCompletionItems(items);
    } catch (const std::exception& e) {
        lspReportHandlerError("completion error: " + std::string(e.what()));
    } catch (...) {
        lspReportHandlerError("completion error: unknown");
    }
    sendLspResponse(transport, id, json{{"isIncomplete", false}, {"items", items}});
}

} // namespace completion
} // namespace lsp
} // namespace trust
