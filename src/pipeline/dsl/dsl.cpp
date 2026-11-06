// Загрузка DSL-макросов и эффективных keywords.
#include "pipeline/dsl/dsl.hpp"

#include "formatter/format.hpp"
#include "package/package.hpp"
#include "session/context.hpp"
#include "syntax/macro.h"
#include "syntax/module_header.hpp"
#include "syntax/parser.h"
#include "syntax/term/term.h"
#include "utils/error.hpp"
#include "utils/utils.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace trust {

void loadDslMacros(Context& ctx, const PipelineOpts& opts) {
    if (opts.no_dsl || ctx.macro()) {
        return;
    }

    // Снимок CLI-значения keywords ДО загрузки dsl: value-флаг может быть установлен через
    // `--keywords=` (и, как побочный эффект value-флага, через `-Wkeywords=`). Это значение
    // побеждает дефолт из dsl.src (и .trust-format). Выполняется только один раз (первый реальный
    // загруз dsl); повторные вызовы упираются в guard выше и настройки CLI не переопределяют.
    std::string cliKw;
    if (auto v = ctx.opts().flagValueByName("keywords"); v && !v->empty()) {
        cliKw = std::string(*v);
    }

    std::string source;
    if (!opts.dsl_file.empty()) {
        auto content = trust::utils::FileIO::read<std::string>(opts.dsl_file);
        if (!content) {
            FAULT("Failed to open DSL file '{}'", opts.dsl_file);
        }
        source = std::move(*content);
    } else {
        // dsl.src — файл пакета ресурсов `share/trust/stdlib.trust`; отсутствие или
        // пустое содержимое — ошибка (без тихого fallback).
        const std::optional<std::string> dsl = readResourcePackageEntry("stdlib", "dsl.src");
        if (!dsl || dsl->empty()) {
            FAULT("dsl source '{}' is not available in the standard library package", "dsl.src");
        }
        source = std::move(*dsl);
    }

    auto macro = std::make_shared<Macro>(makeParserServices(ctx));
    ctx.setMacro(macro);
    ParserServices parseServices = makeParserServices(ctx);
    Parser parser(parseServices);
    // Встроенный DSL - «фиктивный» in-memory источник под именем "@stdlib/dsl"
    // (префикс '@' = файла на диске нет, readFilesFromDisk его пропускает).
    // Содержимое "@stdlib/dsl" - это stdlib/dsl.src; LSP сохраняет его на диск как
    // <tempDir>/stdlib/dsl.src, чтобы ссылки на определения макросов были
    // навигируемы (см. lsp/NAVIGATION.md).
    maskModuleHeader(source);
    TermPtr term = parser.ParseText(source, "@stdlib/dsl");
    if (!term || ctx.diag().errorCount() > 0) {
        FAULT("Failed to parse DSL source");
    }

    // Приоритет keywords: CLI (снимок) > .trust-format "Keywords:" > дефолт dsl.src.
    if (!cliKw.empty()) {
        // CLI побеждает всё (в т.ч. дефолт dsl и .trust-format).
        ctx.opts().setFlagValueByName("keywords", cliKw);
    } else {
        // CLI не задан: .trust-format переопределяет дефолт dsl.
        const std::string cfgPath = trust::formatter::findConfig(std::filesystem::path(opts.input_file).parent_path().string());
        if (!cfgPath.empty()) {
            auto cfg = trust::formatter::loadConfig(cfgPath);
            if (cfg.ok && !cfg.opts.keywords.empty()) {
                std::string kwList;
                for (const auto& k : cfg.opts.keywords) {
                    if (!kwList.empty()) {
                        kwList += ",";
                    }
                    kwList += k;
                }
                ctx.opts().setFlagValueByName("keywords", kwList);
            }
        }
    }
}

std::vector<std::string> effectiveKeywords(Context& ctx, const PipelineOpts& opts) {
    loadDslMacros(ctx, opts);
    std::vector<std::string> out;
    if (auto v = ctx.opts().flagValueByName("keywords"); v && !v->empty()) {
        out = trust::formatter::splitKeywordList(*v);
    }
    return out;
}

} // namespace trust
