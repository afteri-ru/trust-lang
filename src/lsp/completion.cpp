#include "lsp/completion.h"

#include "lsp/document_manager.hpp"
#include "lsp/lsp_options.hpp"
#include "lsp/lsp_protocol.h"
#include "lsp/lsp_utils.hpp"
#include "types/type_names.hpp"
#include "utils/file_io.hpp"
#include "utils/strings.hpp"
#include "utils/uri.hpp"

#include <algorithm>
#include <cctype>
#include <set>
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
    return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '%' || c == ':' || c == '@';
}
bool isSigil(char c) {
    return c == '@' || c == '%' || c == '$' || c == ':';
}

// -- Конвертация LSP-позиций (UTF-16 code units) ↔ байтовые смещения UTF-8 --
// Позиции в LSP - UTF-16 code units; сканирование строки ведём по байтам.
int utf16ToByte(const std::string& s, int utf16pos) {
    int b = 0, u = 0;
    const int n = static_cast<int>(s.size());
    while (b < n && u < utf16pos) {
        const auto c = static_cast<unsigned char>(s[b]);
        if (c < 0x80) {
            b += 1;
        } else if ((c & 0xE0) == 0xC0) {
            b += 2;
        } else if ((c & 0xF0) == 0xE0) {
            b += 3;
        } else {
            b += 4;
        }
        ++u;
    }
    return b;
}
int byteToUtf16(const std::string& s, int byteLen) {
    int b = 0, u = 0;
    const int n = static_cast<int>(s.size());
    while (b < byteLen && b < n) {
        const auto c = static_cast<unsigned char>(s[b]);
        if (c < 0x80) {
            b += 1;
        } else if ((c & 0xE0) == 0xC0) {
            b += 2;
        } else if ((c & 0xF0) == 0xE0) {
            b += 3;
        } else {
            b += 4;
        }
        ++u;
    }
    return u;
}

// Снять ведущую сигнатуру (% $ : @) с имени.
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
                        const std::string& documentation) {
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

// Текст строки (0-based line) документа.
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

// Слово/префикс перед курсором (включая сигнатуру).
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

// -- Типы (:Имя) - из глобального каталога встроенных (BuiltinCatalog) и
// пер-файлового реестра пользовательских типов (заполняет анализатор). --
void collectTypeItems(const trust::TypeRegistry* reg, const trust::BuiltinCatalog* catalog, const std::string& prefix, int line, int utf16Start, int utf16End,
                      json& items) {
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
        reg->forEachType([&](std::string_view name, bool userDefined) {
            if (userDefined) {
                add(std::string(name), true);
            }
        });
    }
}

