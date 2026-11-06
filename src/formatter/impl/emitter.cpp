// Эмиттер форматтера: один проход по токенам — зазоры/комментарии, переводы строк, пробелы, отступы.

#include "formatter/impl/emitter.hpp"

#include "utils/strings.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace trust::formatter {

Emitter::Emitter(std::vector<Token> tokens, const FormatOptions& opts)
: toks_(std::move(tokens))
, opts_(opts) {
    precomputeGroups();
}

void Emitter::setSource(std::string_view src) {
    sourceView_ = src;
}

std::string Emitter::run() {
    int prev = -1; // индекс предыдущего реального токена, или -1
    for (int i = 0; i < static_cast<int>(toks_.size()); ++i) {
        const Token& tok = toks_[i];
        // Зазор перед токеном: для первого токена — от начала источника (ведущие
        // комментарии не теряем), иначе между предыдущим и текущим токенами.
        const Gap gap = (prev >= 0) ? parseGap(gapText(prev, i)) : parseGap(sourceView_.substr(0, std::min(toks_[i].begin, sourceView_.size())));

        if (gap.verbatim) {
            // Невидимый разделитель (raw/embed/macro): сохраняем делимитер как есть,
            // но нормализуем его ВЕДУЩИЕ пробелы по правилам интервалов.
            const std::string_view gtext = gapText(prev, i);
            size_t ws = 0;
            while (ws < gtext.size() && (gtext[ws] == ' ' || gtext[ws] == '\t' || gtext[ws] == '\r' || gtext[ws] == '\n')) {
                ++ws;
            }
            const std::string_view lead = gtext.substr(0, ws);
            const std::string_view delim = gtext.substr(ws);
            const int leadNl = static_cast<int>(std::count(lead.begin(), lead.end(), '\n'));
            const bool needNl = (prev >= 0) && (computeNewline(prev, i) || leadNl > 0);
            const bool needSpace = (prev >= 0) && computeSpace(prev, i);
            if (needNl) {
                appendNewlines(leadNl);
                emitIndent();
            } else if (!lineStart_ && needSpace) {
                out_ += ' ';
            } else if (lineStart_) {
                emitIndent();
            }
            out_ += delim;
            out_ += tok.text;
            lineStart_ = false;
            prev = i;
            continue;
        }

        // Комментарии из безопасного зазора.
        emitGapComments(gap);

        // Логика отступа для } и закрывающих скобок многострочных групп — до записи.
        if (tok.id == TermID::RBRACE) {
            if (indent_ > 0) {
                --indent_;
            }
        } else if (tok.id == TermID::RPAREN || tok.id == TermID::RBRACKET) {
            if (groupMultilineAt(i) && indent_ > 0) {
                --indent_;
            }
        }

        const bool nl = computeNewline(prev, i);

        // Отступ клаузы контрактов (trust_* / @{...@}): первый присоединённый контракт
        // после объявления — с отступом (+1); тело ':= ' возвращается на базовый отступ.
        if (contractIndent_ > 0 && (tok.id == TermID::LBRACE || tok.id == TermID::CREATE_NAME || tok.id == TermID::CREATE_TYPE || tok.id == TermID::ASSIGN)) {
            contractIndent_ = 0;
        }
        if (prev >= 0 && contractIndent_ == 0 && isContractToken(i) &&
            (isNameLike(toks_[prev].id) || toks_[prev].id == TermID::RPAREN || toks_[prev].id == TermID::RBRACKET || toks_[prev].id == TermID::TRUST_END)) {
            contractIndent_ = 1;
        }

        if (nl) {
            // Пустые строки перед закрывающими скобками не сохраняем (стандарт).
            const bool closingBracket = (tok.id == TermID::RBRACE || tok.id == TermID::RPAREN || tok.id == TermID::RBRACKET);
            // Пустые строки сохраняем только в чисто пробельных зазорах (без комментариев),
            // иначе идемпотентность нарушается (новые строки комментариев пересчитываются).
            bool hasComment = false;
            for (const Seg& s : gap.segs) {
                if (s.kind != SegKind::Whitespace) {
                    hasComment = true;
                    break;
                }
            }
            appendNewlines((closingBracket || hasComment) ? 0 : gap.newlines);
            emitIndent();
        } else if (!lineStart_) {
            if (computeSpace(prev, i)) {
                out_ += ' ';
            }
        } else {
            emitIndent();
        }

        out_ += keywordText(tok);
        lineStart_ = false;

        // Увеличение отступа после { и после многострочных ( / [.
        if (tok.id == TermID::LBRACE) {
            ++indent_;
        } else if ((tok.id == TermID::LPAREN || tok.id == TermID::LBRACKET) && multiline_[i]) {
            ++indent_;
        }

        prev = i;
    }
    // Комментарии после последнего токена (в конце файла) не теряем.
    // Файл только с комментарием (prev == -1) тоже покрыт (start = 0).
    {
        const size_t start = (prev >= 0) ? toks_[prev].end : 0;
        if (start < sourceView_.size()) {
            const Gap tailGap = parseGap(sourceView_.substr(start));
            if (tailGap.verbatim) {
                // Хвост содержит НЕ-тривиальный текст, не представленный токенами
                // (после ошибки лексера его поглотила незакрытая строка/комментарий и
                // он не доехал до on_token). Сохраняем исходник вербатим: он остаётся
                // неотформатированным, но ни в коем случае не теряется.
                out_ += sourceView_.substr(start);
            } else {
                emitGapComments(tailGap);
            }
        }
    }
    // Конец файла: гарантируем завершающий перевод строки.
    if (opts_.insert_final_newline && !out_.empty() && out_.back() != '\n') {
        out_ += '\n';
    }
    return out_;
}

