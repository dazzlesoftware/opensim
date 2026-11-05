#include "opensim/framework/config_http_linux_user_store.hpp"
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace opensim::framework;
namespace {
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
struct Fixture {
    std::filesystem::path root;
    EVP_PKEY *key = nullptr;
    X509 *cert = nullptr;
    CertificateDer der;
    Fixture() {
        char pattern[] = "/tmp/opensim-user-store-XXXXXX";
        auto *path = mkdtemp(pattern);
        require(path, "mkdtemp");
        root = path;
        key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1");
        require(key, "keygen");
        cert = X509_new();
        require(cert, "certificate");
        require(X509_set_version(cert, 2) == 1, "version");
        ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);
        X509_gmtime_adj(X509_getm_notBefore(cert), -60);
        X509_gmtime_adj(X509_getm_notAfter(cert), 3600);
        X509_set_pubkey(cert, key);
        auto *name = X509_get_subject_name(cert);
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char *>("store fixture"), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        require(X509_sign(cert, key, EVP_sha256()) > 0, "signature");
        der.resize(i2d_X509(cert, nullptr));
        auto *p = der.data();
        i2d_X509(cert, &p);
    }
    ~Fixture() {
        X509_free(cert);
        EVP_PKEY_free(key);
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
    void pfx(const char *filename, const char *password, bool with_key = true) {
        STACK_OF(X509) *extras = sk_X509_new_null();
        sk_X509_push(extras, cert);
        PKCS12 *value = PKCS12_create(password, "fixture", with_key ? key : nullptr,
                                      with_key ? cert : nullptr, extras, 0, 0, 0, 0, 0);
        sk_X509_free(extras);
        require(value, "PKCS12 create");
        FILE *file = std::fopen((root / filename).c_str(), "wb");
        require(file, "PFX file");
        int result = i2d_PKCS12_fp(file, value);
        std::fclose(file);
        PKCS12_free(value);
        require(result == 1, "PFX write");
    }
    void timestamp(long seconds) {
        timespec times[2]{{seconds, 0}, {seconds, 0}};
        require(utimensat(AT_FDCWD, root.c_str(), times, 0) == 0, "timestamp");
    }
};
}
int main() {
    try {
        Fixture f;
        auto now = std::chrono::steady_clock::time_point{};
        ConfigHttpLinuxUserStore store(f.root, [&] { return now; });
        auto empty = store.collection();
        require(empty->empty(), "empty initial snapshot");
        f.pfx("first.PfX", nullptr);
        f.timestamp(100);
        now += std::chrono::milliseconds(999);
        require(store.collection() == empty, "no stat before one second");
        now += std::chrono::milliseconds(1);
        auto first = store.collection();
        require(first->size() == 1 && first->front() == f.der,
                "mixed-case suffix and null-password certificate");
        require(empty->empty(), "old snapshot immutable");
        f.pfx("duplicate.pfx", "");
        f.pfx("chain-only.pfx", nullptr, false);
        f.pfx("encrypted.pfx", "secret");
        f.pfx("ignored.pem", nullptr);
        std::ofstream(f.root / "bad.pfx") << "invalid";
        f.timestamp(101);
        now += std::chrono::seconds(1);
        auto three = store.collection();
        require(three->size() == 3, "duplicates kept; extras fallback; invalid/password/suffix skipped");
        for (const auto &cert : *three)
            require(cert == f.der, "exact DER");
        require(first->size() == 1, "prior loaded snapshot immutable");
        std::filesystem::remove(f.root / "duplicate.pfx");
        f.timestamp(101);
        now += std::chrono::seconds(29);
        require(store.collection() == three, "unchanged mtime before thirty seconds");
        now += std::chrono::seconds(1);
        auto two = store.collection();
        require(two->size() == 2, "thirty-second reload");
        auto moved = f.root;
        moved += "-moved";
        std::filesystem::rename(f.root, moved);
        now += std::chrono::seconds(1);
        require(store.collection() == two, "deleted directory retained before expiry");
        store.refresh();
        auto absent = store.collection();
        require(absent->empty(), "forced missing-directory refresh");
        std::filesystem::rename(moved, f.root);
        store.refresh();
        require(store.collection()->size() == 2, "forced refresh without clock advance");
        std::cout << "Linux user-store PFX and cache scenarios passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
