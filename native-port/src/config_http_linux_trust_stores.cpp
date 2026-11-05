// Consumed runtime v8.0.31 PersistedFiles, OpenSslDirectoryBasedStoreProvider,
// ChainPal.OpenSsl and OpenSslX509ChainProcessor store composition.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#include "opensim/framework/config_http_linux_trust_stores.hpp"
#include "opensim/framework/text.hpp"
#include <pwd.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace opensim::framework {
ConfigHttpLinuxStoreError::ConfigHttpLinuxStoreError(std::string type, std::u16string text,
                                                     std::uint32_t code,
                                                     std::shared_ptr<const ConfigHttpLinuxStoreError> cause)
    : std::runtime_error(utf8(text)), source_type(std::move(type)), message(std::move(text)), hresult(code),
      inner(std::move(cause)) {}
std::string config_http_linux_user_home_directory() {
    if (const char *home = std::getenv("HOME"); home && *home)
        return utf8(utf16_replacing(home));
    std::vector<char> buffer(256);
    for (;;) {
        struct passwd entry{};
        struct passwd *found = nullptr;
        int error;
        do {
            error = getpwuid_r(geteuid(), &entry, buffer.data(), buffer.size(), &found);
        } while (error == EINTR);
        if (!error)
            return found && found->pw_dir ? utf8(utf16_replacing(found->pw_dir)) : std::string{};
        if (error != ERANGE)
            throw ConfigHttpLinuxStoreError("System.IO.IOException", utf16_replacing(std::strerror(error)),
                                            static_cast<std::uint32_t>(error));
        if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 2))
            throw std::bad_alloc();
        buffer.resize(buffer.size() * 2);
    }
}
const std::filesystem::path &config_http_linux_user_store_root() {
    static const auto root = []() {
        auto home = config_http_linux_user_home_directory();
        if (home.empty())
            throw ConfigHttpLinuxStoreError(
                "System.InvalidOperationException",
                u"The home directory of the current user could not be determined.", 0x80131509);
        return std::filesystem::path(home) / ".dotnet" / "corefx" / "cryptography" / "x509stores";
    }();
    return root;
}
namespace {
[[noreturn]] void denied(const std::filesystem::path &path, int error) {
    auto full = std::filesystem::absolute(path).lexically_normal().native();
    auto inner = std::make_shared<const ConfigHttpLinuxStoreError>(
        "System.IO.IOException", utf16_replacing(std::strerror(error)), static_cast<std::uint32_t>(error));
    throw ConfigHttpLinuxStoreError("System.UnauthorizedAccessException",
                                    u"Access to the path '" + utf16_replacing(full) + u"' is denied.",
                                    0x80070005, std::move(inner));
}
}
void config_http_linux_check_disallowed_store(const std::filesystem::path &root) {
    const auto path = root / "disallowed";
    std::error_code error;
    // Directory.Exists/EnumerateFiles normalize lexically before native I/O;
    // the CryptographicException still formats the original GetStorePath value.
    const auto full = std::filesystem::absolute(path, error).lexically_normal();
    if (error)
        return;
    if (!std::filesystem::is_directory(full, error))
        return;
    auto entries = std::filesystem::directory_iterator(full, error);
    if (error) {
        if (error.value() == EACCES || error.value() == EPERM)
            denied(path, error.value());
        return;
    }
    for (auto end = std::filesystem::directory_iterator(); entries != end; entries.increment(error)) {
        if (error) {
            if (error.value() == EACCES || error.value() == EPERM)
                denied(path, error.value());
            return;
        }
        std::error_code status_error;
        if (entries->is_directory(status_error))
            continue;
        auto message =
            u"The Disallowed store is not supported on this platform, but already has data. All files under '" +
            utf16_replacing(path.native()) + u"' must be removed.";
        auto inner = std::make_shared<const ConfigHttpLinuxStoreError>("System.PlatformNotSupportedException",
                                                                       message, 0x80131539);
        throw ConfigHttpLinuxStoreError("System.Security.Cryptography.CryptographicException",
                                        std::move(message), 0x80131501, std::move(inner));
    }
    if (error && (error.value() == EACCES || error.value() == EPERM))
        denied(path, error.value());
}
struct ConfigHttpLinuxTrustStores::State {
    std::filesystem::path root;
    ConfigHttpLinuxSystemStore system;
    ConfigHttpLinuxUserStore user_root, ca, my;
    State(std::filesystem::path path, ConfigHttpLinuxUserStore::Clock clock)
        : root(std::move(path)), system(clock), user_root(root / "root", clock), ca(root / "ca", clock),
          my(root / "my", std::move(clock)) {}
};
ConfigHttpLinuxTrustStores::ConfigHttpLinuxTrustStores(std::filesystem::path root,
                                                       ConfigHttpLinuxUserStore::Clock clock)
    : state_(std::make_unique<State>(std::move(root), std::move(clock))) {}
ConfigHttpLinuxTrustStores::~ConfigHttpLinuxTrustStores() = default;
ConfigHttpLinuxCertificateStores ConfigHttpLinuxTrustStores::collection(bool custom) {
    config_http_linux_check_disallowed_store(state_->root);
    return collection_unchecked(custom);
}
ConfigHttpLinuxCertificateStores ConfigHttpLinuxTrustStores::collection_unchecked(bool custom) {
    auto &s = *state_;
    const auto system = s.system.collection();
    ConfigHttpLinuxCertificateStores result;
    const auto ca = s.ca.collection(), my = s.my.collection();
    result.user_intermediates = *ca;
    result.user_intermediates.insert(result.user_intermediates.end(), my->begin(), my->end());
    if (!custom) {
        result.trusted = system->roots;
        const auto roots = s.user_root.collection();
        result.trusted.insert(result.trusted.end(), roots->begin(), roots->end());
    }
    result.system_intermediates = system->intermediates;
    return result;
}
void ConfigHttpLinuxTrustStores::refresh_user_stores() {
    state_->user_root.refresh();
    state_->my.refresh();
    state_->ca.refresh();
}
namespace {
ConfigHttpLinuxTrustStores &default_stores() {
    static ConfigHttpLinuxTrustStores stores(config_http_linux_user_store_root());
    return stores;
}
}
void config_http_linux_flush_user_stores() { default_stores().refresh_user_stores(); }
ConfigHttpLinuxCertificateStores config_http_linux_default_certificate_stores(bool custom) {
    const auto &root = config_http_linux_user_store_root();
    config_http_linux_check_disallowed_store(root);
    return default_stores().collection_unchecked(custom);
}
ConfigHttpLinuxCertificateResult config_http_linux_cached_certificate_errors(
    std::span<const std::uint8_t> leaf, const std::vector<CertificateDer> &extra, std::u16string_view host,
    ConfigHttpLinuxCertificatePolicy policy) {
    if (leaf.empty()) {
        ConfigHttpLinuxCertificateResult result;
        result.errors = 1;
        return result;
    }
    auto stores = config_http_linux_default_certificate_stores(policy.custom_roots.has_value());
    return config_http_linux_certificate_errors(leaf, extra, stores, host, std::move(policy));
}
}
