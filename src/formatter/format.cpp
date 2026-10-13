// src/formatter/format.cpp
// Точка входа форматтера: прогон парсера для сбора токенов и макро-классификации,
// затем один проход Emitter. Модель токена/зазора — format_model.hpp, эмиттер — emitter.cpp.

#include "formatter/format.hpp"

#include "formatter/impl/emitter.hpp"
#include "formatter/impl/format_model.hpp"
#include "session/context.hpp"
#include "syntax/lexer.h"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "syntax/term/term.h"
#include "syntax/term/term_types.h"
#include "utils/strings.hpp"
#include "utils/utils.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust::formatter {

using trust::MapperFile;
using trust::TermID;

// Событие классификации макроса (in-stream) для timeline.
struct MacroEvent {
    size_t pos;       // 0-based offset события в источнике
    std::string name; // имя макроса без '@'
    MacroKind kind;
    bool removed;
};

FormatResult format(std::string_view source, const std::string& sourceName, const FormatOptions& opts, Parser& parser) {
    FormatResult res;

    // Регистрируем источник в контексте парсера (для ParseWithSource и сырого скана).
    MapperFile srcIdx;
    try {
        srcIdx = parser.context().source().add_source(sourceName, std::string(source));
    } catch (const std::exception& e) {
        res.ok = false;
        res.error = std::string("formatter: cannot register source: ") + e.what();
        return res;
    }

    // Классификация макросов — через события: базовые (pos 0) + in-stream on_macro_kind.
    std::unordered_map<std::string, MacroKind> active;
    Macro* macro = parser.context().macro().get();
    std::vector<MacroEvent> events;
    std::vector<Token> toks;
    const auto prevOnMacroKind = macro ? macro->on_macro_kind : nullptr;
    const auto prevOnToken = parser.on_token;
    if (macro) {
        for (const auto& [name, kind] : macro->macroKinds()) {
            events.push_back({0, name, kind, /*removed=*/false});
        }
        macro->on_macro_kind = [&events](std::string_view n, MacroKind k, bool r, size_t pos) {
            events.push_back({(pos > 0) ? pos - 1 : 0, std::string(n), k, r});
        };
    }
    parser.on_token = [&](const Term& t) {
        if (t.getTermID() == TermID::END) {
            return;
        }
        const auto& rng = t.m_mapperRange;
        Token tok;
        tok.id = t.getTermID();
        // MapperRange хранит 1-based смещения; для substr нужны 0-based.
        tok.begin = (rng.begin.offset() > 0) ? rng.begin.offset() - 1 : 0;
        tok.end = (rng.end.offset() > 0) ? rng.end.offset() - 1 : 0;
        tok.text = std::string(source.substr(tok.begin, tok.end - tok.begin));
        toks.push_back(std::move(tok));
    };

    // Прогон парсера: токены и классификация макросов собираются in-stream.
    parser.context().diag().setMinSeverity(Severity::Fatal);
    try {
        parser.ParseWithSource(srcIdx, /*expand_module=*/true);
    } catch (const std::exception&) {
    }

    parser.on_token = prevOnToken;
    if (macro) {
        macro->on_macro_kind = prevOnMacroKind;
    }
    // stable: начальные add-события (pos 0) применяются раньше in-stream событий на той же позиции.
    std::stable_sort(events.begin(), events.end(), [](const MacroEvent& a, const MacroEvent& b) { return a.pos < b.pos; });

    // Применяем timeline классификации к токенам по позиции.
    size_t ev = 0;
    for (auto& t : toks) {
        while (ev < events.size() && events[ev].pos <= t.begin) {
            if (events[ev].removed) {
                active.erase(events[ev].name);
            } else {
                active[events[ev].name] = events[ev].kind;
            }
            ++ev;
        }
        const std::string name = std::string(trust::utils::stripMacroSigil(t.text));
        const auto it = active.find(name);
        if (it != active.end()) {
            t.contract = hasKind(it->second, MacroKind::Contract);
            t.noParen = hasKind(it->second, MacroKind::NoParen) && name != "main";
        }
    }

    // `n-1` в грамматике — бинарный минус; расщепляем, чтобы выводить `n - 1`.
    splitNegativeLiterals(toks);

    Emitter emitter(std::move(toks), opts);
    emitter.setSource(source);
    res.text = emitter.run();
    res.ok = true;
    return res;
}

FormatResult formatFile(const std::string& path, const FormatOptions& opts) {
    auto data = trust::utils::FileIO::read<std::vector<char>>(path);
    if (!data) {
        FormatResult res;
        res.ok = false;
        res.error = "formatter: cannot open file: " + path;
        return res;
    }
    std::string text(data->data(), data->size());
    // Standalone: без загрузки DSL макро-классификация недоступна (только raw-@{...@}).
    trust::Context ctx;
    trust::Parser parser(ctx);
    return trust::formatter::format(text, path, opts, parser);
}

} // namespace trust::formatter