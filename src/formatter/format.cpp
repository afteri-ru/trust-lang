#include "formatter/format.hpp"

#include "diag/context.hpp"
#include "syntax/lexer.h"
#include "syntax/macro.h"
#include "syntax/parser.h"
#include "syntax/term.h"
#include "syntax/term_types.h"
#include "utils/file_io.hpp"
#include "utils/strings.hpp"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace trust::formatter {

namespace {

using trust::MapperFile;
using trust::TermID;

// ════════════════════════════════════════════════════════════════
//  Классификация токенов
// ════════════════════════════════════════════════════════════════

bool isOpen(TermID id) {
    return id == TermID::LBRACE || id == TermID::LPAREN || id == TermID::LBRACKET;
}
bool isClose(TermID id) {
    return id == TermID::RBRACE || id == TermID::RPAREN || id == TermID::RBRACKET;
}

// Бинарные операторы и присваивания — пробел с обеих сторон.
bool isBinaryOperator(TermID id) {
    switch (id) {
    case TermID::EQ:
    case TermID::PLUS:
    case TermID::MINUS:
    case TermID::STAR:
    case TermID::SLASH:
    case TermID::AMP:
    case TermID::PIPE:
    case TermID::CARET:
    case TermID::LT:
    case TermID::GT:
    case TermID::ASSIGN:
    case TermID::APPEND:
    case TermID::SWAP:
    case TermID::CREATE_TYPE:
    case TermID::CREATE_NAME:
    case TermID::OP_MATH:
    case TermID::OP_COMPARE:
    case TermID::OP_BITWISE:
    case TermID::OP_LOGICAL:
    case TermID::RANGE:
    case TermID::ELLIPSIS:
        return true;
    default:
        return false;
    }
}

// Унарные операторы-префиксы — без пробела после (прикреплены к операнду).
bool isUnaryOperator(TermID id) {
    switch (id) {
    case TermID::BANG:         // !
    case TermID::TILDE:        // ~
    case TermID::TAKE:         // * (deref)
    case TermID::OPERATOR_PTR: // & (ptr)
    case TermID::AT:           // @
        return true;
    default:
        return false;
    }
}

// Управляющие/объявляющие «ключевые слова» в Trust НЕ являются встроенными — это DSL-макросы
// (if/while/return/...), которые определяются и могут переопределяться в dsl.src / --dsl. Поэтому
// форматтер не имеет встроенного набора: набор «ключевых слов» = эффективный список keywords
// (см. Emitter::isKeywordText), передаваемый через FormatOptions.keywords.

// Документирующий комментарий (## / /// / /** */).
bool isDoc(TermID id) {
    return id == TermID::DOCUMENT;
}
bool isDocInline(TermID id) {
    return id == TermID::DOCUMENT_INLINE;
}

// Операнд-подобные токены: имя/литерал/закрывающая скобка (для решения про ':'-аннотацию).
bool isNameLike(TermID id) {
    switch (id) {
    case TermID::NAME:
    case TermID::LOCAL:
    case TermID::MACRO:
    case TermID::MODULE:
    case TermID::MANGLED:
    case TermID::INTEGER:
    case TermID::NUMBER:
    case TermID::COMPLEX:
    case TermID::RATIONAL:
    case TermID::STRWIDE:
    case TermID::STRCHAR:
    case TermID::REFLECTION:
    case TermID::ARGUMENT:
    case TermID::DOLLAR:
    case TermID::RPAREN:
    case TermID::RBRACKET:
        return true;
    default:
        return false;
    }
}

// Числовые литералы, способные нести ведущий '-' в тексте токена
// (INTEGER `-5`, COMPLEX `-1.5j`, RATIONAL `-1\2`; NUMBER текстом с '-' не бывает).
bool isNumericLiteral(TermID id) {
    switch (id) {
    case TermID::INTEGER:
    case TermID::NUMBER:
    case TermID::COMPLEX:
    case TermID::RATIONAL:
        return true;
    default:
        return false;
    }
}

// ════════════════════════════════════════════════════════════════
//  Данные токена и зазора
// ════════════════════════════════════════════════════════════════

struct Token {
    TermID id;
    size_t begin; // 0-based byte offset
    size_t end;
    std::string text;
    // Классификация макроса, актуальная на позицию токена (заполняется в потоке парсера):
    bool contract = false; // макрос-контракт (trust_* / source/module контракт)
    bool noParen = false;  // no-paren макрос (return/break/...) — пробел после имени
};

// Расщепляет отрицательный числовой литерал, идущий сразу после операнда, на
// бинарный MINUS + литерал без ведущего '-'. Зеркалит грамматическое правило
// `arithmetic: addition digits` (parser.y.in): `n-1` в потоке токенов — это
// `n`, `-1`(INTEGER), а грамматика трактует его как `n - 1`. Чтобы форматтер
// выводил `n - 1`, а не `n -1`, повторяем это решение на уровне токенов.
// В позиции не-операнда (после `(`, `,`, `=`, `:=`, оператора, начала) отрицательный
// литерал сохраняется как есть (`x := -5`, `fib(-1)`, `a * -1`).
void splitNegativeLiterals(std::vector<Token>& toks) {
    for (size_t i = 1; i < toks.size(); ++i) {
        const Token& t = toks[i];
        if (!t.text.empty() && t.text[0] == '-' && isNumericLiteral(t.id) && isNameLike(toks[i - 1].id)) {
            Token minus = t;
            minus.id = TermID::MINUS;
            minus.text = "-";
            minus.end = t.begin + 1;

            Token literal = t;
            literal.begin += 1;
            literal.text = t.text.substr(1);

            toks[i] = std::move(literal);
            toks.insert(toks.begin() + static_cast<std::ptrdiff_t>(i), std::move(minus));
            ++i; // пропускаем новый MINUS (и литерал — инкремент цикла)
        }
    }
}

enum class SegKind { Whitespace, LineComment, BlockComment };
struct Seg {
    SegKind kind;
    std::string text;
};

struct Gap {
    bool verbatim = false; // зазор содержит невидимый разделитель (raw/embed/macro) — сохранить как есть
    std::vector<Seg> segs; // для не-veratim: разбитые пробелы/комментарии
    int newlines = 0;      // число переводов строк в зазоре (для пустых строк)
};

// Разбор зазора: если встречается любой не-пробел/не-комментарий символ — весь зазор verbatim.
Gap parseGap(std::string_view s) {
    Gap g;
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            size_t j = i;
            while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\r' || s[j] == '\n')) {
                if (s[j] == '\n') {
                    ++g.newlines;
                }
                ++j;
            }
            g.segs.push_back({SegKind::Whitespace, std::string(s.substr(i, j - i))});
            i = j;
        } else if (c == '#') {
            // Строковый комментарий #... (## — отдельный токен DOCUMENT, в зазор не попадает).
            size_t j = i;
            while (j < n && s[j] != '\n') {
                ++j;
            }
            g.segs.push_back({SegKind::LineComment, std::string(s.substr(i, j - i))});
            i = j;
        } else if (c == '/' && i + 1 < n && s[i + 1] == '*') {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == '*' && s[j + 1] == '/')) {
                ++j;
            }
            j = (j + 2 <= n) ? j + 2 : n;
            g.segs.push_back({SegKind::BlockComment, std::string(s.substr(i, j - i))});
            i = j;
        } else {
            g.verbatim = true;
            g.segs.clear();
            return g;
        }
    }
    return g;
}

