#include "lsp/catalog/builtin_catalog.h"

#include "session/context.hpp"
#include "diag/options.hpp"
#include "package/package.hpp"
#include "types/type_system.hpp"
#include "utils/error.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "syntax/predef_macro.hpp"

#include <optional>

namespace trust {

// DSL-макросы читаются из пакета ресурсов `share/trust/stdlib.trust`
// (package::readResourcePackageEntry): локального встраивания здесь нет, иначе содержимое
// дублировалось бы с pipeline.

BuiltinCatalog::BuiltinCatalog() {
    // 1) Встроенные типы и их методы - из общего иммутабельного ядра. Один лёгкий
    //    инстанс реестра (встроенные разделяются через TypeSystem::builtinCore(),
    //    пользовательских типов нет). Сбор методов выполняется глобально один раз,
    //    а не на каждый файл.
    {
        DiagnosticEngine diag;
        Options opts(diag);
        TypeSystem system(diag, opts);
        const TypeReadModel reg = system.read();
        reg.forEachType([&](const StorageKey& key, std::string_view name, bool userDefined) {
            (void)userDefined; // каталог - только встроенные типы
            (void)name;
            auto tid = reg.findType(key);
            if (!tid) {
                return;
            }
            const std::string canon = reg.printer().typeNameOf(*tid);
            auto& t = m_types[canon];
            t.userDefined = false;
            if (const auto* methods = reg.store().methodsOf(*tid)) {
                for (const auto& [mname, funcType] : *methods) {
                    (void)funcType;
                    t.methods[mname.name] = true; // trust-имя (структурный ключ → поле name) для LSP
                }
                // Алиасы методов (доверенные имена) - тоже в списке имён.
                if (const auto* aliases = reg.store().methodAliasesOf(*tid)) {
                    for (const auto& [alias, target] : *aliases) {
                        (void)target;
                        t.methods[alias] = true;
                    }
                }
                // Члены классов/типов (TupleTypeData): имя → поле/метод. Методом считаем
                // элемент, чей тип - функциональный (FunctionTypeData).
                if (const auto* td = reg.store().getTypeDataAs<TupleTypeData>(*tid)) {
                    for (const auto& el : td->elements) {
                        if (!el.name.empty()) {
                            t.methods[el.name] = reg.store().getTypeDataAs<FunctionTypeData>(el.type.getTypeId()) != nullptr;
                        }
                    }
                }
            }
        });
    }

    // 2) Предопределённые макросы (@__...__ и др.) - из статического реестра резолвера.
    //    predefMacroNames строит реестры из x-macro TRUST_VALUE_MACROS / TRUST_CONTEXT_MACROS (плюс прагмы).
    m_predefMacros = trust::syntax::PredefMacroResolver::predefMacroNames();

    // 3) DSL-макросы стандартной библиотеки: один раз грузим dsl.src из пакета stdlib и собираем имена
    //    (повторяет pipeline::loadDslMacros: Context + Macro + ParseText "@stdlib/dsl").
    {
        Context ctx(".");
        auto macro = std::make_shared<Macro>(makeParserServices(ctx));
        ctx.setMacro(macro);
        ParserServices parseServices = makeParserServices(ctx);
        Parser parser(parseServices);
        // dsl.src — файл пакета `share/trust/stdlib.trust`. Отсутствие пакета или файла —
        // ошибка (без тихого fallback на пустой набор DSL-макросов).
        const std::optional<std::string> dsl = readResourcePackageEntry("stdlib", "dsl.src");
        if (!dsl || dsl->empty()) {
            FAULT("dsl source '{}' is not available in the standard library package", "dsl.src");
        }
        TermPtr term = parser.ParseText(*dsl, "@stdlib/dsl");
        if (!term || ctx.diag().errorCount() > 0) {
            FAULT("failed to parse DSL source '{}'", "dsl.src");
        }
        for (const auto& n : macro->MacroNames()) {
            m_dslMacros.insert(n);
        }
    }
}

const BuiltinCatalog& BuiltinCatalog::instance() {
    static const BuiltinCatalog inst;
    return inst;
}

} // namespace trust
