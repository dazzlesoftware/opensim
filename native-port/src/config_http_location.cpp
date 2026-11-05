#include "opensim/framework/config_http_location.hpp"
#include "opensim/framework/config_uri_components.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::framework {
namespace {
[[noreturn]] void null_reference() {
    throw ConfigUriComponentsError("System.NullReferenceException",
                                   u"Object reference not set to an instance of an object.", 0x80004003);
}
bool secure(const ConfigUriText &value) { return value && (*value == u"https" || *value == u"wss"); }
ConfigUriText fragment(const ConfigUriObjectPtr &uri) {
    if (!uri)
        null_reference();
    return uri->fragment();
}
ConfigUriText scheme(const ConfigUriObjectPtr &uri) {
    if (!uri)
        null_reference();
    return uri->scheme();
}
bool empty(const ConfigUriText &value) { return !value || value->empty(); }
}
ConfigUriText config_http_location_decode_utf8(ConfigUriText input) {
    if (!input || input->empty() || std::all_of(input->begin(), input->end(), unicode_whitespace))
        return input;
    auto found = std::find_if(input->begin(), input->end(), [](auto c) { return c > 127; });
    if (found == input->end() || std::any_of(found, input->end(), [](auto c) { return c > 255; }))
        return input;
    std::string bytes;
    bytes.reserve(input->size());
    for (auto c : *input)
        bytes += static_cast<char>(c);
    try {
        return std::make_shared<const std::u16string>(utf16(bytes));
    } catch (const std::invalid_argument &) {
        return input;
    }
}
ConfigHttpLocationAdapter::ConfigHttpLocationAdapter(ConfigUriObjectBackend backend)
    : backend_(std::move(backend)) {
    if (!backend_.try_create || !backend_.combine_uri || !backend_.with_fragment)
        throw std::invalid_argument(
            "Typed Location requires URI TryCreate, Uri-object combine and UriBuilder fragment providers");
}
ConfigUriObjectPtr ConfigHttpLocationAdapter::parse_value(ConfigUriText value) const {
    if (empty(value))
        return {};
    auto uri = backend_.try_create(value, ConfigUriObjectKind::RelativeOrAbsolute);
    if (!uri)
        uri = backend_.try_create(config_http_location_decode_utf8(value),
                                  ConfigUriObjectKind::RelativeOrAbsolute);
    return uri;
}
ConfigUriObjectPtr ConfigHttpLocationAdapter::redirect(ConfigUriObjectPtr request, std::int32_t status,
                                                       const std::vector<ConfigUriText> &values) const {
    auto code = static_cast<std::uint32_t>(status);
    if (code - 300u > 3u && code - 307u > 1u)
        return {};
    if (values.empty())
        return {};
    auto uri = parse_value(values.front());
    if (!uri)
        return {};
    if (!uri->is_absolute())
        uri = backend_.combine_uri(request, uri);
    auto inherited = fragment(request);
    if (!empty(inherited) && empty(fragment(uri)))
        uri = backend_.with_fragment(uri, inherited);
    if (secure(scheme(request)) && !secure(scheme(uri)))
        return {};
    return uri;
}
}
