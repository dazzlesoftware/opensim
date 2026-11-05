// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/account_service_urls.hpp"
#include "opensim/network/service_protocol.hpp"
namespace opensim::services {
std::shared_ptr<framework::AgentCircuitData::ServiceUrls> decode_account_service_urls(std::string_view text) {
    auto result = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
    std::size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find(' ', start);
        auto entry = text.substr(start, end == std::string_view::npos ? text.size() - start : end - start);
        auto equals = entry.find('=');
        if (equals != std::string_view::npos && entry.find('=', equals + 1) == std::string_view::npos)
            (*result)[network::service_url_decode(entry.substr(0, equals))] =
                network::service_url_decode(entry.substr(equals + 1));
        if (end == std::string_view::npos)
            break;
        start = end + 1;
    }
    return result;
}
std::string
encode_account_service_urls(const std::shared_ptr<framework::AgentCircuitData::ServiceUrls>& urls) {
    if (!urls)
        throw std::invalid_argument("Null account service URL dictionary");
    auto enumerated = urls;
    std::string result;
    std::size_t index = 1;
    for (auto [key, value] : *enumerated) {
        result += network::service_url_encode(key);
        result += '=';
        if (!value.has_value() || value.type() == typeid(std::nullptr_t))
            throw std::invalid_argument("Null account service URL value");
        if (auto p = std::any_cast<const char*>(&value); p && !*p)
            throw std::invalid_argument("Null account service URL string");
        result += network::service_url_encode(framework::service_url_object_text(value));
        if (!urls)
            throw std::invalid_argument("Account URL dictionary replaced with null");
        if (index++ < urls->size())
            result += ' ';
    }
    return result;
}
}
