// Consumed System.Net.CookieContainer/CookieParser HTTP closure, .NET8.0.31.
// .NET Foundation MIT license: third_party/dotnet_collation/LICENSE.
#pragma once
#include "opensim/framework/config_cookie_uri.hpp"
#include "opensim/framework/asset_base.hpp"
#include <vector>
#include <exception>
namespace opensim::framework {
using ConfigCookieText = ConfigUriText;
class ConfigHttpCookieError : public std::runtime_error {
  public:
    ConfigHttpCookieError(std::string type, std::u16string message, std::uint32_t hresult,
                          std::optional<std::u16string> parameter = {}, std::exception_ptr inner = {});
    std::string source_type;
    std::u16string message;
    std::uint32_t hresult;
    std::optional<std::u16string> parameter;
    std::exception_ptr inner;
};
struct ConfigHttpCookiesBackend {
    std::function<AssetDateTime()> now;
    std::function<AssetDateTime(AssetDateTime)> to_local_time;
    // Exactly DateTime.TryParse(InvariantCulture, AllowWhiteSpaces|AdjustToUniversal=23).
    std::function<std::optional<AssetDateTime>(ConfigCookieText)> parse_expiry;
    // Source int.TryParse CurrentCulture (startup provider may explicitly invariant).
    std::function<std::optional<std::int32_t>(ConfigCookieText)> try_int;
    std::function<bool(char16_t)> white_space;
    std::function<bool(std::u16string_view, std::u16string_view)> ordinal_ignore_case_equal;
    std::function<ConfigCookieText(ConfigCookieText)> lower_invariant;
    // Captured source CreateFqdnMyDomain output: empty, or '.' + HostInformation.DomainName.
    ConfigCookieText fqdn_my_domain;
    // Actual .NET String.GetHashCode process seed, or explicit source-test comparer.
    // Drives exact Hashtable buckets, resize/removal and tied cross-domain eviction.
    std::function<std::uint32_t(ConfigCookieText)> domain_hash;
};
struct ConfigHttpCookieView {
    ConfigCookieText name, value, domain, path, port, comment;
    std::int32_t version = 0, variant = 0;
    bool secure = false, http_only = false, discard = false, domain_implicit = true, path_implicit = true,
         port_implicit = true;
    bool quoted_domain = false, quoted_version = false;
    AssetDateTime expires, created;
    std::vector<std::int32_t> ports;
};
class ConfigHttpCookies {
    struct State;
    std::shared_ptr<State> state_;

  public:
    static constexpr int DefaultCapacity = 300, DefaultPerDomain = 20, DefaultSize = 4096;
    explicit ConfigHttpCookies(std::shared_ptr<ConfigHttpCookiesBackend>);
    // Additional source constructor limit surface, useful for exhaustive aging proof.
    ConfigHttpCookies(std::shared_ptr<ConfigHttpCookiesBackend>, int capacity, int per_domain, int max_size);
    void set_cookies(std::shared_ptr<ConfigUriComponents>, ConfigCookieText);
    // Actual HttpClient CookieHelper: public SetCookies, catch CookieException PER header.
    void receive(std::shared_ptr<ConfigUriComponents>, const std::vector<ConfigCookieText>&);
    ConfigCookieText header(std::shared_ptr<ConfigUriComponents>);
    std::vector<ConfigHttpCookieView> cookies(std::shared_ptr<ConfigUriComponents>);
    void set_cookies_for_uri(std::shared_ptr<ConfigCookieUri>, ConfigCookieText);
    void receive_for_uri(std::shared_ptr<ConfigCookieUri>, const std::vector<ConfigCookieText>&);
    ConfigCookieText header_for_uri(std::shared_ptr<ConfigCookieUri>);
    std::vector<ConfigHttpCookieView> cookies_for_uri(std::shared_ptr<ConfigCookieUri>);
    std::int32_t count() const;
};
}
