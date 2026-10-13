#pragma once

// include/formatter/impl/format_model.hpp
// Модель токена и зазора форматтера и классификация токенов/зазоров. Внутренний заголовок
// зоны форматтера: публичный путь — formatter/format.hpp; включается только собственной
// реализацией (token_gap.cpp/emitter.cpp/format.cpp).

#include "syntax/term/term_types.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace trust::formatter {

bool isOpen(TermID id);
bool isClose(TermID id);
/// Бинарный оператор / присваивание — пробел с обеих сторон.
bool isBinaryOperator(TermID id);
/// Унарный оператор-префикс — без пробела после (прикреплён к операнду).
bool isUnaryOperator(TermID id);
bool isDoc(TermID id);
bool isDocInline(TermID id);
/// Операнд-подобные токены (имя/литерал/закрывающая скобка) — для решения про ':'-аннотацию.
bool isNameLike(TermID id);
/// Числовые литералы, способные нести ведущий '-' в тексте токена.
bool isNumericLiteral(TermID id);

/// Токен с 0-based смещениями и макро-классификацией, актуальной на его позицию.
struct Token {
    TermID id;
    size_t begin; // 0-based byte offset
    size_t end;
    std::string text;
    bool contract = false; // макрос-контракт (trust_* / source/module контракт)
    bool noParen = false;  // no-paren макрос (return/break/...) — пробел после имени
};

/// Расщепляет отрицательный литерал сразу после операнда на бинарный MINUS + литерал без '-'
/// (зеркалит грамматику `addition digits`); в позиции не-операнда литерал сохраняется.
void splitNegativeLiterals(std::vector<Token>& toks);

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

/// Разбор зазора: любой не-пробел/не-комментарий символ делает весь зазор verbatim.
Gap parseGap(std::string_view s);

} // namespace trust::formatter