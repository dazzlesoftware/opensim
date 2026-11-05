// Runtime v8.0.31 ChainPal.SaveIntermediateCertificates,
// OpenSslDirectoryBasedStoreProvider.AddCertToStore and UnixExportProvider.
// MIT .NET Foundation license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_certificate_persistence.hpp"
#include "opensim/framework/config_http_linux_trust_stores.hpp"
#include "opensim/framework/config_http_linux_x500_name.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <climits>
#include <algorithm>
namespace opensim::framework {
namespace {
using Cert = std::unique_ptr<X509, decltype(&X509_free)>;
[[noreturn]] void failure() {
    throw ConfigHttpLinuxCertificateError();
}
struct Fd {
    int value;
    ~Fd() {
        if (value >= 0)
            ::close(value);
    }
};
struct BagCloser {
    void operator()(STACK_OF(PKCS12_SAFEBAG) * value) const {
        sk_PKCS12_SAFEBAG_pop_free(value, PKCS12_SAFEBAG_free);
    }
};
struct SafeCloser {
    void operator()(STACK_OF(PKCS7) * value) const {
        sk_PKCS7_pop_free(value, PKCS7_free);
    }
};
struct DirectoryCloser {
    void operator()(DIR* value) const {
        if (value)
            closedir(value);
    }
};
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using KeyId = std::optional<CertificateDer>;
KeyId local_key_id(const PKCS12_SAFEBAG* bag) {
    auto* attr = PKCS12_SAFEBAG_get0_attr(bag, NID_localKeyID);
    if (!attr)
        return {};
    if (ASN1_TYPE_get(attr) != V_ASN1_OCTET_STRING)
        failure();
    auto* value = attr->value.octet_string;
    const auto count = ASN1_STRING_length(value);
    if (count <= 0)
        return CertificateDer{};
    auto* data = ASN1_STRING_get0_data(value);
    return CertificateDer(data, data + count);
}
bool matches_public_key(X509* cert, EVP_PKEY* key) {
    X509_PUBKEY* raw = nullptr;
    if (!X509_PUBKEY_set(&raw, key))
        return false;
    std::unique_ptr<X509_PUBKEY, decltype(&X509_PUBKEY_free)> pub(raw, X509_PUBKEY_free);
    ASN1_OBJECT* co = nullptr;
    ASN1_OBJECT* ko = nullptr;
    const unsigned char* cb = nullptr;
    const unsigned char* kb = nullptr;
    int cn = 0, kn = 0;
    X509_ALGOR* ca = nullptr;
    X509_ALGOR* ka = nullptr;
    if (!X509_PUBKEY_get0_param(&co, &cb, &cn, &ca, X509_get_X509_PUBKEY(cert)) ||
        !X509_PUBKEY_get0_param(&ko, &kb, &kn, &ka, pub.get()) || cn != kn || cn <= 0 ||
        !std::equal(cb, cb + cn, kb))
        return false;
    const int cert_nid = OBJ_obj2nid(co), key_nid = OBJ_obj2nid(ko);
    const auto rsa = [](int nid) { return nid == NID_rsaEncryption || nid == NID_rsassaPss; };
    const auto ec = [](const ASN1_OBJECT* oid) {
        if (OBJ_obj2nid(oid) == NID_X9_62_id_ecPublicKey)
            return true;
        char text[32]{};
        return OBJ_obj2txt(text, sizeof(text), oid, 1) > 0 && std::string_view(text) == "1.3.132.1.12";
    };
    if (rsa(cert_nid)) {
        auto nil = [](const ASN1_TYPE* p) { return !p || ASN1_TYPE_get(p) == V_ASN1_NULL; };
        return rsa(key_nid) && nil(ca->parameter) && nil(ka->parameter);
    }
    if (ec(co)) {
        if (!ec(ko) || !ka->parameter)
            return false;
    } else if (OBJ_cmp(co, ko) != 0)
        return false;
    return ca->parameter && ka->parameter ? ASN1_TYPE_cmp(ca->parameter, ka->parameter) == 0
                                          : ca->parameter == ka->parameter;
}
Cert pfx_candidate(PKCS12* pfx, const char* password) {
    struct Public {
        Cert cert;
        KeyId id;
    };
    struct Private {
        Key key;
        KeyId id;
    };
    std::vector<Public> certificates;
    std::vector<Private> keys;
    std::unique_ptr<STACK_OF(PKCS7), SafeCloser> safes(PKCS12_unpack_authsafes(pfx));
    if (!safes)
        failure();
    for (int i = 0; i < sk_PKCS7_num(safes.get()); ++i) {
        auto* safe = sk_PKCS7_value(safes.get(), i);
        if (!PKCS7_type_is_data(safe) && !PKCS7_type_is_encrypted(safe))
            continue;
        if (PKCS7_type_is_encrypted(safe)) {
            auto* encrypted = safe->d.encrypted;
            if (!encrypted || !encrypted->enc_data || !encrypted->enc_data->enc_data ||
                OBJ_obj2nid(encrypted->enc_data->content_type) != NID_pkcs7_data)
                failure();
            auto version = ASN1_INTEGER_get(encrypted->version);
            if (version != 0 && version != 2)
                failure();
        }
        std::unique_ptr<STACK_OF(PKCS12_SAFEBAG), BagCloser> bags(
            PKCS7_type_is_data(safe) ? PKCS12_unpack_p7data(safe)
                                     : PKCS12_unpack_p7encdata(safe, password, 0));
        if (!bags)
            failure();
        for (int j = 0; j < sk_PKCS12_SAFEBAG_num(bags.get()); ++j) {
            auto* bag = sk_PKCS12_SAFEBAG_value(bags.get(), j);
            auto type = PKCS12_SAFEBAG_get_nid(bag);
            if (type == NID_certBag && PKCS12_SAFEBAG_get_bag_nid(bag) == NID_x509Certificate) {
                Cert cert(PKCS12_SAFEBAG_get1_cert(bag), X509_free);
                if (!cert)
                    failure();
                certificates.push_back({std::move(cert), local_key_id(bag)});
            }
            if (type == NID_keyBag || type == NID_pkcs8ShroudedKeyBag) {
                std::unique_ptr<PKCS8_PRIV_KEY_INFO, decltype(&PKCS8_PRIV_KEY_INFO_free)> decrypted(
                    type == NID_pkcs8ShroudedKeyBag ? PKCS12_decrypt_skey(bag, password, 0) : nullptr,
                    PKCS8_PRIV_KEY_INFO_free);
                auto* info = type == NID_keyBag ? PKCS12_SAFEBAG_get0_p8inf(bag) : decrypted.get();
                Key key(info ? EVP_PKCS82PKEY(info) : nullptr, EVP_PKEY_free);
                if (key) {
                    auto id = EVP_PKEY_base_id(key.get());
                    if (id != EVP_PKEY_RSA && id != EVP_PKEY_DSA && id != EVP_PKEY_EC)
                        key.reset();
                }
                keys.push_back({std::move(key), local_key_id(bag)});
            }
        }
    }
    if (certificates.empty())
        failure();
    auto selected = certificates.size() - 1;
    bool selected_key = false;
    // UnixPkcs12Reader binds in reverse bag order and prefers the last keyed
    // certificate. A localKeyId takes priority over public-key matching.
    for (std::size_t i = certificates.size(); i-- != 0;) {
        auto& entry = certificates[i];
        const Private* binding = nullptr;
        if (entry.id)
            for (const auto& key : keys)
                if (key.id == entry.id) {
                    binding = &key;
                    break;
                }
        if (!binding)
            for (const auto& key : keys)
                if (key.key && matches_public_key(entry.cert.get(), key.key.get())) {
                    binding = &key;
                    break;
                }
        if (binding) {
            if (!binding->key)
                failure();
            if (!selected_key) {
                selected = i;
                selected_key = true;
            }
        }
    }
    return std::move(certificates[selected].cert);
}
std::u16string issuer(X509* cert) {
    auto* name = X509_get_issuer_name(cert);
    int length = i2d_X509_NAME(name, nullptr);
    if (length <= 0)
        failure();
    std::vector<std::uint8_t> bytes(length);
    auto* at = bytes.data();
    if (i2d_X509_NAME(name, &at) != length)
        failure();
    return config_http_linux_x500_name(bytes);
}
bool equal(X509* a, X509* b) {
    auto* sa = X509_get_serialNumber(a);
    auto* sb = X509_get_serialNumber(b);
    return ASN1_STRING_length(sa) == ASN1_STRING_length(sb) &&
           std::equal(ASN1_STRING_get0_data(sa), ASN1_STRING_get0_data(sa) + ASN1_STRING_length(sa),
                      ASN1_STRING_get0_data(sb)) &&
           issuer(a) == issuer(b);
}
Cert candidate(const std::filesystem::path& file) {
    std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(file.c_str(), "rb"), BIO_free);
    if (!bio)
        return {nullptr, X509_free};
    Cert cert(PEM_read_bio_X509_AUX(bio.get(), nullptr, nullptr, nullptr), X509_free);
    if (cert)
        return cert;
    ERR_clear_error();
    BIO_reset(bio.get());
    cert.reset(d2i_X509_bio(bio.get(), nullptr));
    if (cert)
        return cert;
    ERR_clear_error();
    BIO_reset(bio.get());
    std::unique_ptr<PKCS12, decltype(&PKCS12_free)> pfx(d2i_PKCS12_bio(bio.get(), nullptr), PKCS12_free);
    if (!pfx)
        return {nullptr, X509_free};
    try {
        if (PKCS12_mac_present(pfx.get())) {
            if (PKCS12_verify_mac(pfx.get(), "", 0))
                return pfx_candidate(pfx.get(), "");
            if (PKCS12_verify_mac(pfx.get(), nullptr, 0))
                return pfx_candidate(pfx.get(), nullptr);
        } else {
            try {
                return pfx_candidate(pfx.get(), "");
            } catch (const ConfigHttpLinuxCertificateError&) {
                ERR_clear_error();
                return pfx_candidate(pfx.get(), nullptr);
            }
        }
    } catch (const ConfigHttpLinuxCertificateError&) {
    }
    ERR_clear_error();
    return {nullptr, X509_free};
}
std::vector<std::uint8_t> export_public_pfx(X509* cert) {
    std::unique_ptr<STACK_OF(PKCS12_SAFEBAG), BagCloser> bags(sk_PKCS12_SAFEBAG_new_null());
    if (!bags)
        failure();
    auto* bag = PKCS12_SAFEBAG_create_cert(cert);
    if (!bag)
        failure();
    if (!sk_PKCS12_SAFEBAG_push(bags.get(), bag)) {
        PKCS12_SAFEBAG_free(bag);
        failure();
    }
    unsigned char encryption_salt[16], mac_salt[20];
    if (RAND_bytes(encryption_salt, sizeof(encryption_salt)) != 1 ||
        RAND_bytes(mac_salt, sizeof(mac_salt)) != 1)
        failure();
    std::unique_ptr<PKCS7, decltype(&PKCS7_free)> encrypted(
        PKCS12_pack_p7encdata(NID_pbe_WithSHA1And3_Key_TripleDES_CBC, "", 0, encryption_salt,
                              sizeof(encryption_salt), 2000, bags.get()),
        PKCS7_free);
    if (!encrypted)
        failure();
    std::unique_ptr<STACK_OF(PKCS7), SafeCloser> safes(sk_PKCS7_new_null());
    if (!safes)
        failure();
    if (!sk_PKCS7_push(safes.get(), encrypted.get()))
        failure();
    encrypted.release();
    std::unique_ptr<PKCS12, decltype(&PKCS12_free)> pfx(PKCS12_init(NID_pkcs7_data), PKCS12_free);
    if (!pfx || !PKCS12_pack_authsafes(pfx.get(), safes.get()) ||
        !PKCS12_set_mac(pfx.get(), "", 0, mac_salt, sizeof(mac_salt), 2000, EVP_sha1()))
        failure();
    const X509_ALGOR* algorithm = nullptr;
    PKCS12_get0_mac(nullptr, &algorithm, nullptr, nullptr, pfx.get());
    if (!algorithm)
        failure();
    X509_ALGOR_set0(const_cast<X509_ALGOR*>(algorithm), OBJ_nid2obj(NID_sha1), V_ASN1_UNDEF, nullptr);
    int length = i2d_PKCS12(pfx.get(), nullptr);
    if (length <= 0)
        failure();
    std::vector<std::uint8_t> bytes(length);
    auto* at = bytes.data();
    if (i2d_PKCS12(pfx.get(), &at) != length)
        failure();
    return bytes;
}
bool file_exists(const std::filesystem::path& path) {
    struct stat info{};
    return ::stat(path.c_str(), &info) == 0 && !S_ISDIR(info.st_mode);
}
void add(const std::filesystem::path& directory, const ConfigHttpLinuxDownloadedCertificate& downloaded) {
    std::filesystem::create_directories(directory);
    struct stat info{};
    if (::stat(directory.c_str(), &info) != 0 || info.st_uid != ::geteuid() || (info.st_mode & 0700) != 0700)
        failure();
    if (downloaded.der.empty() || downloaded.der.size() > LONG_MAX)
        failure();
    auto* at = downloaded.der.data();
    Cert cert(d2i_X509(nullptr, &at, static_cast<long>(downloaded.der.size())), X509_free);
    if (!cert)
        failure();
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned length = 0;
    if (!X509_digest(cert.get(), EVP_sha1(), digest, &length))
        failure();
    std::string thumbprint;
    constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < length; ++i) {
        thumbprint += hex[digest[i] >> 4];
        thumbprint += hex[digest[i] & 15];
    }
    std::unique_ptr<DIR, DirectoryCloser> entries(opendir(directory.c_str()));
    if (!entries)
        failure();
    bool candidates = false;
    while (auto* entry = readdir(entries.get())) {
        std::string_view name(entry->d_name);
        if (!name.starts_with(thumbprint) || !name.ends_with(".pfx"))
            continue;
        auto path = directory / entry->d_name;
        struct stat entry_info{};
        if (::stat(path.c_str(), &entry_info) == 0 && S_ISDIR(entry_info.st_mode))
            continue;
        candidates = true;
        auto existing = candidate(path);
        if (existing && equal(cert.get(), existing.get()))
            return;
        ERR_clear_error();
    }
    std::filesystem::path path = directory / (thumbprint + ".pfx");
    if (candidates) {
        bool found = false;
        for (int i = 1; i <= 9; ++i) {
            path = directory / (thumbprint + "." + std::to_string(i) + ".pfx");
            if (!file_exists(path)) {
                found = true;
                break;
            }
        }
        if (!found)
            failure();
    }
    Fd file{::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600)};
    if (file.value < 0)
        failure();
    if (::fstat(file.value, &info) != 0 || (info.st_mode & 07777) != 0600)
        failure();
    auto bytes = export_public_pfx(cert.get());
    std::size_t written = 0;
    while (written < bytes.size()) {
        auto count = ::write(file.value, bytes.data() + written, bytes.size() - written);
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            failure();
        written += static_cast<std::size_t>(count);
    }
}
}
void config_http_linux_persist_aia_issuers(const ConfigHttpLinuxAiaAssetResult& result,
                                           const std::filesystem::path& root,
                                           const std::function<void()>& flush) {
    if (result.native_error != 0)
        return;
    for (const auto& cert : result.downloaded) {
        try {
            add(std::filesystem::absolute(root / "ca").lexically_normal(), cert);
            if (flush)
                flush();
        } catch (...) {
            ERR_clear_error();
        }
    }
}
void config_http_linux_persist_aia_issuers(const ConfigHttpLinuxAiaAssetResult& result) {
    if (result.native_error != 0)
        return;
    config_http_linux_persist_aia_issuers(result, config_http_linux_user_store_root(),
                                          config_http_linux_flush_user_stores);
}
ConfigHttpLinuxCertificateResult config_http_linux_certificate_errors_with_downloads(
    std::span<const std::uint8_t> leaf, const std::vector<CertificateDer>& extra, std::u16string_view host,
    ConfigHttpLinuxCertificatePolicy policy, const ConfigHttpX509ResourceClient& client, std::int64_t ticks,
    bool disable_aia) {
    if (leaf.empty()) {
        ConfigHttpLinuxCertificateResult result;
        result.errors = 1;
        return result;
    }
    auto stores = config_http_linux_default_certificate_stores(policy.custom_roots.has_value());
    ConfigHttpLinuxCertificateChain chain(leaf, stores, std::move(policy));
    const auto first = chain.find_first(extra);
    if (!disable_aia && (first == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT ||
                         first == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)) {
        if (ticks == 0)
            ticks = 150000000;
        else if (ticks < 0 || ticks > 600000000)
            ticks = 600000000;
        auto uris = native_config_uri_object_backend();
        auto result = config_http_linux_find_chain_via_assets(
            chain, *uris, std::chrono::milliseconds(ticks / 10000),
            ConfigHttpLinuxIssuerAssetDownload{
                [&](auto uri, auto) { return config_http_linux_download_certificate(client, uri, ticks); }});
        config_http_linux_persist_aia_issuers(result);
    }
    return chain.finish(host);
}
}
