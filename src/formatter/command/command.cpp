#include "formatter/command.hpp"

#include "formatter/format.hpp"
#include "session/context.hpp"
#include "syntax/parser.h"
#include "syntax/parser_services.hpp"
#include "utils/utils.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace trust {

int runFormatCommand(Context& ctx, const PipelineOpts& opts, const std::vector<std::string>& keywords) {
    if (opts.input_file.empty()) {
        trust::errs() << "error: --format requires an input file\n";
        return 1;
    }
    if (!std::filesystem::exists(opts.input_file)) {
        trust::errs() << "error: input file not found: " << opts.input_file << "\n";
        return 1;
    }
    auto data = trust::utils::FileIO::read<std::vector<char>>(opts.input_file);
    if (!data) {
        trust::errs() << "error: cannot read file: " << opts.input_file << "\n";
        return 1;
    }
    std::string sourceText(data->data(), data->size());

    // Резолвим конфиг .trust-format (если задан) + CLI-переопределения.
    trust::formatter::FormatConfig cfg;
    cfg.ok = true;
    std::string cfgPath;
    if (!opts.format_config.empty()) {
        cfgPath = opts.format_config;
    } else if (!opts.format_no_config) {
        cfgPath = trust::formatter::findConfig(std::filesystem::path(opts.input_file).parent_path().string());
    }
    if (!cfgPath.empty()) {
        cfg = trust::formatter::loadConfig(cfgPath);
        if (!cfg.ok) {
            trust::errs() << "error: " << cfg.error << "\n";
            return 1;
        }
    }
    trust::formatter::FormatOptions fopts = cfg.opts;
    // Эффективные keywords (дефолт dsl + приоритет CLI > .trust-format) для форматирования.
    fopts.keywords = keywords;
    // CLI --keyword-sigil (валидирован в parseArgs) переопределяет .trust-format.
    trust::formatter::KeywordSigil ksig;
    if (!opts.keyword_sigil.empty() && trust::formatter::keywordSigilFromString(opts.keyword_sigil, ksig)) {
        fopts.keyword_sigil = ksig;
    }

    // Форматтер подписывается на Macro::on_macro_kind, прогоняет парсинг (в т.ч. модули) и
    // собирает классификацию макросов in-stream. Диагностики парсинга подавлены.
    ctx.diag().setMinSeverity(trust::Severity::Fatal);
    trust::ParserServices parseServices = trust::makeParserServices(ctx);
    trust::Parser parser(parseServices);
    auto fres = trust::formatter::format(sourceText, opts.input_file, fopts, parser);
    if (!fres.ok) {
        trust::errs() << "error: cannot format '" << opts.input_file << "': " << fres.error << "\n";
        return 1;
    }
    if (opts.format_check) {
        if (fres.text == sourceText) {
            return 0;
        }
        trust::errs() << opts.input_file << ": not formatted\n";
        return 1;
    }
    trust::outs() << fres.text;
    return 0;
}

} // namespace trust
