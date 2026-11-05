// Native OpenSSL3 Unix chain-processing core consumed by SslStream.
#pragma once
#include "opensim/framework/certificate_policy_chain.hpp"
#include <memory>
#include <span>
#include <string_view>
namespace opensim::framework {
#ifdef __linux__
using CertificateDer = std::vector<std::uint8_t>;
struct ConfigHttpLinuxCertificateStores {
    std::vector<CertificateDer> trusted, user_intermediates, system_intermediates;
};
struct ConfigHttpLinuxCertificatePolicy {
    std::optional<std::vector<CertificateDer>> custom_roots;
    std::optional<std::int64_t> verification_unix_seconds;
    std::vector<CertificatePolicyChain::Oid> application_policies{std::string("1.3.6.1.5.5.7.3.1")},
        certificate_policies;
    bool check_name = true;
};
struct ConfigHttpLinuxChainElement {
    CertificateDer certificate;
    std::vector<std::uint32_t> status;
};
struct ConfigHttpLinuxCertificateResult {
    std::int32_t errors = 0;
    std::vector<std::uint32_t> status;
    std::vector<ConfigHttpLinuxChainElement> elements;
};
class ConfigHttpLinuxCertificateError : public std::runtime_error {
public:
    ConfigHttpLinuxCertificateError()
        : std::runtime_error("Error occurred during a cryptographic operation.") {}
    const std::string source_type = "System.Security.Cryptography.CryptographicException";
    const std::uint32_t hresult = 0x80131501;
};
class ConfigHttpLinuxCertificateChain {
    struct State;
    std::unique_ptr<State> state_;

public:
    // Valid peer certificate DER, explicit snapshots of the native stores, and
    // NoCheck revocation. Store loading/caching and AIA/revocation are external.
    ConfigHttpLinuxCertificateChain(std::span<const std::uint8_t>, const ConfigHttpLinuxCertificateStores&,
                                    ConfigHttpLinuxCertificatePolicy = {});
    ~ConfigHttpLinuxCertificateChain();
    int find_first(const std::vector<CertificateDer>& peer_extra);
    // Exposes the missing issuer's child for the source AIA continuation.
    std::optional<CertificateDer> missing_issuer_child() const;
    // Opaque identity for the source AIA repeat guard, never dereferenced by callers.
    std::uintptr_t missing_issuer_identity() const;
    int add_issuer(std::span<const std::uint8_t>);
    // Import a downloaded certificate's complete i2d_X509_AUX representation.
    // It remains untrusted; auxiliary metadata is not discarded on insertion.
    int add_issuer_aux(std::span<const std::uint8_t>);
    // Indices of add_issuer inputs retained by the selected chain, by native handle
    // identity (DER equality would incorrectly retain unused duplicate instances).
    std::vector<std::size_t> selected_added_issuers() const;
    std::size_t issuer_addition_count() const;
    ConfigHttpLinuxCertificateResult finish(std::u16string_view normalized_host);
};
ConfigHttpLinuxCertificateResult config_http_linux_certificate_errors(
    std::span<const std::uint8_t>, const std::vector<CertificateDer>& peer_extra,
    const ConfigHttpLinuxCertificateStores&, std::u16string_view normalized_host,
    ConfigHttpLinuxCertificatePolicy = {});
#endif
}
