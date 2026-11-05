#pragma once
#include "opensim/framework/config_http_x509_resource_client.hpp"
#include <span>
namespace opensim::framework {
#ifdef __linux__
// RawData and the complete OpenSSL public-certificate representation are
// distinct: trusted PEM can carry auxiliary trust/reject OIDs and an alias.
struct ConfigHttpLinuxDownloadedCertificate {
 std::vector<std::uint8_t> der;
 std::vector<std::uint8_t> auxiliary_der;
};
std::optional<ConfigHttpLinuxDownloadedCertificate> config_http_linux_parse_downloaded_certificate(std::span<const std::uint8_t>);
std::optional<ConfigHttpLinuxDownloadedCertificate> config_http_linux_download_certificate(const ConfigHttpX509ResourceClient&,std::optional<std::u16string_view>,std::int64_t timeout_ticks);
#endif
}
