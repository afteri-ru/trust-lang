#pragma once
// C++-поддержка ГЕНЕРИРУЕМЫХ классов строк — рантайм-примитивы (не реализация trust-типа).
// Один заголовок собирает всё, что нужно объявлению/телу генерируемого StrChar/StrWide:
// базовый контейнер (<string>), UTF-8-конверсии и std::formatter-специализацию.

#include <string>

#include "trust/format_support.hpp"
#include "trust/utf8.hpp"