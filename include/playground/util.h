#pragma once
// include/playground/util.h
// trust-playground: общие свободные утилиты, используемые несколькими компонентами
// (балансировщик, сервисы, воркер). Выделены в отдельный модуль, чтобы не дублировать
// их в нескольких исходниках и держать зоны ответственности изолированными.

#include <string>

namespace trust {
namespace playground {

// Криптографически случайные байты из /dev/urandom. При ошибке чтения возвращает
// строку из нулевых байт (вызывающий обрабатывает как случайные данные).
std::string randomBytes(size_t n);

// Текущее время UTC в ISO8601-виде "%Y-%m-%dT%H:%M:%SZ".
std::string utcNowString();

} // namespace playground
} // namespace trust
