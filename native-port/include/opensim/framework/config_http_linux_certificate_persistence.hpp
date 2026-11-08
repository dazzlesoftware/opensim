#pragma once
#include "opensim/framework/config_http_linux_aia.hpp"
#include <filesystem>
namespace opensim::framework {
#ifdef __linux__
// ChainPal saves only selected downloads from a native-OK path, before policy
// processing. Each add failure is swallowed; successful no-op adds flush caches.
void config_http_linux_persist_aia_issuers(const ConfigHttpLinuxAiaAssetResult&,
                                           const std::filesystem::path& user_store_root,
                                           const std::function<void()>& flush_user_stores);
void config_http_linux_persist_aia_issuers(const ConfigHttpLinuxAiaAssetResult&);
// NoCheck chain orchestration: default store snapshots, optional AIA, caching
// before Finish's policy/name processing. URL retrieval timeout uses TimeSpan
// ticks, including zero/default and the source one-minute clamp.
ConfigHttpLinuxCertificateResult config_http_linux_certificate_errors_with_downloads(
    std::span<const std::uint8_t> leaf, const std::vector<CertificateDer>& peer_extra,
    std::u16string_view host, ConfigHttpLinuxCertificatePolicy, const ConfigHttpX509ResourceClient&,
    std::int64_t url_retrieval_timeout_ticks = 0, bool disable_aia = false);
#endif
} // namespace opensim::framework
