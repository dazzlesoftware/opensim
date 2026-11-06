// Consumed .NET8 UriHeaderParser and RedirectHandler.GetUriForRedirect.
#pragma once
#include "opensim/framework/config_uri_object.hpp"
#include <vector>
namespace opensim::framework {
ConfigUriText config_http_location_decode_utf8(ConfigUriText);
class ConfigHttpLocationAdapter {
    ConfigUriObjectBackend backend_;

  public:
    explicit ConfigHttpLocationAdapter(ConfigUriObjectBackend);
    // UriHeaderParser.TryParseValue(index0); empty/null are not URI values. Failed
    // creation is retried even when strict UTF8 recovery returns the original text.
    ConfigUriObjectPtr parse_value(ConfigUriText) const;
    // Exact consumed single Location getter and redirect selection. This does not
    // send a request, enforce the redirect count, or imply a supported transport.
    ConfigUriObjectPtr redirect(ConfigUriObjectPtr request_uri, std::int32_t status,
                                const std::vector<ConfigUriText>& raw_locations) const;
};
}