std::string_view Emitter::gapText(int a, int b) const {
    const size_t start = toks_[a].end;
    const size_t end = toks_[b].begin;
    if (start > end) {
        return "";
    }
    return sourceView_.substr(start, end - start);
}

int Emitter::countNewlines(const std::string& s) {
    return static_cast<int>(std::count(s.begin(), s.end(), '\n'));
}

void Emitter::appendCommentNewlines(int pendingNl) {
    if (!lineStart_) {
        out_ += '\n';
        lineStart_ = true;
    }
    if (opts_.preserve_blank_lines && pendingNl > 1) {
        out_ += '\n'; // одна пустая строка
    }
}

void Emitter::emitGapComments(const Gap& gap) {
    int pendingNl = 0; // новых строк в пробелах перед текущим комментарием
    bool emitted = false;
    for (const Seg& seg : gap.segs) {
        if (seg.kind == SegKind::Whitespace) {
            pendingNl += countNewlines(seg.text);
            continue;
        }
        emitted = true;
        if (seg.kind == SegKind::LineComment) {
            appendCommentNewlines(pendingNl);
            pendingNl = 0;
            emitIndent();
            out_ += seg.text;
            out_ += '\n';
            lineStart_ = true;
        } else { // BlockComment
            if (pendingNl > 0 || lineStart_) {
                // блок-комментарий на отдельной строке
                appendCommentNewlines(pendingNl);
                pendingNl = 0;
                emitIndent();
                out_ += seg.text;
                out_ += '\n';
                lineStart_ = true;
            } else {
                // блок-комментарий инлайн после предыдущего токена
                out_ += ' ';
                out_ += seg.text;
                pendingNl = 0;
                lineStart_ = false;
            }
        }
    }
    // Пустые строки после последнего комментария (перед следующим токеном/концом файла).
    if (emitted && opts_.preserve_blank_lines && pendingNl > 1) {
        if (!lineStart_) {
            out_ += '\n';
            lineStart_ = true;
        }
        out_ += '\n';
    }
}

void Emitter::appendNewlines(int inGap) {
    if (!lineStart_) {
        out_ += '\n';
        lineStart_ = true;
    }
    // Схлопываем несколько пустых строк в одну (идемпотентно).
    if (opts_.preserve_blank_lines && inGap > 1) {
        out_ += '\n';
    }
}

std::string Emitter::indentString() const {
    const int total = indent_ + contractIndent_;
    if (opts_.use_spaces) {
        return std::string(static_cast<size_t>(total * opts_.tab_size), ' ');
    }
    return std::string(static_cast<size_t>(total), '\t');
}

void Emitter::emitIndent() {
    if (lineStart_ && (indent_ > 0 || contractIndent_ > 0)) {
        out_ += indentString();
    }
}
bool Emitter::computeNewline(int prev, int i) {
    const Token& tok = toks_[i];
    if (tok.id == TermID::RBRACE) {
        return true;
    }
    if (tok.id == TermID::DOCUMENT) {
        return true; // документирующий комментарий — на отдельной строке
    }
    if (prev < 0) {
        return false;
    }
    const Token& p = toks_[prev];
    if (p.id == TermID::LBRACE) {
        return true; // тело блока — с новой строки
    }
    if (p.id == TermID::RBRACE) {
        // Завершающая точка с запятой сразу после '}' остаётся на той же строке (};),
        // а не переносится на новую.
        if (tok.id == TermID::SEMICOLON) {
            return false;
        }
        return true; // после закрытия блока
    }
    if (p.id == TermID::SEMICOLON) {
        return true; // конец statement
    }
    if (p.id == TermID::DOCUMENT) {
        return true; // после документирующего комментария
    }
    // '@]' лексится как ATTR_COMPLETE, но распознаём по тексту (маркер стабилен).
    if (p.text == "@]" && !inParen_[i]) {
        const TermID nid = tok.id;
        if (isNameLike(nid) || nid == TermID::PERCENT || nid == TermID::NATIVE || isKeywordText(tok.text)) {
            return true;
        }
    }
    // Ключевое слово (управляющий макрос) после ведущего термина/макроса на уровне
    // statement — начинается с новой строки (не только после атрибута).
    if (!inParen_[i] && isKeywordLike(tok.text) && (p.text == "@]" || p.id == TermID::MACRO)) {
        return true;
    }
    // Контракт/инвариант (trust_* или @{...@}) после объявления — на новой строке с отступом.
    if (isContractToken(i) && (isNameLike(p.id) || p.id == TermID::RPAREN || p.id == TermID::RBRACKET || p.id == TermID::TRUST_END)) {
        return true;
    }
    // Многострочная группа: разбивка по запятым и на границах ( / [.
    const int group = groupOf(prev);
    if (group >= 0 && multiline_[group]) {
        if (p.id == TermID::LPAREN || p.id == TermID::LBRACKET) {
            return true;
        }
        if (tok.id == TermID::RPAREN || tok.id == TermID::RBRACKET) {
            return true;
        }
        if (p.id == TermID::COMMA) {
            return true;
        }
    }
    return false;
}