// -- Макросы (@...) - единый источник: глобальный каталог (predef + DSL) +
// макроопределения, записанные анализатором (SymbolIndex, isMacro). --
void collectMacroItems(const trust::BuiltinCatalog* catalog, const trust::SymbolIndex* symbols, const std::string& prefix, int line, int utf16Start,
                       int utf16End, json& items) {
    // Имя макроса (первый терм группы, напр. `func`) → документирующий комментарий.
    // Доки берём из ЕДИНОГО хранилища каталога (macroDocs(), ключ = первый терм без '@')
    // и из таблицы анализатора (isMacro).
    std::map<std::string, std::string> nameDoc;
    if (catalog) {
        const auto& docs = catalog->macroDocs();
        for (const auto& n : catalog->predefMacros()) {
            std::string bare = (!n.empty() && n.front() == '@') ? n.substr(1) : n;
            auto it = docs.find(bare);
            nameDoc.emplace(n, (it != docs.end()) ? it->second : "");
        }
        for (const auto& n : catalog->dslMacros()) {
            auto it = docs.find(n);
            nameDoc.emplace(n, (it != docs.end()) ? it->second : "");
        }
    }
    if (symbols) {
        for (const auto& si : *symbols) {
            // Перезаписываем док только непустой документацией: DSL-макросы в таблице
            // анализатора могут нести пустую документацию (их реальный док - в хранилище),
            // и пустая запись не должна затирать док из macroDocs().
            if (si.isMacro && !si.name.empty() && !si.documentation.empty()) {
                nameDoc[si.name] = si.documentation;
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

// -- Тип объекта для member-доступа --
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

// -- Имена из анализатора (SymbolIndex) --
// Единый источник имён пользовательского кода: объявленные переменные/функции/типы
// с типами и фильтром видимости по позиции курсора. Сигнатура (% $ : @) сохраняется
// в label - вставка через textEdit не дублирует набранную сигнатуру.
void collectSymbolItems(const trust::SymbolIndex* symbols, const trust::Context* ctx, int line, const std::string& prefix, int utf16Start, int utf16End,
                        json& items) {
    if (!symbols || !ctx) {
        return;
    }
    std::set<std::string> seen;
    for (const auto& si : *symbols) {
        if (si.isMacro) {
            continue;
        }
        const std::string& name = si.name;
        if (name.empty() || name.find("::") != std::string::npos) {
            continue; // квалифицированные имена областей имён показываем как тип, не как переменную
        }
        if (!matchesPrefix(name, prefix)) {
            continue;
        }
        // Однопроходная семантика: имя, объявленное на строке курсора или ПОСЛЕ него,
        // ещё не видимо в точке завершения (не предлагаем незавершённое/будущее объявление).
        if (!si.nameRange.isInvalid()) {
            const int declLine = ctx->source().line_column(si.nameRange.begin).line - 1;
            if (declLine >= line) {
                continue;
            }
        }
        // Локальные по позиции: символ виден, если курсор внутри его скоупа (иначе - глобальный).
        if (!si.scopeRange.isInvalid()) {
            const int startLine = ctx->source().line_column(si.scopeRange.begin).line - 1;
            const int endLine = ctx->source().line_column(si.scopeRange.end).line - 1;
            if (line < startLine || line > endLine) {
                continue;
            }
        }
        if (!seen.insert(name).second) {
            continue;
        }
        const std::string detail = si.typeName.empty() ? "" : si.typeName;
        items.push_back(makeCompletionItem(name, nameKind(name), detail, line, utf16Start, utf16End));
    }
}

// -- Методы/поля для `obj.` --
// Тип объекта резолвится из ЕДИНЫХ источников (без сканирования документа):
//  1) objExpr - литерал → тип по значению (literalTypeOf);
//  2) objExpr - имя типа → TypeId из пер-файлового реестра;
//  3) objExpr - переменная → SymbolInfo::type (TypeId) из таблицы анализатора.
// Методы/поля берутся по TypeId из реестра (descriptor::methods + TupleTypeData::elements);
// при отсутствии реестра (нет кеша) - из глобального каталога встроенных типов.
void collectMemberItems(const trust::TypeRegistry* reg, const trust::BuiltinCatalog* catalog, const trust::SymbolIndex* symbols, const std::string& objExpr,
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
        if (auto tid = reg->findType(typeName)) {
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
            if (auto tid = reg->findType(key)) {
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
            std::string sname = si.name;
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
        const bool isNative = !raw.empty() && raw.front() == '%';
        const std::string label = isNative ? raw.substr(1) : raw;
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
        auto emitDescMethods = [&](const trust::TypeDescriptor* d) {
            if (!d) {
                return;
            }
            for (const auto& [mname, funcType] : d->methods) {
                (void)funcType;
                emitMember(utils::bare_name(mname), true);
            }
            for (const auto& [alias, target] : d->methodAliases) {
                (void)target;
                emitMember(alias, true);
            }
        };
        if (const auto* desc = reg->lookup(typeId)) {
            emitDescMethods(desc);
        }
        // Параметризованный Range<Elem> собственных методов не несёт: они объявлены на абстрактном
        // `:Range` (методы с типовым параметром T; см. findMethodInfo/registry). Для автодополнения
        // `$a.` (переменная диапазона) подмешиваем методы `:Range` (dedup через seen в emitMember).
        if (reg->isRangeType(typeId)) {
            emitDescMethods(reg->lookup(reg->getType(trust::type_category::Range)));
        }
        // Члены классов/типов (TupleTypeData): имя → поле/метод (функциональный тип - метод).
        if (const auto* td = reg->getTypeDataAs<trust::TupleTypeData>(typeId)) {
            for (const auto& el : td->elements) {
                if (!el.name.empty()) {
                    emitMember(el.name, reg->getTypeDataAs<trust::FunctionTypeData>(el.type) != nullptr);
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
// Вынесен из монолита TrustLsp (src/lsp/trust_lsp.cpp). Stateless.
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
        const trust::TypeRegistry* reg = nullptr;
        const trust::SymbolIndex* symbols = nullptr;
        const trust::Context* ctx = nullptr;
        auto cit = documents.sourceCache().find(trustFilePath);
        if (cit != documents.sourceCache().end()) {
            reg = cit->second.types.get();
            symbols = &cit->second.symbols;
            ctx = cit->second.sourceMap.get();
        }

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
        } else {
            // Имена пользовательского кода - из таблицы анализатора (SymbolIndex).
            collectSymbolItems(symbols, ctx, line, prefix, utf16Start, character, items);
            // Типы: встроенные (каталог) + пользовательские (реестр).
            collectTypeItems(reg, &catalog, prefix, line, utf16Start, character, items);
            // Макросы: predef/DSL (каталог) + записанные анализатором (SymbolIndex).
            collectMacroItems(&catalog, symbols, prefix, line, utf16Start, character, items);
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
