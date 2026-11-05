#include "opensim/framework/config_http_windows_certificate.hpp"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <wincrypt.h>
#include <system_error>
#include <string>
namespace opensim::framework {
namespace {
[[noreturn]] void fail(const char* api) {
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), api);
}
struct Store {
    HCERTSTORE value = nullptr;
    Store() {
        value = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, 0, nullptr);
        if (!value)
            fail("CertOpenStore");
    }
    ~Store() {
        CertCloseStore(value, 0);
    }
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    void add(const std::vector<std::uint8_t>& der) {
        if (der.size() > MAXDWORD)
            throw std::length_error("Certificate DER exceeds DWORD");
        if (!CertAddEncodedCertificateToStore(value, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, der.data(),
                                              static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS, nullptr))
            fail("CertAddEncodedCertificateToStore");
    }
};
struct Engine {
    HCERTCHAINENGINE value = nullptr;
    bool owned = false;
    ~Engine() {
        if (owned)
            CertFreeCertificateChainEngine(value);
    }
};
struct Certificate {
    PCCERT_CONTEXT value;
    ~Certificate() {
        CertFreeCertificateContext(value);
    }
};
struct Chain {
    PCCERT_CHAIN_CONTEXT value = nullptr;
    ~Chain() {
        if (value)
            CertFreeCertificateChain(value);
    }
};
}
std::int32_t config_http_windows_certificate_errors(
    std::span<const std::uint8_t> leaf, const std::vector<std::vector<std::uint8_t>>& intermediates,
    std::u16string_view host, const ConfigHttpWindowsCertificatePolicy& options) {
    if (leaf.empty())
        return 1;
    if (leaf.size() > MAXDWORD)
        throw std::length_error("Certificate DER exceeds DWORD");
    Certificate cert{CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, leaf.data(),
                                                  static_cast<DWORD>(leaf.size()))};
    if (!cert.value)
        fail("CertCreateCertificateContext");
    Store extra, roots;
    for (const auto& der : intermediates)
        extra.add(der);
    Engine engine;
    if (options.custom_roots) {
        for (const auto& der : *options.custom_roots)
            roots.add(der);
        CERT_CHAIN_ENGINE_CONFIG config{};
        config.cbSize = sizeof(config);
        config.hExclusiveRoot = roots.value;
        if (!CertCreateCertificateChainEngine(&config, &engine.value))
            fail("CertCreateCertificateChainEngine");
        engine.owned = true;
    }
    CERT_CHAIN_PARA parameters{};
    parameters.cbSize = sizeof(parameters);
    char server_auth[] = szOID_PKIX_KP_SERVER_AUTH;
    char* usages[] = {server_auth};
    parameters.RequestedUsage.dwType = USAGE_MATCH_TYPE_AND;
    parameters.RequestedUsage.Usage.cUsageIdentifier = 1;
    parameters.RequestedUsage.Usage.rgpszUsageIdentifier = usages;
    FILETIME time{};
    if (options.verification_filetime) {
        time.dwLowDateTime = static_cast<DWORD>(*options.verification_filetime);
        time.dwHighDateTime = static_cast<DWORD>(*options.verification_filetime >> 32);
    } else
        GetSystemTimeAsFileTime(&time);
    DWORD flags = options.disable_certificate_downloads
                      ? (CERT_CHAIN_DISABLE_AUTH_ROOT_AUTO_UPDATE | CERT_CHAIN_DISABLE_AIA)
                      : 0;
    Chain chain;
    if (!CertGetCertificateChain(engine.value, cert.value, &time,
                                 intermediates.empty() ? nullptr : extra.value, &parameters, flags, nullptr,
                                 &chain.value) ||
        !chain.value)
        fail("CertGetCertificateChain");
    CERT_CHAIN_POLICY_PARA policy{};
    policy.cbSize = sizeof(policy);
    CERT_CHAIN_POLICY_STATUS status{};
    status.cbSize = sizeof(status);
    if (!CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_BASE, chain.value, &policy, &status))
        fail("CertVerifyCertificateChainPolicy(BASE)");
    std::int32_t errors = status.dwError ? 4 : 0;
    if (options.check_name) {
        std::u16string name(host);
        SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
        ssl.cbSize = sizeof(ssl);
        ssl.dwAuthType = AUTHTYPE_SERVER;
        ssl.pwszServerName = reinterpret_cast<wchar_t*>(name.data());
        policy.dwFlags = 4031;
        policy.pvExtraPolicyPara = &ssl;
        status = {};
        status.cbSize = sizeof(status);
        CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain.value, &policy, &status);
        if (status.dwError == static_cast<DWORD>(CERT_E_CN_NO_MATCH))
            errors |= 2;
    }
    return errors;
}
}
#endif