bool Emitter::isKeywordText(const std::string& t) const {
    // «Ключевые слова» — это имена из эффективного списка keywords (DSL-макросы, без
    // встроенного набора). Распознавание не зависит от ведущего '@' (в обеих сторонах):
    // и '@if'/'@return', и 'if'/'return' трактуются как ключевое слово (важно для пробела
    // перед '(' и '{', а также согласовано с опцией KeywordSigil).
    const std::string_view tv = trust::utils::stripMacroSigil(t);
    for (const std::string& kw : opts_.keywords) {
        const std::string_view k = trust::utils::stripMacroSigil(kw);
        if (k == tv) {
            return true;
        }
    }
    return false;
}

bool Emitter::isKeywordLike(const std::string& t) const {
    const std::string_view v = trust::utils::stripMacroSigil(t);
    for (const std::string& kw : opts_.keywords) {
        const std::string_view k = trust::utils::stripMacroSigil(kw);
        if (k == v) {
            return true;
        }
    }
    return false;
}

std::string Emitter::keywordText(const Token& tok) const {
    if (!isKeywordLike(tok.text)) {
        return tok.text;
    }
    const std::string_view bare = trust::utils::stripMacroSigil(tok.text);
    switch (opts_.keyword_sigil) {
    case KeywordSigil::Remove:
        return std::string(bare);
    case KeywordSigil::Add:
        return (!tok.text.empty() && tok.text[0] == '@') ? tok.text : "@" + std::string(bare);
    case KeywordSigil::None:
    default:
        return tok.text;
    }
}

bool Emitter::isNoParenKeyword(int idx) const {
    return toks_[idx].noParen;
}

