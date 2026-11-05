// Adapted from runtime v8.0.31 OpenSslCachedDirectoryStoreProvider.cs and
// System.Security.Cryptography.Native/pal_x509.c (ReadNextPublicCert).
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_user_store.hpp"
#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <mutex>

namespace opensim::framework {
namespace {
struct DirectoryCloser {
    void operator()(DIR *p) const {
        if (p)
            closedir(p);
    }
};
struct FileCloser {
    void operator()(FILE *p) const {
        if (p)
            std::fclose(p);
    }
};
struct PfxCloser {
    void operator()(PKCS12 *p) const { PKCS12_free(p); }
};
struct CertCloser {
    void operator()(X509 *p) const { X509_free(p); }
};
struct KeyCloser {
    void operator()(EVP_PKEY *p) const { EVP_PKEY_free(p); }
};
struct StackCloser {
    void operator()(STACK_OF(X509) * p) const { sk_X509_pop_free(p, X509_free); }
};
std::vector<CertificateDer> load(const std::filesystem::path &directory) {
    std::vector<CertificateDer> result;
    ERR_clear_error();
    std::unique_ptr<DIR, DirectoryCloser> dir(opendir(directory.c_str()));
    if (!dir)
        return result;
    while (auto *entry = readdir(dir.get())) {
        std::string_view name(entry->d_name);
        if (name.size() <= 4)
            continue;
        auto suffix = name.substr(name.size() - 4);
        if (suffix[0] != '.' || (suffix[1] != 'p' && suffix[1] != 'P') ||
            (suffix[2] != 'f' && suffix[2] != 'F') || (suffix[3] != 'x' && suffix[3] != 'X'))
            continue;
        std::unique_ptr<FILE, FileCloser> file(std::fopen((directory / entry->d_name).c_str(), "r"));
        if (!file)
            continue;
        std::unique_ptr<PKCS12, PfxCloser> pfx(d2i_PKCS12_fp(file.get(), nullptr));
        if (!pfx)
            continue;
        EVP_PKEY *key = nullptr;
        X509 *certificate = nullptr;
        STACK_OF(X509) *extras = nullptr;
        const int parsed = PKCS12_parse(pfx.get(), nullptr, &key, &certificate, &extras);
        std::unique_ptr<EVP_PKEY, KeyCloser> own_key(key);
        std::unique_ptr<X509, CertCloser> own_certificate(certificate);
        std::unique_ptr<STACK_OF(X509), StackCloser> own_extras(extras);
        if (parsed && !certificate && extras && sk_X509_num(extras) > 0)
            certificate = sk_X509_value(extras, 0);
        if (!certificate)
            continue;
        const int length = i2d_X509(certificate, nullptr);
        if (length <= 0)
            continue;
        CertificateDer der(static_cast<std::size_t>(length));
        auto *output = der.data();
        if (i2d_X509(certificate, &output) == length)
            result.push_back(std::move(der));
    }
    ERR_clear_error();
    return result;
}
}
struct ConfigHttpLinuxUserStore::State {
    std::filesystem::path directory;
    Clock clock;
    std::mutex mutex;
    Snapshot snapshot;
    std::chrono::steady_clock::time_point loaded;
    std::int64_t last_write = 0;
    bool forced = false;
    State(std::filesystem::path path, Clock time)
        : directory(std::filesystem::absolute(path).lexically_normal()), clock(std::move(time)) {}
};
ConfigHttpLinuxUserStore::ConfigHttpLinuxUserStore(std::filesystem::path directory, Clock clock)
    : state_(std::make_unique<State>(std::move(directory), std::move(clock))) {}
ConfigHttpLinuxUserStore::~ConfigHttpLinuxUserStore() = default;
void ConfigHttpLinuxUserStore::refresh() {
    std::lock_guard lock(state_->mutex);
    state_->forced = true;
}
ConfigHttpLinuxUserStore::Snapshot ConfigHttpLinuxUserStore::collection() {
    auto &s = *state_;
    std::lock_guard lock(s.mutex);
    const auto now = s.clock();
    const auto elapsed = now - s.loaded;
    if (!s.snapshot || s.forced || elapsed >= std::chrono::seconds(1)) {
        struct stat info{};
        const bool exists = stat(s.directory.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
        // DateTime resolution is 100ns; changes below that do not invalidate .NET's cache.
        const std::int64_t write =
            exists ? static_cast<std::int64_t>(info.st_mtim.tv_sec) * 10000000 + info.st_mtim.tv_nsec / 100
                   : 0;
        if (!s.snapshot || s.forced || elapsed >= std::chrono::seconds(30) ||
            (exists && write != s.last_write)) {
            s.snapshot = std::make_shared<const std::vector<CertificateDer>>(
                exists ? load(s.directory) : std::vector<CertificateDer>{});
            if (exists)
                s.last_write = write;
            s.loaded = s.clock();
            s.forced = false;
        }
    }
    return s.snapshot;
}
}
