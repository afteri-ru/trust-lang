// Пер-компонентная регистрация флагов `verify` (см. diag/registry.hpp) + разбор режима гейта.
#include "verify/verify.hpp"

#include "diag/registry.hpp"
#include "verify/model/diag.hpp"

namespace trust {
namespace verify {

namespace {
struct VerifyFlagsRegistrar {
    VerifyFlagsRegistrar() {
        registerDiagnostics([](Options& opts) {
            VERIFY_FLAG_LIST(VERIFY_FL_ADD_OPT)
            // Допустимое значение `--verify`: только "all". Неизвестное значение — ошибка
            // применения опции (тихого fallback нет).
            opts.set_flag_validator(verify::FlagKind::Verify, [](std::string_view v) { return parseVerifyMode(v).has_value(); });
        });
    }
};
[[maybe_unused]] const VerifyFlagsRegistrar g_verify_flags_registrar;
} // namespace

std::optional<VerifyMode> parseVerifyMode(std::string_view value) noexcept {
    if (value == "all") {
        return VerifyMode::kAll;
    }
    return std::nullopt;
}

std::optional<VerifyMode> verifyModeFromOptions(const Options& opts) noexcept {
    if (const auto value = opts.flag_value(verify::FlagKind::Verify)) {
        return parseVerifyMode(*value);
    }
    return std::nullopt;
}

} // namespace verify
} // namespace trust
