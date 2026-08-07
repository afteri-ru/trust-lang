// format_check.cpp — парсер printf-формата и сверка типов аргументов.

#include "semantic/format_check.hpp"
#include "types/group.hpp"
#include "types/registry.hpp"
#include "types/type_names.hpp"

#include <cctype>
#include <string_view>
#include <vector>

namespace trust {
namespace format_check {

bool parse_printf_format(std::string_view fmt, std::vector<Conversion>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < fmt.size()) {
        if (fmt[i] != '%') {
            ++i;
            continue;
        }
        if (i + 1 >= fmt.size()) {
            return false; // незакрытый '%' в конце
        }
        ++i; // '%'
        if (fmt[i] == '%') {
            ++i; // %% — литерал, аргумент не потребляет
            continue;
        }
        // Флаги: -+ #0
        while (i < fmt.size() && (fmt[i] == '-' || fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '#' || fmt[i] == '0')) {
            ++i;
        }
        // Ширина: '*' или десятичные цифры.
        if (i < fmt.size() && fmt[i] == '*') {
            ++i;
        } else {
            while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
                ++i;
            }
        }
        // Точность: '.' + ('*' или цифры).
        if (i < fmt.size() && fmt[i] == '.') {
            ++i;
            if (i < fmt.size() && fmt[i] == '*') {
                ++i;
            } else {
                while (i < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[i]))) {
                    ++i;
                }
            }
        }
        // Length-модификаторы: hh h l ll L z j t.
        if (i < fmt.size() && (fmt[i] == 'l' || fmt[i] == 'h')) {
            ++i;
            if (i < fmt.size() && fmt[i] == (fmt[i - 1] == 'l' ? 'l' : 'h')) {
                ++i;
            }
        } else if (i < fmt.size() && (fmt[i] == 'L' || fmt[i] == 'z' || fmt[i] == 'j' || fmt[i] == 't')) {
            ++i;
        }
        if (i >= fmt.size()) {
            return false; // после модификаторов нет конверсионного символа
        }
        const char conv = fmt[i];
        ++i;
        Expect expect;
        switch (conv) {
        case 'd':
        case 'i':
        case 'c':
            expect = Expect::Integer;
            break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
            expect = Expect::Unsigned;
            break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'g':
        case 'G':
        case 'a':
        case 'A':
            expect = Expect::Float;
            break;
        case 's':
            expect = Expect::StrChar;
            break;
        case 'p':
        case 'n':
            expect = Expect::Pointer;
            break;
        default:
            return false; // неизвестный конверсионный символ
        }
        out.push_back({expect, conv});
    }
    return true;
}

bool arg_matches_expect(const TypeRegistry& reg, TypeId argType, Expect expect) {
    if (argType == INVALID_TYPE_ID) {
        return true; // выражение без выведенного типа — не проверяем
    }
    const TypeId c = reg.getCanonicalTypeId(argType);
    if (c == INVALID_TYPE_ID) {
        return true;
    }
    const TypeKind k = getKindFromId(c);
    const Group g = getGroup(k);
    const bool isValue = (getRefType(k) == RefType::kValue);
    switch (expect) {
    case Expect::Integer:
        // Любое целое по значению (числовые группы). %c — целый аргумент.
        return isValue && (g == Group::kIntegers || g == Group::kUnsigned);
    case Expect::Unsigned:
        return isValue && g == Group::kUnsigned;
    case Expect::Float:
        return isValue && (g == Group::kNumbers || g == Group::kBFloat);
    case Expect::StrChar:
        // %s — C-строка `const char*` (= встроенный тип CString). StrChar (std::string)
        // напрямую в printf %s передать нельзя — нужен .c_str() (→ CString) или строковый
        // литерал (уже const char*); литерал обрабатывается отдельно в checkFormatArgs.
        return c == reg.getType(type::CString);
    case Expect::Pointer:
        // %p: ссылочный/указательный тип либо динамический (Any).
        return g == Group::kReftype || !isValue || c == reg.getType(type_generic::Any);
    }
    return false;
}

} // namespace format_check
} // namespace trust
