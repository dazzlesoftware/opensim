// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/login_agent_factory.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/sdk_boolean_ignorables.hpp"
#include "opensim/services/service_prefix_boundaries.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::services {
bool LoginAgentFactory::is_service_key(std::u16string_view key) {
    constexpr std::u16string_view prefix = u"SRV_";
    std::size_t matched = 0;
    for (std::size_t i = 0; i < key.size(); ++i) {
        char32_t point = key[i];
        if (point >= 0xd800 && point <= 0xdbff && i + 1 < key.size() && key[i + 1] >= 0xdc00 &&
            key[i + 1] <= 0xdfff)
            point = 0x10000 + ((point - 0xd800) << 10) + (key[++i] - 0xdc00);
        auto& ranges = network::detail::boolean_ignorables;
        auto ignored = std::lower_bound(ranges.begin(), ranges.end(), point,
                                        [](auto range, char32_t n) { return range.second < n; });
        if (ignored != ranges.end() && ignored->first <= point)
            continue;
        if (matched == prefix.size()) {
            auto& boundaries = detail::service_prefix_boundaries;
            auto blocked = std::lower_bound(boundaries.begin(), boundaries.end(), point,
                                            [](auto range, char32_t n) { return range.second < n; });
            return blocked == boundaries.end() || blocked->first > point;
        }
        if (point != static_cast<char32_t>(prefix[matched]))
            return false;
        ++matched;
    }
    return matched == prefix.size();
}
LoginAgentFactory::LoginAgentFactory(Config config, std::optional<std::string> gatekeeper, Store store,
                                     CapsPath caps)
    : gatekeeper_(std::move(gatekeeper)), store_(std::move(store)), caps_(std::move(caps)) {
    auto values = std::make_shared<Config>(std::move(config));
    keys_ = [values] {
        std::vector<std::string> result;
        for (auto& [key, value] : *values)
            result.push_back(key);
        return result;
    };
    read_ = [values](const std::string& key, const std::string& fallback) {
        for (auto& [name, value] : *values)
            if (name == key)
                return value;
        return fallback;
    };
    if (!caps_)
        caps_ = []() { return framework::Uuid::random().str(); };
}
LoginAgentFactory::LoginAgentFactory(Keys keys, Read read, std::optional<std::string> gatekeeper, Store store,
                                     CapsPath caps)
    : keys_(std::move(keys)), read_(std::move(read)), gatekeeper_(std::move(gatekeeper)),
      store_(std::move(store)), caps_(std::move(caps)) {
    if (!keys_ || !read_)
        throw std::invalid_argument("Login configuration callbacks are missing");
    if (!caps_)
        caps_ = []() { return framework::Uuid::random().str(); };
}
std::shared_ptr<framework::AgentCircuitData>
LoginAgentFactory::make_agent(UserAccount& account, const framework::AvatarAppearance* appearance,
                              const LoginAgentDetails& details) const {
    auto circuit = std::make_shared<framework::AgentCircuitData>();
    circuit->agent_id = framework::Uuid(account.principal_id);
    circuit->appearance = appearance ? std::make_shared<framework::AvatarAppearance>(appearance)
                                     : std::make_shared<framework::AvatarAppearance>();
    circuit->caps_path = caps_();
    circuit->child = false;
    circuit->children_cap_seeds = std::make_shared<framework::AgentCircuitData::ChildrenSeeds>();
    circuit->circuit_code = details.circuit;
    circuit->first_name = account.first_name;
    circuit->last_name = account.last_name;
    circuit->secure_session_id = details.secure_session;
    circuit->session_id = details.session;
    circuit->start_position = details.position;
    circuit->ip_address = details.ip_address;
    circuit->viewer(details.viewer);
    circuit->channel = details.channel;
    circuit->mac = details.mac;
    circuit->id0 = details.id0;
    set_service_urls(*circuit, account);
    return circuit;
}
void LoginAgentFactory::set_service_urls(framework::AgentCircuitData& circuit, UserAccount& account) const {
    using Urls = framework::AgentCircuitData::ServiceUrls;
    circuit.service_urls = std::make_shared<Urls>();
    if (!account.service_urls)
        return;
    auto original = account.service_urls;
    for (auto [key, value] : *original)
        if (auto s = std::any_cast<std::string>(&value))
            (*circuit.service_urls)[key] = s->ends_with('/') ? *s : *s + '/';
    auto keys = keys_();
    if (keys.empty())
        return;
    bool changed = false;
    auto differs = [&](const std::string& key, const std::string& value) {
        auto found = account.service_urls->find(key);
        if (found == account.service_urls->end())
            return true;
        if (!found->second.has_value() || found->second.type() == typeid(std::nullptr_t))
            return true;
        auto text = std::any_cast<std::string>(&found->second);
        if (!text)
            throw std::invalid_argument("Service URL override requires a string value");
        return value != *text;
    };
    for (auto& key : keys) {
        auto units = framework::utf16_replacing(key);
        if (!is_service_key(units))
            continue;
        auto name = framework::utf8(std::u16string_view(units).substr(4));
        auto url = read_(key, "");
        if (!url.ends_with('/'))
            url += '/';
        if (differs(name, url)) {
            (*account.service_urls)[name] = url;
            changed = true;
        }
        (*circuit.service_urls)[name] = url;
    }
    if (gatekeeper_ && !gatekeeper_->empty() && differs("GatekeeperURI", *gatekeeper_)) {
        (*account.service_urls)["GatekeeperURI"] = *gatekeeper_;
        changed = true;
    }
    if (changed) {
        if (!store_)
            throw std::invalid_argument("User account persistence callback is missing");
        (void)store_(account);
    }
}
}
