// Consumed runtime v8.0.31 Unix CertificateValidation / openssl.c name policy.
#pragma once
#include <cstdint>
#include <span>
#include <string_view>
namespace opensim::framework {
#ifdef __linux__
bool config_http_linux_certificate_name(std::span<const std::uint8_t>,std::u16string_view normalized_host);
#endif
}