// ════════════════════════════════════════════════════════════════
//  Эмиттер
// ════════════════════════════════════════════════════════════════

class Emitter {
  public:
    Emitter(std::vector<Token> tokens, const FormatOptions& opts)
    : toks_(std::move(tokens))
    , opts_(opts) {
        precomputeGroups();
    }

    void setSource(std::string_view src) { sourceView_ = src; }

    std::string run() {
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
            if (contractIndent_ > 0 &&
                (tok.id == TermID::LBRACE || tok.id == TermID::CREATE_NAME || tok.id == TermID::CREATE_TYPE || tok.id == TermID::ASSIGN)) {
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

            out_ += tok.text;
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

  private:
    // Текст зазора между токенами a и b (0-based offsets).
    std::string_view gapText(int a, int b) const {
        const size_t start = toks_[a].end;
        const size_t end = toks_[b].begin;
        if (start > end) {
            return "";
        }
        return sourceView_.substr(start, end - start);
    }

    // Число переводов строк в строке.
    static int countNewlines(const std::string& s) { return static_cast<int>(std::count(s.begin(), s.end(), '\n')); }

    // Обеспечивает перевод строки перед комментарием, сохраняя пустые строки
    // (схлопывая их до одной) — идемпотентно. Вызывается только когда комментарий
    // должен начинаться с новой строки.
    void appendCommentNewlines(int pendingNl) {
        if (!lineStart_) {
            out_ += '\n';
            lineStart_ = true;
        }
        if (opts_.preserve_blank_lines && pendingNl > 1) {
            out_ += '\n'; // одна пустая строка
        }
    }

    // Эмитит комментарии из зазора (между токенами, в начале или в конце файла).
    // Сохраняет пустые строки вокруг комментариев (схлопывая до одной).
    void emitGapComments(const Gap& gap) {
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

    void appendNewlines(int inGap) {
        if (!lineStart_) {
            out_ += '\n';
            lineStart_ = true;
        }
        // Схлопываем несколько пустых строк в одну (идемпотентно).
        if (opts_.preserve_blank_lines && inGap > 1) {
            out_ += '\n';
        }
    }

    std::string indentString() const {
        const int total = indent_ + contractIndent_;
        if (opts_.use_spaces) {
            return std::string(static_cast<size_t>(total * opts_.tab_size), ' ');
        }
        return std::string(static_cast<size_t>(total), '\t');
    }

    void emitIndent() {
        if (lineStart_ && (indent_ > 0 || contractIndent_ > 0)) {
            out_ += indentString();
        }
    }

    // Решает: перевод строки перед токеном i.
    bool computeNewline(int prev, int i) {
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
        // Атрибут @[ ... ]@ на предыдущей строке: имя/ключевое слово/нативный вызов после
        // ']@' (вне группы скобок) начинают новую строку. ']@' лексится как TermID::ATTR_COMPLETE
        // (YY_TOKEN), но распознаём по тексту токена (маркер стабилен).
        if (p.text == "]@" && !inParen_[i]) {
            const TermID nid = tok.id;
            if (isNameLike(nid) || nid == TermID::PERCENT || nid == TermID::NATIVE || isKeywordText(tok.text)) {
                return true;
            }
        }
        // Ключевое слово (управляющий макрос) после ведущего термина/макроса на уровне
        // statement — начинается с новой строки (не только после атрибута).
        if (!inParen_[i] && isKeywordLike(tok.text) && (p.text == "]@" || p.id == TermID::MACRO)) {
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

    // Решает: пробел перед токеном i (когда нет перевода строки).
    bool isKeywordText(const std::string& t) const {
        // «Ключевые слова» — это имена из эффективного списка keywords (DSL-макросы, без
        // встроенного набора). Записи могут иметь ведущий '@' (сравниваем без него).
        for (const std::string& kw : opts_.keywords) {
            std::string_view k = kw;
            if (!k.empty() && k[0] == '@') {
                k.remove_prefix(1);
            }
            if (k == t) {
                return true;
            }
        }
        return false;
    }
    // «Похоже на ключевое слово» — имя из keywords с необязательным ведущим '@'
    // (распознаёт и '@if'/'@return', и 'if'/'return'). Используется для переноса строки
    // перед управляющим макросом после ведущего термина/атрибута.
    bool isKeywordLike(const std::string& t) const {
        const std::string_view v = trust::utils::strip_macro_sigil(t);
        for (const std::string& kw : opts_.keywords) {
            const std::string_view k = trust::utils::strip_macro_sigil(kw);
            if (k == v) {
                return true;
            }
        }
        return false;
    }
    // No-paren keyword-макрос (return/@return и др.): определён без скобок — всегда пробел
    // после имени. Классификация зафиксирована на позицию токена (см. Token::noParen).
    bool isNoParenKeyword(int idx) const { return toks_[idx].noParen; }
    // Контракт/инвариант Trust: raw-маркер @{...@} (TRUST_BEGIN) или макрос-контракт
    // (trust_* / source/module), классификация зафиксирована на позицию токена.
    bool isContractToken(int i) const { return toks_[i].id == TermID::TRUST_BEGIN || toks_[i].contract; }

    // Решает: пробел перед токеном i (когда нет перевода строки).
    bool computeSpace(int prev, int i) {
        const Token& tok = toks_[i];
        const Token& p = toks_[prev];

        // `%` — префикс нативного вызова/метода (%printf, x.%method): нет пробела после.
        if (p.id == TermID::PERCENT) {
            return false;
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
            // ':' — сигил типа. Перед ним пробел, если он открывает каст (после ,/=/:=),
            // и нет пробела, если это аннотация после имени (name:Type) или после скобки.
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
            // перед '(' / '[' пробел после бинарного оператора или управляющего ключевого
            // слова (if (, x := (...), return (); без пробела после имени callee (f(), arr[i]).
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
        // Нет пробела после ':' (сигил типа ':Int32'), кроме 'kind: expr' внутри raw-контракта.
        if (p.id == TermID::COLON) {
            if (inTrust_[i]) {
                return true; // '@{ invariant: s @}' — пробел после ':'
            }
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

    // Группа (открывающая скобка), к которой относится индекс токена idx. -1 если вне группы.
    int groupOf(int idx) const {
        if (openStack_[idx] >= 0) {
            return openStack_[idx];
        }
        if (match_[idx] >= 0 && isOpen(toks_[match_[idx]].id)) {
            return match_[idx];
        }
        return -1;
    }

    // Многострочна ли группа, закрывающей скобкой которой является токен idx.
    bool groupMultilineAt(int idx) const {
        if (match_[idx] >= 0 && isOpen(toks_[match_[idx]].id)) {
            return multiline_[match_[idx]];
        }
        return false;
    }

    // Предрасчёт соответствий скобок и признака «многострочная группа».
    void precomputeGroups() {
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
        // Признак «внутри raw-контракта @{ ... @}» (для пробела после ':' в 'kind: expr').
        inTrust_.assign(n, false);
        int trustDepth = 0;
        for (int i = 0; i < n; ++i) {
            const TermID id = toks_[i].id;
            if (id == TermID::TRUST_BEGIN) {
                ++trustDepth;
            }
            inTrust_[i] = trustDepth > 0;
            if (id == TermID::TRUST_END && trustDepth > 0) {
                --trustDepth;
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

    bool containsForcedMultiline(int o, int c) const {
        for (int k = o + 1; k < c; ++k) {
            const TermID id = toks_[k].id;
            if (id == TermID::LBRACE || isDoc(id) || isDocInline(id)) {
                return true; // вложенный блок/док — принудительно многострочно
            }
        }
        return false;
    }

    // Приблизительная ширина группы в одну строку.
    bool groupFitsInline(int o, int c) const {
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

    std::vector<Token> toks_;
    const FormatOptions& opts_;
    std::string_view sourceView_;
    std::vector<int> match_;
    std::vector<bool> multiline_;
    std::vector<int> openStack_;
    std::vector<bool> inTrust_;
    std::vector<bool> inParen_;
    std::string out_;
    int indent_ = 0;
    int contractIndent_ = 0;
    bool lineStart_ = true;
};

} // namespace

// ════════════════════════════════════════════════════════════════
//  Публичное API
// ════════════════════════════════════════════════════════════════

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

    // Классификация макросов ведётся целиком через события (events): базовые макросы (DSL и
    // загруженные до подписки) эмитируем как начальные add-события на позиции 0, а source/module
    // макросы приходят in-stream через on_macro_kind при прогоне парсера ниже. active — пустой
    // аккумулятор, наполняемый событиями на проходе по токенам (по позиции).
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

    // Прогоняем парсер: сырые токены (on_token) и классификация макросов (on_macro_kind)
    // собираются in-stream; комментарии форматтер восстановит из зазоров по позициям токенов.
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

    // Применяем timeline классификации к токенам (по позиции): макрос, определённый ранее и
    // ещё не удалённый, классифицируется даже если в итоговой таблице его уже нет (in-stream).
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
        const std::string name = std::string(trust::utils::strip_macro_sigil(t.text));
        const auto it = active.find(name);
        if (it != active.end()) {
            t.contract = hasKind(it->second, MacroKind::Contract);
            t.noParen = hasKind(it->second, MacroKind::NoParen) && name != "main";
        }
    }

    // Отрицательный литерал после операнда (`n-1`) — это бинарный минус в грамматике;
    // расщепляем, чтобы выводить `n - 1`, а не `n -1`.
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
