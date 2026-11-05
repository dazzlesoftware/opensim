#pragma once
#include "opensim/framework/config_http_linux_certificate.hpp"
#include "opensim/framework/config_http_linux_certificate_download.hpp"
#include "opensim/framework/config_uri_object.hpp"
#include <chrono>
namespace opensim::framework {
#ifdef __linux__
std::optional<std::u16string> config_http_linux_find_http_aia_record(std::span<const std::uint8_t> extension,std::string_view record_oid,const ConfigUriObjectBackend&);
struct ConfigHttpLinuxAiaResult {
 int native_error;
 std::vector<CertificateDer> downloaded;
};
using ConfigHttpLinuxIssuerDownload=std::function<std::optional<CertificateDer>(std::u16string_view,std::chrono::milliseconds)>;
// Called only after FindFirstChain returned an incomplete path. The downloader
// supplies a parsed public certificate (the HTTP/content-type boundary is separate).
// No download becomes trusted. On OK, return only selected issuer instances for
// ChainPal's subsequent save-to-CA step, before policy/name processing.
ConfigHttpLinuxAiaResult config_http_linux_find_chain_via_aia(ConfigHttpLinuxCertificateChain&,const ConfigUriObjectBackend&,std::chrono::milliseconds,const ConfigHttpLinuxIssuerDownload&);
struct ConfigHttpLinuxAiaAssetResult {
 int native_error;
 std::vector<ConfigHttpLinuxDownloadedCertificate> downloaded;
};
using ConfigHttpLinuxIssuerAssetDownload=std::function<std::optional<ConfigHttpLinuxDownloadedCertificate>(std::u16string_view,std::chrono::milliseconds)>;
ConfigHttpLinuxAiaAssetResult config_http_linux_find_chain_via_assets(ConfigHttpLinuxCertificateChain&,const ConfigUriObjectBackend&,std::chrono::milliseconds,const ConfigHttpLinuxIssuerAssetDownload&);
ConfigHttpLinuxAiaAssetResult config_http_linux_find_chain_via_assets(ConfigHttpLinuxCertificateChain&,const ConfigUriObjectBackend&,std::chrono::milliseconds,const ConfigHttpX509ResourceClient&);
#endif
}
