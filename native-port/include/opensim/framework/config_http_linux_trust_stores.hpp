#pragma once
#include "opensim/framework/config_http_linux_system_store.hpp"
namespace opensim::framework {
#ifdef __linux__
class ConfigHttpLinuxStoreError : public std::runtime_error {
  public:
    std::string source_type;
    std::u16string message;
    std::uint32_t hresult;
    std::shared_ptr<const ConfigHttpLinuxStoreError> inner;
    ConfigHttpLinuxStoreError(std::string type, std::u16string text, std::uint32_t code,
                              std::shared_ptr<const ConfigHttpLinuxStoreError> cause = {});
};
// HOME is read afresh; the feature root, like PersistedFiles, is captured once.
std::string config_http_linux_user_home_directory();
const std::filesystem::path& config_http_linux_user_store_root();
void config_http_linux_check_disallowed_store(const std::filesystem::path& root);
class ConfigHttpLinuxTrustStores {
    struct State;
    std::unique_ptr<State> state_;
    ConfigHttpLinuxCertificateStores collection_unchecked(bool custom_root_trust);
    friend ConfigHttpLinuxCertificateStores config_http_linux_default_certificate_stores(bool);

  public:
    explicit ConfigHttpLinuxTrustStores(
        std::filesystem::path user_root,
        ConfigHttpLinuxUserStore::Clock clock = std::chrono::steady_clock::now);
    ~ConfigHttpLinuxTrustStores();
    ConfigHttpLinuxCertificateStores collection(bool custom_root_trust = false);
    void refresh_user_stores();
};
ConfigHttpLinuxCertificateStores config_http_linux_default_certificate_stores(bool custom_root_trust = false);
void config_http_linux_flush_user_stores();
// NoCheck chain policy with native cached stores and downloads disabled.
// AIA orchestration remains separate; this is not the default live TLS hook.
ConfigHttpLinuxCertificateResult config_http_linux_cached_certificate_errors(
    std::span<const std::uint8_t> leaf, const std::vector<CertificateDer>& peer_extra,
    std::u16string_view host, ConfigHttpLinuxCertificatePolicy policy = {});
#endif
}
