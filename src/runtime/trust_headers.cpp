// runtime/trust_headers.cpp - embeds the public runtime headers
// ("trust/rational.hpp", "trust/assert.hpp", ...) into trust-runtime.so/.a.
//
// The header bytes are placed into an ELF section whose name equals the header
// path (e.g. "trust/rational.hpp"). The pipeline reads the section back from the
// library (as ELF / ar-archive member) and extracts it into a temporary `trust/`
// directory when a generated C++ program actually uses the corresponding type /
// symbol.
//
// `used`/`retain` keep the section alive through linker GC.

#include <string_view>

// The arrays are char so a trailing NUL makes it easy to treat as text.
// The header paths are relative to this source file (src/runtime/ → include/trust/):
// clang's `#embed` resolves quoted names against the including file's directory
// and does not search -I include paths.

__attribute__((used, retain, section("trust/rational.hpp"))) static const char kTrustRationalHeader[] = {
#embed "../../include/trust/rational.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/big_integer.hpp"))) static const char kTrustBigIntegerHeader[] = {
#embed "../../include/trust/big_integer.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/assert.hpp"))) static const char kTrustAssertHeader[] = {
#embed "../../include/trust/assert.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/checked_cast.hpp"))) static const char kTrustCheckedCastHeader[] = {
#embed "../../include/trust/checked_cast.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/io.hpp"))) static const char kTrustIoHeader[] = {
#embed "../../include/trust/io.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/dict.hpp"))) static const char kTrustDictHeader[] = {
#embed "../../include/trust/dict.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/args.hpp"))) static const char kTrustArgsHeader[] = {
#embed "../../include/trust/args.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/range.hpp"))) static const char kTrustRangeHeader[] = {
#embed "../../include/trust/range.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/enum.hpp"))) static const char kTrustEnumHeader[] = {
#embed "../../include/trust/enum.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/any_convert.hpp"))) static const char kTrustAnyConvertHeader[] = {
#embed "../../include/trust/any_convert.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/stack_check.hpp"))) static const char kTrustStackCheckHeader[] = {
#embed "../../include/trust/stack_check.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/interrupt.hpp"))) static const char kTrustInterruptHeader[] = {
#embed "../../include/trust/interrupt.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/trusted-cpp.hpp"))) static const char kTrustTrustedCppHeader[] = {
#embed "../../include/trust/trusted-cpp.hpp"
    , '\0'};

__attribute__((used, retain, section("trust/trusted-cpp-sync.hpp"))) static const char kTrustTrustedCppSyncHeader[] = {
#embed "../../include/trust/trusted-cpp-sync.hpp"
    , '\0'};

// Convenience views (length excludes the trailing NUL).
namespace trust::runtime {
inline constexpr std::string_view kRationalHeader(kTrustRationalHeader, sizeof(kTrustRationalHeader) - 1);
inline constexpr std::string_view kBigIntegerHeader(kTrustBigIntegerHeader, sizeof(kTrustBigIntegerHeader) - 1);
inline constexpr std::string_view kAssertHeader(kTrustAssertHeader, sizeof(kTrustAssertHeader) - 1);
inline constexpr std::string_view kCheckedCastHeader(kTrustCheckedCastHeader, sizeof(kTrustCheckedCastHeader) - 1);
inline constexpr std::string_view kIoHeader(kTrustIoHeader, sizeof(kTrustIoHeader) - 1);
inline constexpr std::string_view kDictHeader(kTrustDictHeader, sizeof(kTrustDictHeader) - 1);
inline constexpr std::string_view kArgsHeader(kTrustArgsHeader, sizeof(kTrustArgsHeader) - 1);
inline constexpr std::string_view kRangeHeader(kTrustRangeHeader, sizeof(kTrustRangeHeader) - 1);
inline constexpr std::string_view kEnumHeader(kTrustEnumHeader, sizeof(kTrustEnumHeader) - 1);
inline constexpr std::string_view kAnyConvertHeader(kTrustAnyConvertHeader, sizeof(kTrustAnyConvertHeader) - 1);
inline constexpr std::string_view kStackCheckHeader(kTrustStackCheckHeader, sizeof(kTrustStackCheckHeader) - 1);
inline constexpr std::string_view kInterruptHeader(kTrustInterruptHeader, sizeof(kTrustInterruptHeader) - 1);
inline constexpr std::string_view kTrustedCppHeader(kTrustTrustedCppHeader, sizeof(kTrustTrustedCppHeader) - 1);
inline constexpr std::string_view kTrustedCppSyncHeader(kTrustTrustedCppSyncHeader, sizeof(kTrustTrustedCppSyncHeader) - 1);
} // namespace trust::runtime