bool Emitter::isContractToken(int i) const {
    return toks_[i].id == TermID::TRUST_BEGIN || toks_[i].contract;
}
bool Emitter::computeSpace(int prev, int i) {
    const Token& tok = toks_[i];
    const Token& p = toks_[prev];

    // `%` — префикс нативного вызова/метода (%printf, x.%method): нет пробела после.
    if (p.id == TermID::PERCENT) {
        return false;
    }

    // `++`/`--`: позицию (префикс/постфикс) определяет соседний операнд.
    const bool prevOperandEnd = isNameLike(p.id) || p.id == TermID::RPAREN || p.id == TermID::RBRACKET;
    if (tok.id == TermID::OP_INC || tok.id == TermID::OP_DEC) {
        return !prevOperandEnd; // постфикс `x++` - без пробела; префикс `++x` - пробел перед
    }
    if (p.id == TermID::OP_INC || p.id == TermID::OP_DEC) {
        const int pp = (prev >= 1) ? prev - 1 : -1;
        const bool prevWasPostfix = (pp >= 0) && (isNameLike(toks_[pp].id) || toks_[pp].id == TermID::RPAREN || toks_[pp].id == TermID::RBRACKET);
        if (!prevWasPostfix) {
            return false; // префикс `++x` приклеен к операнду (нет пробела после)
        }
        // Постфикс `x++`: пробел после решают общие правила ниже (напр. нет пробела перед `;`).
    }

    // Нет пробела перед закрывающими скобками, разделителями, скобками-открытиями.
    switch (tok.id) {
    case TermID::RPAREN:
    case TermID::RBRACKET:
    case TermID::COMMA:
    case TermID::SEMICOLON:
    case TermID::DOT:
        return false;
    case TermID::COLON:
        // No-paren keyword-макрос (return/@return): пробел перед ':' (return :Tuple(...)).
        if (isNoParenKeyword(prev)) {
            return true;
        }
        // ':' — сигил типа: пробел перед кастом (после ,/=/:=), иначе приклеен к имени.
        if (p.id == TermID::COMMA) {
            return true;
        }
        if (isNameLike(p.id)) {
            return false;
        }
        if (p.id == TermID::LPAREN || p.id == TermID::LBRACKET) {
            return false;
        }
        return true;
    case TermID::LPAREN:
    case TermID::LBRACKET:
        // пробел перед '(' / '[' после оператора/ключевого слова; после callee — нет.
        if (isBinaryOperator(p.id) || isKeywordText(p.text) || isNoParenKeyword(prev)) {
            return true;
        }
        return false;
    default:
        break;
    }

    // { на той же строке, что и заголовок/сигнатура, но с пробелом перед.
    if (tok.id == TermID::LBRACE) {
        return true;
    }
    // Бинарный оператор — пробел с обеих сторон.
    if (isBinaryOperator(tok.id)) {
        return true;
    }

    // Нет пробела после открывающих скобок, точки, @, &.
    if (p.id == TermID::LPAREN || p.id == TermID::LBRACKET || p.id == TermID::DOT || p.id == TermID::AT || p.id == TermID::OPERATOR_PTR) {
        return false;
    }
    // Нет пробела после ':' (сигил типа ':Int32'). Внутри raw-контракта `@{ expr @}` kind НЕ
    // указывается (вынесен перед маркером), поэтому ':' в выражении форматируется единообразно.
    if (p.id == TermID::COLON) {
        return false;
    }
    // Нет пробела после унарного префикса.
    if (p.id == TermID::TAKE) {
        return false;
    }
    // Разделители — пробел после.
    if (p.id == TermID::COMMA) {
        return true;
    }
    // Бинарный оператор — пробел после.
    if (isBinaryOperator(p.id)) {
        return true;
    }
    // Унарный префикс перед токеном.
    if (isUnaryOperator(tok.id) || tok.id == TermID::TAKE) {
        return false;
    }
    return true; // между словами/значениями
}
int Emitter::groupOf(int idx) const {
    if (openStack_[idx] >= 0) {
        return openStack_[idx];
    }
    if (match_[idx] >= 0 && isOpen(toks_[match_[idx]].id)) {
        return match_[idx];
    }
    return -1;
}

bool Emitter::groupMultilineAt(int idx) const {
    if (match_[idx] >= 0 && isOpen(toks_[match_[idx]].id)) {
        return multiline_[match_[idx]];
    }
    return false;
}

void Emitter::precomputeGroups() {
    const int n = static_cast<int>(toks_.size());
    match_.assign(n, -1);
    multiline_.assign(n, false);
    openStack_.assign(n, -1);
    std::vector<int> stack;
    for (int i = 0; i < n; ++i) {
        const TermID id = toks_[i].id;
        if (isOpen(id)) {
            stack.push_back(i);
        } else if (isClose(id)) {
            if (!stack.empty()) {
                const int o = stack.back();
                stack.pop_back();
                match_[o] = i;
                match_[i] = o;
                openStack_[i] = o;
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        if (!isOpen(toks_[i].id)) {
            continue;
        }
        const int c = match_[i];
        if (c < 0) {
            continue;
        }
        if (toks_[i].id == TermID::LBRACE) {
            multiline_[i] = true; // {} всегда блок
        } else {
            multiline_[i] = !groupFitsInline(i, c) || containsForcedMultiline(i, c);
        }
    }
    // Признак «внутри скобок ( / [» — чтобы не переносить инлайн-атрибуты внутри сигнатур.
    inParen_.assign(n, false);
    int parenDepth = 0;
    for (int i = 0; i < n; ++i) {
        const TermID id = toks_[i].id;
        if (id == TermID::LPAREN || id == TermID::LBRACKET) {
            ++parenDepth;
        }
        inParen_[i] = parenDepth > 0;
        if (id == TermID::RPAREN || id == TermID::RBRACKET) {
            if (parenDepth > 0) {
                --parenDepth;
            }
        }
    }
}

bool Emitter::containsForcedMultiline(int o, int c) const {
    for (int k = o + 1; k < c; ++k) {
        const TermID id = toks_[k].id;
        if (id == TermID::LBRACE || isDoc(id) || isDocInline(id)) {
            return true; // вложенный блок/док — принудительно многострочно
        }
    }
    return false;
}

bool Emitter::groupFitsInline(int o, int c) const {
    size_t w = 2; // скобки
    for (int k = o + 1; k < c; ++k) {
        w += toks_[k].text.size();
        const TermID id = toks_[k].id;
        if (id == TermID::COMMA || id == TermID::COLON || isBinaryOperator(id) || isKeywordText(toks_[k].text)) {
            w += 1;
        }
    }
    return w <= static_cast<size_t>(std::max(opts_.max_line_width, 1));
}

} // namespace trust::formatter
