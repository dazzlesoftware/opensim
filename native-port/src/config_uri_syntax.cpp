// Native .NET8.0.31 UriParser syntax table/cache; .NET Foundation MIT license.
#include "opensim/framework/config_uri_syntax.hpp"
#include <mutex>
#include <unordered_map>
#include <stdexcept>
namespace opensim::framework {
namespace {
using F = ConfigUriSyntaxFlags;
constexpr std::uint64_t bits(F value) {
    return static_cast<std::uint64_t>(value);
}
constexpr std::uint64_t flags_HttpUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveQuery | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted |
         F::ConvertPathSlashes | F::CompressPath | F::CanonicalizeAsFilePath | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_HttpsUri = flags_HttpUri;
constexpr std::uint64_t flags_WsUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveQuery | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted |
         F::ConvertPathSlashes | F::CompressPath | F::CanonicalizeAsFilePath | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_WssUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveQuery | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted |
         F::ConvertPathSlashes | F::CompressPath | F::CanonicalizeAsFilePath | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_FtpUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted | F::ConvertPathSlashes |
         F::CompressPath | F::CanonicalizeAsFilePath | F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_FileUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHavePath | F::MayHaveQuery |
         F::MayHaveFragment | F::AllowEmptyHost | F::AllowUncHost | F::FileLikeUri | F::AllowDOSPath |
         F::PathIsRooted | F::ConvertPathSlashes | F::CompressPath | F::CanonicalizeAsFilePath |
         F::UnEscapeDotsAndSlashes | F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_UnixFileUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHavePath | F::MayHaveQuery |
         F::MayHaveFragment | F::AllowEmptyHost | F::AllowUncHost | F::FileLikeUri | F::AllowDOSPath |
         F::PathIsRooted | F::CompressPath | F::CanonicalizeAsFilePath | F::UnEscapeDotsAndSlashes |
         F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_GopherUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_NntpUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_NewsUri = bits(F::MayHavePath | F::MayHaveFragment | F::AllowIriParsing) |
                                        bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_MailToUri =
    bits(F::AllowAnInternetHost | F::MayHaveUserInfo | F::MayHavePort | F::MayHavePath | F::MayHaveQuery |
         F::MayHaveFragment | F::AllowEmptyHost | F::AllowUncHost | F::MailToLikeUri | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_UuidUri = flags_NewsUri;
constexpr std::uint64_t flags_TelnetUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveFragment | F::AllowUncHost | F::PathIsRooted | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_LdapUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveQuery | F::MayHaveFragment | F::AllowEmptyHost | F::AllowUncHost |
         F::PathIsRooted | F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_NetTcpUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHavePort | F::MayHavePath | F::MayHaveQuery |
         F::MayHaveFragment | F::PathIsRooted | F::ConvertPathSlashes | F::CompressPath |
         F::CanonicalizeAsFilePath | F::UnEscapeDotsAndSlashes | F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_NetPipeUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHavePath | F::MayHaveQuery |
         F::MayHaveFragment | F::PathIsRooted | F::ConvertPathSlashes | F::CompressPath |
         F::CanonicalizeAsFilePath | F::UnEscapeDotsAndSlashes | F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_VsMacrosUri =
    bits(F::AllowAnInternetHost | F::MustHaveAuthority | F::MayHavePath | F::MayHaveFragment |
         F::AllowEmptyHost | F::AllowUncHost | F::FileLikeUri | F::AllowDOSPath | F::ConvertPathSlashes |
         F::CompressPath | F::CanonicalizeAsFilePath | F::UnEscapeDotsAndSlashes | F::AllowIdn |
         F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);
constexpr std::uint64_t flags_unknown =
    bits(F::AllowAnInternetHost | F::OptionalAuthority | F::MayHaveUserInfo | F::MayHavePort |
         F::MayHavePath | F::MayHaveQuery | F::MayHaveFragment | F::AllowEmptyHost | F::AllowUncHost |
         F::V1_UnknownUri | F::AllowDOSPath | F::PathIsRooted | F::ConvertPathSlashes | F::CompressPath |
         F::AllowIdn | F::AllowIriParsing) |
    bits(F::SimpleUserSyntax | F::BuiltInSyntax);

ConfigUriText text(std::u16string value) {
    return std::make_shared<const std::u16string>(std::move(value));
}
struct Builtins {
    std::unordered_map<std::u16string, ConfigUriSyntaxPtr> table;
    ConfigUriSyntaxPtr unix_file;
    Builtins() {
        auto add = [&](std::u16string name, std::int32_t port, std::uint64_t flags) {
            auto value = std::make_shared<const ConfigUriSyntax>(ConfigUriSyntax{text(name), port, flags});
            table.emplace(std::move(name), std::move(value));
        };
        add(u"http", 80, flags_HttpUri);
        add(u"https", 443, flags_HttpsUri);
        add(u"ws", 80, flags_WsUri);
        add(u"wss", 443, flags_WssUri);
        add(u"ftp", 21, flags_FtpUri);
        add(u"file", -1, flags_FileUri);
        add(u"gopher", 70, flags_GopherUri);
        add(u"nntp", 119, flags_NntpUri);
        add(u"news", -1, flags_NewsUri);
        add(u"mailto", 25, flags_MailToUri);
        add(u"uuid", -1, flags_UuidUri);
        add(u"telnet", 23, flags_TelnetUri);
        add(u"ldap", 389, flags_LdapUri);
        add(u"net.tcp", 808, flags_NetTcpUri);
        add(u"net.pipe", -1, flags_NetPipeUri);
        add(u"vsmacros", -1, flags_VsMacrosUri);
        unix_file = std::make_shared<const ConfigUriSyntax>(
            ConfigUriSyntax{table.at(u"file")->scheme, -1, flags_UnixFileUri});
    }
};
const Builtins& builtins() {
    static const Builtins value;
    return value;
}
}
struct NativeConfigUriSyntaxCatalog::State {
    std::mutex mutex;
    std::unordered_map<std::u16string, ConfigUriSyntaxPtr> temporary;
};
NativeConfigUriSyntaxCatalog::NativeConfigUriSyntaxCatalog() : state_(std::make_shared<State>()) {
}
ConfigUriSyntaxPtr NativeConfigUriSyntaxCatalog::get(std::u16string_view name) const {
    auto found = builtins().table.find(std::u16string(name));
    if (found != builtins().table.end())
        return found->second;
    std::lock_guard lock(state_->mutex);
    auto temporary = state_->temporary.find(std::u16string(name));
    return temporary == state_->temporary.end() ? ConfigUriSyntaxPtr{} : temporary->second;
}
ConfigUriSyntaxPtr NativeConfigUriSyntaxCatalog::find_or_fetch(ConfigUriText name) {
    if (!name)
        throw std::invalid_argument("URI syntax catalog requires a validated non-null scheme");
    if (auto existing = get(*name))
        return existing;
    // Source FindOrFetch does not recheck after entering s_table's lock.
    // Keep that publication order while protecting the C++ map accesses.
    std::lock_guard lock(state_->mutex);
    if (state_->temporary.size() >= 512)
        state_->temporary.clear();
    auto value = std::make_shared<const ConfigUriSyntax>(ConfigUriSyntax{std::move(name), -1, flags_unknown});
    state_->temporary[*value->scheme] = value;
    return value;
}
ConfigUriSyntaxPtr NativeConfigUriSyntaxCatalog::unix_file() {
    return builtins().unix_file;
}
NativeConfigUriSyntaxCatalog& native_config_uri_syntax_catalog() {
    static NativeConfigUriSyntaxCatalog value;
    return value;
}
}
