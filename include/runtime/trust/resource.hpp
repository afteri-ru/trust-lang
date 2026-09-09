// trust/resource.hpp - встроенные deleter'ы для ВНЕШНИХ (не-память) ресурсов.
//
// Public runtime header: самодостаточен (только std-заголовки), поэтому сгенерированные
// C++-программы включают его без дерева include компилятора. При сборке встраивается в
// trust-runtime (через #embed, ELF-секция "trust/resource.hpp"); pipeline извлекает его
// во временный каталог trust/, когда программа реально использует deleter-тип.
//
// Deleter - функтор с `void operator()(V*)`, передаваемый как ВТОРОЙ шаблонный параметр
// `trust::Unique<V, D>` (D - часть типа) или как аргумент создания
// `trust::Shared<V>::adopt(ptr, D{})` (D стирается). Язык выражает это атрибутом
// `@[deleter(D)]` на владеющем объявлении (см. types/REFType.md §9.2).
//
// Здесь собраны типовые deleter'ы, пригодные для общих случаев; пользовательские
// deleter-функторы объявляются в TrustLang как нативные классы.

#ifndef TRUST_RESOURCE_HPP
#define TRUST_RESOURCE_HPP

#include <cstdio>
#include <cstdlib>

namespace trust {

/// Освобождение памяти, полученной через std::malloc/std::calloc/std::realloc.
/// Принимает `void*`, поэтому применим к любому объектному указателю-ресурсу
/// (`Unique<T, FreeDeleter>`).
struct FreeDeleter {
    void operator()(void* ptr) const noexcept { std::free(ptr); }
};

/// Закрытие файлового потока C (`std::FILE*`), полученного через fopen/... .
/// `operator()(std::FILE*)`: применим как `Unique<std::FILE, FileDeleter>`.
struct FileDeleter {
    void operator()(std::FILE* file) const noexcept {
        if (file != nullptr) {
            std::fclose(file);
        }
    }
};

} // namespace trust

#endif // TRUST_RESOURCE_HPP
