// Consumed .NET8 Unix X509 chain application/certificate policy processing.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
namespace opensim::framework {
struct CertificatePolicyData {
    using Bytes = std::optional<std::vector<std::uint8_t>>;
    Bytes application_cert_policies, cert_policies, cert_policy_mappings, cert_policy_constraints,
        enhanced_key_usage, inhibit_any_policy;
};
class CertificatePolicyError : public std::runtime_error {
  public:
    CertificatePolicyError() : std::runtime_error("ASN1 corrupted data.") {}
    const std::string source_type = "System.Security.Cryptography.CryptographicException";
    const std::uint32_t hresult = 0x80131501;
};
class CertificatePolicyChain {
    struct State;
    std::shared_ptr<const State> state_;

  public:
    // DER extension values in leaf-to-root chain order. Extraction, path building,
    // trust stores and revocation are separate native chain-processor operations.
    explicit CertificatePolicyChain(const std::vector<CertificatePolicyData>&);
    using Oid = std::optional<std::string>;
    bool matches_application_policy(const Oid&) const;
    bool matches_certificate_policy(const Oid&) const;
    bool matches_application_policies(const std::vector<Oid>&) const;
    bool matches_certificate_policies(const std::vector<Oid>&) const;
};
}
