// Consumed Windows SslStream/X509Chain default server certificate policy.
#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>
namespace opensim::framework {
#ifdef _WIN32
struct ConfigHttpWindowsCertificatePolicy {
 // SslStream's consumed default is NoCheck with the server-auth application OID.
 // Revocation-enabled policies are a separate, unimplemented surface.
 bool check_name=true,disable_certificate_downloads=false;
 std::optional<std::uint64_t> verification_filetime;
 // null uses the current-user/system chain engine. A value uses an exclusive
 // in-memory custom root store; no machine or user stores are modified.
 std::optional<std::vector<std::vector<std::uint8_t>>> custom_roots;
};
// Returns SslPolicyErrors flags: missing certificate=1, name=2, chain=4.
// Intermediate DERs are the peer's ExtraStore certificates, not trusted roots.
std::int32_t config_http_windows_certificate_errors(std::span<const std::uint8_t> leaf,
 const std::vector<std::vector<std::uint8_t>>& intermediates,std::u16string_view normalized_host,
 const ConfigHttpWindowsCertificatePolicy& = {});
#endif
}
