// Runtime v8.0.31 OpenSslCertificateAssetDownloader / OpenSslX509Encoder /
// OpenSslX509CertificateReader. MIT .NET Foundation license:
// third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_certificate_download.hpp"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <climits>
#include <memory>
namespace opensim::framework {
std::optional<ConfigHttpLinuxDownloadedCertificate>
config_http_linux_parse_downloaded_certificate(std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > INT_MAX)
        return {};
    const auto* at = bytes.data();
    std::unique_ptr<X509, decltype(&X509_free)> cert(d2i_X509(nullptr, &at, static_cast<long>(bytes.size())),
                                                     X509_free);
    if (!cert) {
        ERR_clear_error();
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(
            BIO_new_mem_buf(bytes.data(), static_cast<int>(bytes.size())), BIO_free);
        if (!bio)
            throw std::bad_alloc();
        cert.reset(PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr));
        if (!cert) {
            ERR_clear_error();
            return {};
        }
    }
    // Other content types cannot produce a downloaded certificate. In particular,
    // the runtime detects PKCS7 but its single-certificate import always throws
    // Crypto_PKCS7_NoSigner; do not select a certificate from that container.
    ConfigHttpLinuxDownloadedCertificate result;
    auto encode = [&](auto writer, std::vector<std::uint8_t>& output) {
        auto count = writer(cert.get(), nullptr);
        if (count <= 0) {
            ERR_clear_error();
            return false;
        }
        output.resize(static_cast<std::size_t>(count));
        auto* target = output.data();
        if (writer(cert.get(), &target) != count) {
            ERR_clear_error();
            return false;
        }
        return true;
    };
    if (!encode(i2d_X509, result.der) || !encode(i2d_X509_AUX, result.auxiliary_der))
        return {};
    return result;
}
std::optional<ConfigHttpLinuxDownloadedCertificate>
config_http_linux_download_certificate(const ConfigHttpX509ResourceClient& client,
                                       std::optional<std::u16string_view> uri, std::int64_t timeout_ticks) {
    auto bytes = client.download_asset(uri, timeout_ticks);
    if (!bytes)
        return {};
    return config_http_linux_parse_downloaded_certificate(*bytes);
}
}
