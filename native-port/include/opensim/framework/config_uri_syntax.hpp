// Source .NET runtime8.0.31 UriParser built-in/V1 unknown syntax.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/config_uri.hpp"
#include <cstdint>
namespace opensim::framework {
enum class ConfigUriSyntaxFlags : std::uint64_t {
    None = 0ull,
    MustHaveAuthority = 1ull,
    OptionalAuthority = 2ull,
    MayHaveUserInfo = 4ull,
    MayHavePort = 8ull,
    MayHavePath = 16ull,
    MayHaveQuery = 32ull,
    MayHaveFragment = 64ull,
    AllowEmptyHost = 128ull,
    AllowUncHost = 256ull,
    AllowDnsHost = 512ull,
    AllowIPv4Host = 1024ull,
    AllowIPv6Host = 2048ull,
    AllowAnInternetHost = 3584ull,
    AllowAnyOtherHost = 4096ull,
    FileLikeUri = 8192ull,
    MailToLikeUri = 16384ull,
    V1_UnknownUri = 65536ull,
    SimpleUserSyntax = 131072ull,
    BuiltInSyntax = 262144ull,
    ParserSchemeOnly = 524288ull,
    AllowDOSPath = 1048576ull,
    PathIsRooted = 2097152ull,
    ConvertPathSlashes = 4194304ull,
    CompressPath = 8388608ull,
    CanonicalizeAsFilePath = 16777216ull,
    UnEscapeDotsAndSlashes = 33554432ull,
    AllowIdn = 67108864ull,
    AllowIriParsing = 268435456ull,
};
constexpr ConfigUriSyntaxFlags operator|(ConfigUriSyntaxFlags a, ConfigUriSyntaxFlags b) {
    return static_cast<ConfigUriSyntaxFlags>(static_cast<std::uint64_t>(a) | static_cast<std::uint64_t>(b));
}
struct ConfigUriSyntax {
    ConfigUriText scheme;
    std::int32_t default_port;
    std::uint64_t flags;
};
using ConfigUriSyntaxPtr = std::shared_ptr<const ConfigUriSyntax>;
// Input is the already validated lower-case scheme passed by CheckSchemeSyntax.
// No public UriParser registration/custom-parser substitute is supplied here.
class NativeConfigUriSyntaxCatalog {
    struct State;
    std::shared_ptr<State> state_;

  public:
    NativeConfigUriSyntaxCatalog();
    ConfigUriSyntaxPtr get(std::u16string_view) const;
    ConfigUriSyntaxPtr find_or_fetch(ConfigUriText);
    static ConfigUriSyntaxPtr unix_file();
};
NativeConfigUriSyntaxCatalog& native_config_uri_syntax_catalog();
}
