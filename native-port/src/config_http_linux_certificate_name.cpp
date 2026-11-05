// Adapted from runtime v8.0.31 src/native/libs/System.Security.Cryptography.Native/openssl.c
// CryptoNative_CheckX509Hostname / CryptoNative_CheckX509IpAddress, MIT .NET Foundation.
// License: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_certificate_name.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/ip_address.hpp"
#include "opensim/framework/text.hpp"
#include <openssl/x509v3.h>
#include <openssl/err.h>
#include <memory>
#include <cstring>
#include <strings.h>
#include <climits>
namespace opensim::framework {
bool config_http_linux_certificate_name(std::span<const std::uint8_t> der, std::u16string_view host) {
    if (der.empty() || host.empty())
        return false;
    if (der.size() > LONG_MAX)
        throw std::length_error("Certificate DER exceeds native length");
    auto cursor = der.data();
    std::unique_ptr<X509, decltype(&X509_free)> cert(
        d2i_X509(nullptr, &cursor, static_cast<long>(der.size())), X509_free);
    if (!cert)
        throw std::runtime_error("Certificate DER decoding failed");
    auto name = utf8(host);
    ERR_clear_error();
    if (auto ip = IpAddress::parse(name)) {
        auto address = ip->bytes();
        auto size = ip->v6() ? 16 : 4;
        std::unique_ptr<GENERAL_NAMES, decltype(&GENERAL_NAMES_free)> san(
            static_cast<GENERAL_NAMES*>(X509_get_ext_d2i(cert.get(), NID_subject_alt_name, nullptr, nullptr)),
            GENERAL_NAMES_free);
        if (san)
            for (int i = 0; i < sk_GENERAL_NAME_num(san.get()); ++i) {
                auto entry = sk_GENERAL_NAME_value(san.get(), i);
                if (entry->type != GEN_IPADD)
                    continue;
                auto value = entry->d.iPAddress;
                if (value && value->data && value->length == size &&
                    !std::memcmp(address.data(), value->data, size))
                    return true;
            }
        // Source checks every CN after a nonmatching/missing IP SAN, even when other
        // SAN entries exist. Compare against the original normalized textual form.
        if (auto subject = X509_get_subject_name(cert.get())) {
            int at = -1;
            while ((at = X509_NAME_get_index_by_NID(subject, NID_commonName, at)) >= 0) {
                auto cn = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(subject, at));
                if (cn && cn->length == static_cast<int>(host.size()) &&
                    !strncasecmp(reinterpret_cast<const char*>(cn->data), name.c_str(), host.size()))
                    return true;
            }
        }
        return false;
    }
    name = utf8(config_http_certificate_idn_ascii(host));
    if (!name.empty() && name.front() == '.')
        return false;
    auto result =
        X509_check_host(cert.get(), name.c_str(), name.size(), X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS, nullptr);
    if (result < 0)
        throw std::runtime_error("Certificate hostname validation failed");
    return result == 1;
}
}
