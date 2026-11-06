// Seed-регистрация встроенных типов и методов абстрактных контейнеров.
// Порядок регистрации — часть контракта: он задаёт registry_index встроенных типов.
#include "types/registry/builtin_core_builder.hpp"

#include "types/registry/builtin_seed_zones.hpp"

#include "types/group.hpp"
#include "types/names/type_names.hpp"
#include "types/registry/builtin_registry.hpp"
#include "types/registry/method_table.hpp"
#include "types/registry/named_type_registrar.hpp"
#include "types/registry/structural_interner.hpp"
#include "types/registry/type_store.hpp"
#include "types/type_read.hpp"
#include "utils/error.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace trust {

void BuiltinCoreBuilder::registerBuiltinTypes(BuiltinSeedZones& z) {
    // Concrete builtin types (Data ≠ 0 → TypeClass::kTrivial)
    registerBuiltinType(z, type::Void, Group::kVoid, 1);
    registerBuiltinType(z, type::None, Group::kVoid, 2);
    auto boolId = registerBuiltinType(z, type::Bool, Group::kLogical, 1, "bool");
    auto int8Id = registerBuiltinType(z, type::Int8, Group::kIntegers, 8, "int8_t");
    registerBuiltinType(z, type::Int16, Group::kIntegers, 16, "int16_t");
    registerBuiltinType(z, type::Int32, Group::kIntegers, 32, "int32_t");
    registerBuiltinType(z, type::Int64, Group::kIntegers, 64, "int64_t");
    auto uint8Id = registerBuiltinType(z, type::UInt8, Group::kUnsigned, 8, "uint8_t");
    auto uint16Id = registerBuiltinType(z, type::UInt16, Group::kUnsigned, 16, "uint16_t");
    auto uint32Id = registerBuiltinType(z, type::UInt32, Group::kUnsigned, 32, "uint32_t");
    auto uint64Id = registerBuiltinType(z, type::UInt64, Group::kUnsigned, 64, "uint64_t");
    registerBuiltinType(z, type::Float16, Group::kNumbers, 16);
    auto float32Id = registerBuiltinType(z, type::Float32, Group::kNumbers, 32, "float");
    auto float64Id = registerBuiltinType(z, type::Float64, Group::kNumbers, 64, "double");
    registerBuiltinType(z, type::BFloat16, Group::kBFloat, 16);
    // Complex32/64: ПРЕДОБЪЯВЛЕНИЕ (интерфейс) БЕЗ реализации и БЕЗ C++-имени. C++-имя и include
    // приходят из stdlib/builtin/complex.src (`Complex32 = %std::complex<float> { };`).
    registerBuiltinType(z, type::Complex32, Group::kComplex, 32);
    registerBuiltinType(z, type::Complex64, Group::kComplex, 64);
    // StrChar/StrWide: ПРЕДОБЪЯВЛЕНИЕ (интерфейс) БЕЗ реализации и БЕЗ C++-имени. C++-имя и КЛАСС —
    // деталь РЕАЛИЗАЦИИ из stdlib/builtin/string.src (`StrChar = %trust::StrChar, %std::string { ... }` — базис в синтаксисе:
    // класс ГЕНЕРИРУЕТСЯ из .src); довязка методов — там же (`%native` / `:= other`). Здесь — только
    // чистый Trust-интерфейс.
    auto strCharId = registerBuiltinType(z, type::StrChar, Group::kStrChar, 1);
    registerBuiltinType(z, type::StrWide, Group::kStrWide, 1);
    // CString - невладеющий указатель на C-строку (const char*): результат метода
    // StrChar.c_str(), совместим с нативными C-функциями (printf %s). Интринсик (реализация в рантайме).
    auto cstringId = registerBuiltinType(z, type::CString, Group::kStrChar, 2, "const char*");
    // Интерфейсные методы StrChar (операции над C++ std::string): ЧИСТЫЙ Trust-интерфейс — БЕЗ '%' и
    // БЕЗ нативного имени. Конкретные C++-имена (c_str/size/...) даёт реализация string.src. Сигнатура
    // метода - функциональный тип (метод и функция - одно и то же).
    z.methods.addMethod(strCharId, "c_str", z.interner.getOrCreateFunctionType(cstringId, {}));
    z.methods.addMethod(strCharId, "data", z.interner.getOrCreateFunctionType(cstringId, {}));
    z.methods.addMethod(strCharId, "size", z.interner.getOrCreateFunctionType(uint64Id, {}));
    z.methods.addMethod(strCharId, "length", z.interner.getOrCreateFunctionType(uint64Id, {}));
    z.methods.addMethod(strCharId, "empty", z.interner.getOrCreateFunctionType(boolId, {}));
    // Rational/BigInteger: ПРЕДОБЪЯВЛЕНИЕ (интерфейс) БЕЗ реализации и БЕЗ C++-имени. C++-имя,
    // include и реализация display приходят из stdlib/builtin/rational.src (как StrChar/string.src):
    //   Rational = %trust::Rational { display^(len: Int64): StrChar := %display ...; };
    auto rationalId = registerBuiltinType(z, type::Rational, Group::kArbitraryPrecision, 1);
    // BigInteger: arbitrary-precision integer; Rational is built on it (numerator/denominator).
    // Живёт в той же группе kArbitraryPrecision (Data: Rational=1, BigInteger=2).
    auto bigIntegerId = registerBuiltinType(z, type::BigInteger, Group::kArbitraryPrecision, 2);
    // Метод показа BigInteger: `x.display(L)` → StrChar, голова = хвост = L цифр (только для
    // вывода, не round-trip). Const-метод (как Dict.%size^); параметр L - Int64.
    // Чистый Trust-интерфейс: C++-имя члена и привязка приходят из rational.src.
    z.methods.addMethod(bigIntegerId, "display^", z.interner.getOrCreateFunctionType(strCharId, {z.read.getType(type::Int64)}));
    // Метод показа Rational: `x.display(L)` → StrChar; каждый из num\den сокращается как BigInteger.
    z.methods.addMethod(rationalId, "display^", z.interner.getOrCreateFunctionType(strCharId, {z.read.getType(type::Int64)}));
    // Dict: ПРЕДОБЪЯВЛЕНИЕ (интерфейс) БЕЗ реализации и БЕЗ C++-имени. C++-имя/заголовок
    // и реализация size приходят из stdlib/builtin/dict.src (как StrChar/string.src).
    // Dictionary - каноническое базовое имя (алиас).
    auto dictId = registerBuiltinType(z, type::Dict, Group::kDicts, 1);
    z.registrar.registerBuiltinAlias(type::Dictionary, dictId);

    // Политики доступа/владения: ПРЕДОБЪЯВЛЕНИЕ (интерфейс) БЕЗ реализации, cppName и include
    // приходят из stdlib/core/mem.src (`AccessMutex = %trust::AccessMutex { };` и т.п.).
    // Признак HasAttrsFlag (markAttrs) - «тип несёт атрибуты» (политика `sync`) - задаётся здесь,
    // он ортогонален cppName/include. Имя в реестре = РЕАЛЬНОЕ имя класса (без алиасов):
    // 2-й аргумент reftype (`@[reftype("shared", AccessMutex)@]`) резолвится через findType.
    registerBuiltinType(z, type::AccessMutex, Group::kAccessPolicy, 1, {}, /*markAttrs=*/true);
    registerBuiltinType(z, type::AccessRwMutex, Group::kAccessPolicy, 2, {}, /*markAttrs=*/true);
    registerBuiltinType(z, type::AccessSingleThread, Group::kAccessPolicy, 3, {}, /*markAttrs=*/true);
    // Обёртка синхронизированной сильной ссылки (НЕ политика: Data=4 вне диапазона политик 1..3).
    registerBuiltinType(z, type::AccessShared, Group::kAccessPolicy, 4);
    // Обёртка статического эксклюзивного владения `unique` (Data=7 - НЕ политика).
    registerBuiltinType(z, type::StaticUnique, Group::kAccessPolicy, 7);

    // Deleter-типы ресурсов: ПРЕДОБЪЯВЛЕНИЕ БЕЗ реализации, cppName/include - из mem.src.
    // Обычный тип-функтор, используемый как аргумент `@[deleter(FreeDeleter)]` (резолвится через
    // findType; распознавание - isDeleterPolicyType).
    registerBuiltinType(z, type::FreeDeleter, Group::kDeleterPolicy, 1);
    registerBuiltinType(z, type::FileDeleter, Group::kDeleterPolicy, 2);

    // Runtime symbols (целевые факты: C++-символ + заголовки trust-runtime) вынесены в компонент
    // `target` (`target/runtime_symbols.hpp`, единый X-macro источник); ядро типов их не хранит (R37).

    // Ellipsis types
    registerBuiltinType(z, type_category::EllipsisAny, Group::kEllipsis, 1);
    registerBuiltinType(z, type_category::EllipsisTyped, Group::kEllipsis, 2);

    // Aliases - register as separate types based on existing concrete types
    z.registrar.registerBuiltinAlias(type::Char, int8Id);
    z.registrar.registerBuiltinAlias(type::Byte, uint8Id);
    z.registrar.registerBuiltinAlias(type::Word, uint16Id);
    z.registrar.registerBuiltinAlias(type::DWord, uint32Id);
    z.registrar.registerBuiltinAlias(type::DDWord, uint64Id);
    z.registrar.registerBuiltinAlias(type::Single, float32Id);
    z.registrar.registerBuiltinAlias(type::Double, float64Id);

    // String alias for StrChar
    z.registrar.registerBuiltinAlias("String", strCharId);

    // Integer alias for Int64 (generic integer → largest supported)
    auto int64Id = z.read.getType(type::Int64);
    z.registrar.registerBuiltinAlias("Integer", int64Id);

    // Abstract group types (Data = 0 → TypeClass::kComplex)
    // Auto: ВНУТРЕННИЙ sentinel «тип не выведен» (НЕ пользовательский тип; до кодогена не доходит).
    registerBuiltinType(z, type_generic::Auto, Group::kAuto, 0);
    // Any (`std::any`): ПРЕДОБЪЯВЛЕНИЕ БЕЗ реализации/имени — cppName/include из builtin/any.src.
    // Верхний тип (принимает любой) и материализуемый RTTI-тип: грузится ЯВНО (`\std();`/`\any();`).
    registerBuiltinType(z, type_generic::Any, Group::kAny, 0);
    registerBuiltinType(z, type_category::Struct, Group::kStructured);
    registerBuiltinType(z, type_category::Enum, Group::kStructured);
    registerBuiltinType(z, type_category::Variant, Group::kStructured);
    registerBuiltinType(z, type_category::Tuple, Group::kStructured);
    registerBuiltinType(z, type_category::Optional, Group::kStructured);
    registerBuiltinType(z, type_category::Expected, Group::kStructured);
    registerBuiltinType(z, type_category::Function, Group::kCallable);
    registerBuiltinType(z, type_category::Closure, Group::kCallable);
    registerBuiltinType(z, type_category::Coroutine, Group::kCallable);
    registerBuiltinType(z, type_category::Generator, Group::kCallable);
    registerBuiltinType(z, type_category::Method, Group::kCallable);
    registerBuiltinType(z, type_category::Delegate, Group::kCallable);
    registerBuiltinType(z, type_category::Class, Group::kClasses);
    registerBuiltinType(z, type_category::Interface, Group::kClasses);
    registerBuiltinType(z, type_category::Range, Group::kRanges);
    registerBuiltinType(z, type_category::Slice, Group::kRanges);
    registerBuiltinType(z, type_category::View, Group::kRanges);
    registerBuiltinType(z, type_category::Forward, Group::kIterators);
    registerBuiltinType(z, type_category::Bidirectional, Group::kIterators);
    registerBuiltinType(z, type_category::RandomAccess, Group::kIterators);
    registerBuiltinType(z, type_category::Duration, Group::kDateTime);
    registerBuiltinType(z, type_category::TimePoint, Group::kDateTime);
    registerBuiltinType(z, type_category::Future, Group::kAsync);
    registerBuiltinType(z, type_category::Awaitable, Group::kAsync);
    registerBuiltinType(z, type_category::Thread, Group::kSync);
    registerBuiltinType(z, type_category::Mutex, Group::kSync);
    registerBuiltinType(z, type_category::Condition, Group::kSync);
    registerBuiltinType(z, type_category::Exception, Group::kExceptions);
    registerBuiltinType(z, type_category::Error, Group::kExceptions);
    // Interruptions: IntAny - база, IntPlus - возврат, IntMinus - ошибка. ПРЕДОБЪЯВЛЕНИЕ БЕЗ
    // реализации: cppName/include приходят из stdlib/core/exceptions.src (заголовок interrupts.hpp).
    registerBuiltinType(z, type::IntAny, Group::kExceptions, 0);
    registerBuiltinType(z, type::IntPlus, Group::kExceptions, 0);
    registerBuiltinType(z, type::IntMinus, Group::kExceptions, 0);
    // Базовый C++-класс исключений: catch(:StdException) → `catch (std::exception&)`.
    // Обобщённый catch - ловит любое std::exception-производное (ошибки конверсии и т.п.),
    // а не только прерывания trust. Инклуд <stdexcept> обязателен (std::exception).
    registerBuiltinType(z, type::StdException, Group::kExceptions, 0);
    registerBuiltinType(z, type_category::Arithmetics, Group::kArithmetics);
    registerBuiltinType(z, type_category::Native, Group::kNative);

    // Array: универсальный изменяемый массив (cpp-имя std::vector<Elem>). Структурные
    // `Array<Elem>` создаются getOrCreateArrayType (Group::kContainers, Data=1). Здесь -
    // абстрактный `:Array` (Data=0), на котором объявлены методы (как у `:Range`).
    registerBuiltinType(z, type::Array, Group::kContainers);

    // Template parameter type
    registerBuiltinType(z, type_category::TemplateParam, Group::kTemplateParam);

    // Generalized types (Data = 0)
    registerBuiltinType(z, type_generic::Integers, Group::kIntegers);
    registerBuiltinType(z, type_generic::Numbers, Group::kNumbers);
    registerBuiltinType(z, type_generic::Strings, Group::kStrChar);
    registerBuiltinType(z, type_generic::Tensors, Group::kTensors);

    // Tensor: Core-тип с провайдером. Абстрактные `:Tensor`/`:SparseTensor` (Data=0) несут
    // методы; конкретные `Tensor<Elem, shape>` создаются getOrCreateTensorType
    // (Group::kTensors, Data=1 dense / 2 sparse). `DenseTensor` - алиас `:Tensor`.
    // C++-имя/include/link приходят только из провайдера tensor.trust (`Tensor = %trust::Tensor`).
    auto tensorId = registerBuiltinType(z, type::Tensor, Group::kTensors, 0);
    z.registrar.registerBuiltinAlias(type::DenseTensor, tensorId);
    registerBuiltinType(z, type::SparseTensor, Group::kTensors, 0);

    registerBuiltinContainerMethods(z);
}

} // namespace trust
