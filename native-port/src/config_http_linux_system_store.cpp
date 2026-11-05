// Adapted from runtime v8.0.31 OpenSslCachedSystemStoreProvider.cs and
// System.Security.Cryptography.Native/pal_x509_root.c.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_system_store.hpp"
#include "opensim/framework/config_http_linux_x500_name.hpp"
#include "opensim/framework/text.hpp"
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#include <sys/stat.h>
#include <cstdlib>
#include <algorithm>
#include <mutex>
#include <set>
namespace opensim::framework {
namespace {
using Stamp = std::optional<std::int64_t>;
bool stat_path(const std::filesystem::path &path, bool directory, Stamp &stamp) {
    stamp.reset();
    struct stat info{};
    if (stat(path.c_str(), &info) != 0 || (directory ? !S_ISDIR(info.st_mode) : !S_ISREG(info.st_mode)))
        return false;
    stamp = static_cast<std::int64_t>(info.st_mtim.tv_sec) * 10000000 + info.st_mtim.tv_nsec / 100;
    return true;
}
std::filesystem::path fullpath(const char *path) {
    return std::filesystem::absolute(utf8(utf16_replacing(path))).lexically_normal();
}
std::u16string name(X509_NAME *value) {
    const int length = i2d_X509_NAME(value, nullptr);
    if (length < 0)
        throw ConfigHttpLinuxCertificateError();
    CertificateDer der(static_cast<std::size_t>(length));
    auto *at = der.data();
    if (i2d_X509_NAME(value, &at) != length)
        throw ConfigHttpLinuxCertificateError();
    return config_http_linux_x500_name(der);
}
struct Identity {
    std::uint32_t hash;
    std::u16string issuer;
    CertificateDer serial;
    auto operator<=>(const Identity &) const = default;
};
Identity identity(X509 *cert, std::u16string issuer) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned size = 0;
    if (!X509_digest(cert, EVP_sha1(), digest, &size) || size < 4)
        throw ConfigHttpLinuxCertificateError();
    auto *serial = X509_get_serialNumber(cert);
    auto *data = ASN1_STRING_get0_data(serial);
    const int length = ASN1_STRING_length(serial);
    return {(std::uint32_t(digest[0]) << 24) | (std::uint32_t(digest[1]) << 16) |
                (std::uint32_t(digest[2]) << 8) | digest[3],
            std::move(issuer), CertificateDer(data, data + length)};
}
struct Loader {
    ConfigHttpLinuxSystemStoreSnapshot output;
    std::set<Identity> roots, intermediates;
    bool file(const std::filesystem::path &path, Stamp &modified, bool skip_stat = false) {
        modified.reset();
        if (!skip_stat && !stat_path(path, false, modified))
            return false;
        const auto filename = utf8(utf16_replacing(path.native()));
        std::unique_ptr<BIO, decltype(&BIO_free)> bio(BIO_new_file(filename.c_str(), "rb"), BIO_free);
        if (!bio) {
            ERR_clear_error();
            return false;
        }
        bool any = false;
        for (;;) {
            X509 *raw = PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr);
            if (!raw) {
                ERR_clear_error();
                raw = d2i_X509_bio(bio.get(), nullptr);
            }
            if (!raw) {
                ERR_clear_error();
                break;
            }
            std::unique_ptr<X509, decltype(&X509_free)> cert(raw, X509_free);
            if (!X509_check_purpose(raw, -1, 0))
                throw ConfigHttpLinuxCertificateError();
            any = true;
            auto issuer = name(X509_get_issuer_name(raw));
            const bool root = name(X509_get_subject_name(raw)) == issuer;
            if (!(root ? roots : intermediates).insert(identity(raw, std::move(issuer))).second)
                continue;
            const int length = i2d_X509(raw, nullptr);
            if (length <= 0)
                throw ConfigHttpLinuxCertificateError();
            CertificateDer der(static_cast<std::size_t>(length));
            auto *at = der.data();
            if (i2d_X509(raw, &at) != length)
                throw ConfigHttpLinuxCertificateError();
            (root ? output.roots : output.intermediates).push_back(std::move(der));
        }
        return any;
    }
    bool directory(const std::filesystem::path &path, Stamp &modified) {
        if (!stat_path(path, true, modified))
            return false;
        bool any = false;
        for (const auto &entry : std::filesystem::directory_iterator(path)) {
            std::error_code error;
            if (entry.is_directory(error))
                continue;
            Stamp unused;
            any = file(entry.path(), unused, true) || any;
        }
        return any;
    }
};
}
struct ConfigHttpLinuxSystemStore::State {
    Clock clock;
    std::mutex mutex;
    Snapshot snapshot;
    std::chrono::steady_clock::time_point loaded;
    std::optional<std::filesystem::path> file;
    std::vector<std::filesystem::path> directories;
    Stamp file_modified;
    std::vector<Stamp> directory_modified;
    bool default_directory = false;
    explicit State(Clock time) : clock(std::move(time)) {}
    void paths() {
        const char *dirs = std::getenv(X509_get_default_cert_dir_env());
        default_directory = dirs == nullptr;
        if (!dirs)
            dirs = X509_get_default_cert_dir();
        std::string_view remaining = dirs ? dirs : "";
        while (!remaining.empty()) {
            auto colon = remaining.find(':');
            auto item = remaining.substr(0, colon);
            if (!item.empty()) {
                auto path = fullpath(std::string(item).c_str());
                if (std::find(directories.begin(), directories.end(), path) == directories.end())
                    directories.push_back(std::move(path));
            }
            if (colon == remaining.npos)
                break;
            remaining.remove_prefix(colon + 1);
        }
        const char *filename = std::getenv(X509_get_default_cert_file_env());
        if (!filename)
            filename = X509_get_default_cert_file();
        if (filename && *filename)
            file = fullpath(filename);
        directory_modified.resize(directories.size());
    }
    bool changed() {
        Stamp stamp;
        if (file) {
            stat_path(*file, false, stamp);
            if (stamp != file_modified)
                return true;
        }
        for (std::size_t i = 0; i < directories.size(); ++i) {
            stat_path(directories[i], true, stamp);
            if (stamp != directory_modified[i])
                return true;
        }
        return false;
    }
    Snapshot load() {
        const bool first = !snapshot;
        if (first)
            paths();
        Loader loader;
        if (file)
            loader.file(*file, file_modified);
        bool loaded_directory = false;
        for (std::size_t i = 0; i < directories.size(); ++i)
            loaded_directory = loader.directory(directories[i], directory_modified[i]);
        Stamp fallback;
        if (first && !loaded_directory && default_directory && loader.directory("/etc/ssl/certs", fallback)) {
            directories = {"/etc/ssl/certs"};
            directory_modified = {fallback};
        }
        snapshot = std::make_shared<const ConfigHttpLinuxSystemStoreSnapshot>(std::move(loader.output));
        loaded = clock();
        return snapshot;
    }
};
ConfigHttpLinuxSystemStore::ConfigHttpLinuxSystemStore(Clock clock)
    : state_(std::make_unique<State>(std::move(clock))) {}
ConfigHttpLinuxSystemStore::~ConfigHttpLinuxSystemStore() = default;
ConfigHttpLinuxSystemStore::Snapshot ConfigHttpLinuxSystemStore::collection() {
    auto &s = *state_;
    std::lock_guard lock(s.mutex);
    const auto elapsed = s.clock() - s.loaded;
    if (!s.snapshot)
        return s.load();
    if (elapsed > std::chrono::seconds(5) && (elapsed > std::chrono::minutes(5) || s.changed()))
        return s.load();
    return s.snapshot;
}
}
