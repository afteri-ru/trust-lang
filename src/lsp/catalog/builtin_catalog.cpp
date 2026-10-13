#include "lsp/catalog/builtin_catalog.h"

#include "session/context.hpp"
#include "diag/options.hpp"
#include "package/package.hpp"
#include "types/type_system.hpp"
#include "utils/error.hpp"
#include "utils/strings.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "syntax/predef_macro.hpp"

#include <optional>

namespace trust {

// DSL-макросы читаются из пакета ресурсов `share/trust/stdlib.trust`
// (package::readResourcePackageEntry): локального #embed здесь нет, иначе содержимое
// дублировалось бы с pipeline.cpp.

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
            const std::string canon = reg.printer().getTypeName(*tid);
            auto& t = m_types[canon];
            t.userDefined = false;
            if (const auto* desc = reg.store().descriptorOf(*tid)) {
                for (const auto& [mname, funcType] : desc->methods) {
                    (void)funcType;
                    t.methods[utils::bareName(mname)] = true; // bare-имя (без '%'/'^') для LSP
                }
                // Алиасы методов (доверенные имена) - тоже в списке имён.
                for (const auto& [alias, target] : desc->methodAliases) {
                    (void)target;
                    t.methods[alias] = true;
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
    //    predefMacroNames строит реестры из x-macro TRUST_VALUE_MACROS / TRUST_CONTEXT_MACROS
    //    (плюс прагмы) и сидирует дефолты доков в ЕДИНОЕ хранилище Context::macroDocs().
    m_predefMacros = trust::syntax::PredefMacroResolver::predefMacroNames();

    // 3) DSL-макросы стандартной библиотеки: один раз грузим dsl.src из пакета stdlib и собираем имена
    //    (повторяет pipeline::loadDslMacros: Context + Macro + ParseText "@stdlib/dsl").
    //    Доки DSL-макросов (##-комментарии) и переопределения прагмой @__PRAGMA_DOC__
    //    записываются в ЕДИНОЕ хранилище Context::macroDocs() через recordMacro/прагму.
    {
        Context ctx(".");
        auto macro = std::make_shared<Macro>(ctx);
        ctx.setMacro(macro);
        Parser parser(ctx);
        // dsl.src — файл пакета `share/trust/stdlib.trust`. Отсутствие пакета или файла —
        // ошибка (без тихого fallback на пустой набор DSL-макросов).
        const std::optional<std::string> dsl = readResourcePackageEntry("stdlib", "stdlib/dsl.src");
        if (!dsl || dsl->empty()) {
            FAULT("dsl source '{}' is not available in the standard library package", "stdlib/dsl.src");
        }
        TermPtr term = parser.ParseText(*dsl, "@stdlib/dsl");
        if (term) {
            for (const auto& n : macro->MacroNames()) {
                m_dslMacros.insert(n);
            }
        }
    }
}

const BuiltinCatalog& BuiltinCatalog::instance() {
    static const BuiltinCatalog inst;
    return inst;
}

} // namespace trust
