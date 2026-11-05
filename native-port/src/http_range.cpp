// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/http_range.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/user_account.hpp"
namespace opensim::server {
bool try_parse_http_range(const std::optional<std::string>& header, int32_t& start, int32_t& end) {
    start = end = 0;
    if (!header || framework::trim_utf8(*header).empty())
        return false;
    if (framework::invariant_starts_with(*header, "bytes=")) {
        auto units = framework::utf16_replacing(*header);
        if (units.size() < 6)
            throw std::out_of_range("HTTP range prefix");
        auto values = framework::utf8(std::u16string_view(units).substr(6));
        auto separator = values.find('-');
        if (separator != values.npos && values.find('-', separator + 1) == values.npos) {
            auto raw = framework::trim_utf8(values.substr(0, separator));
            if (!raw.empty() && !services::parse_account_integer(raw, start))
                return false;
            if (start < 0)
                return false;
            raw = framework::trim_utf8(values.substr(separator + 1));
            if (raw.empty()) {
                end = -1;
                return true;
            }
            if (services::parse_account_integer(raw, end))
                return end > 0;
        }
    }
    start = end = 0;
    return false;
}
}
