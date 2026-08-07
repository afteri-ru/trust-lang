#include "lsp/builtin_catalog.h"

#include "diag/context.hpp"
#include "diag/options.hpp"
#include "types/registry.hpp"
#include "utils/strings.hpp"
#include "syntax/macro.h"
#include "syntax/parser.h"

namespace trust {

namespace {
// Встроенный trust/dsl.src: компилируется в бинарник через #embed (как в pipeline.cpp).
// Относительный путь от каталога исходника (src/lsp/ → include/trust/).
static constexpr char kEmbeddedDslSrc[] = {
#embed "../../include/trust/dsl.src"
    , 0};
} // namespace

BuiltinCatalog::BuiltinCatalog() {
    // 1) Встроенные типы и их методы — из общего иммутабельного ядра. Один лёгкий
    //    инстанс реестра (встроенные разделяются через TypeRegistry::builtinCore(),
    //    пользовательских типов нет). Тот же сбор методов, что и прежний typeSnapshot,
    //    но выполняется глобально один раз, а не на каждый файл.
    {
        DiagnosticEngine diag;
        Options opts(diag);
        TypeRegistry reg(diag, opts);
        reg.forEachType([&](std::string_view name, bool userDefined) {
            (void)userDefined; // каталог — только встроенные типы
            auto tid = reg.findType(name);
            if (!tid) {
                return;
            }
            const std::string canon = reg.getFullTypeName(*tid);
            if (canon.empty() || canon == "Unknown") {
                return;
            }
            auto& t = m_types[canon];
            t.userDefined = false;
            if (const auto* desc = reg.lookup(*tid)) {
                for (const auto& [mname, funcType] : desc->methods) {
                    (void)funcType;
                    t.methods[utils::bare_name(mname)] = true; // bare-имя (без '%'/'^') для LSP
                }
                // Алиасы методов (доверенные имена) — тоже в списке имён.
                for (const auto& [alias, target] : desc->methodAliases) {
                    (void)target;
                    t.methods[alias] = true;
                }
                // Члены классов/типов (TupleTypeData): имя → поле/метод. Методом считаем
                // элемент, чей тип — функциональный (FunctionTypeData).
                if (const auto* td = reg.getTypeDataAs<TupleTypeData>(*tid)) {
                    for (const auto& el : td->elements) {
                        if (!el.name.empty()) {
                            t.methods[el.name] = reg.getTypeDataAs<FunctionTypeData>(el.type) != nullptr;
                        }
                    }
                }
            }
        });
    }

    // 2) Предопределённые макросы (@__...__ и др.) — из статического реестра парсера
    //    (PredefMacroNames сам вызывает InitPredefMacro).
    m_predefMacros = Parser::PredefMacroNames();

    // 3) Встроенные DSL-макросы: один раз грузим встроенный DSL и собираем имена
    //    (повторяет pipeline::loadDslMacros: Context + Macro + ParseText "@dsl").
    {
        Context ctx(".");
        auto macro = std::make_shared<Macro>(ctx);
        ctx.setMacro(macro);
        Parser parser(ctx);
        TermPtr term = parser.ParseText(std::string_view(kEmbeddedDslSrc, sizeof(kEmbeddedDslSrc) - 1), "@dsl");
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
