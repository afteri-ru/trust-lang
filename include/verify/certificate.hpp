#pragma once

// include/verify/certificate.hpp
// Сертификат доказательств `*.trustcert`: детерминированный JSON (байт-стабильный вывод).
// Поля: версия формата, хеш входа, решатель + версия, обязательства со статусами и причинами envelope.

#include "verify/model/obligation.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace trust {
namespace verify {

/// Версия формата сертификата (меняется при изменении состава полей).
inline constexpr std::string_view kCertificateFormat = "trustcert/1";

/// Запись сертификата: обязательство + итоговый статус.
struct CertificateEntry {
    Obligation obligation;
    ProofStatus status = ProofStatus::Unknown;
};

/// Собрать JSON сертификата. Порядок ключей и записей фиксирован ВХОДОМ (без сортировок по
/// контейнерам с нестабильным порядком), поэтому вывод побайтно воспроизводим.
[[nodiscard]] std::string composeCertificate(std::string_view subjectHash, std::string_view tool, std::string_view toolVersion,
                                             const std::vector<CertificateEntry>& entries);

} // namespace verify
} // namespace trust
