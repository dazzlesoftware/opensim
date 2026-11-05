// Native startup-invariant providers for consumed HttpClient cookie state.
#pragma once
#include "opensim/framework/config_http_cookies.hpp"
#include "opensim/framework/invariant_datetime_context.hpp"
namespace opensim::framework {
std::uint32_t config_http_cookie_marvin_hash(std::u16string_view,std::uint64_t seed);
// Independent native process entropy is explicit; tied eviction can differ across
// processes just as CLR randomized string hashes do. Exact tests supply CLR seed.
std::uint64_t native_config_http_cookie_hash_seed();
ConfigCookieText native_config_http_cookie_fqdn_domain();
struct ConfigHttpCookieNativeOptions {
 std::shared_ptr<NativeInvariantDateTimeContext> context;
 std::optional<ConfigCookieText> fqdn_snapshot;
 std::optional<std::uint64_t> hash_seed;
 std::function<std::uint32_t(ConfigCookieText)> domain_hash;
 std::function<std::optional<std::int32_t>(ConfigCookieText)> current_int_parser;
 std::function<bool(std::u16string_view,std::u16string_view)> ordinal_equal;
 std::function<ConfigCookieText(ConfigCookieText)> lower_invariant;
};
// Default CurrentCulture is explicitly the original startup invariant culture;
// alternate current numeric/casing modes must supply their corresponding callbacks.
// Requires the separately source-proven fixed-style23 DateTime parser extension.
std::shared_ptr<ConfigHttpCookiesBackend> native_invariant_config_http_cookies_backend(ConfigHttpCookieNativeOptions={});
}
