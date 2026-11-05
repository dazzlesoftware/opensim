// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/caps_handlers.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
#include <unordered_set>
namespace opensim::server {
namespace {
const std::string& key(const CapsHandlers::Name& name) {
    if (!name)
        throw std::invalid_argument("Null capability name");
    return *name;
}
CapabilityListener& listener(const std::shared_ptr<CapabilityListener>& value) {
    if (!value)
        throw std::invalid_argument("Null capability listener");
    return *value;
}
template <class T> std::optional<std::string> path(const std::shared_ptr<T>& handler) {
    if (!handler)
        throw std::invalid_argument("Null capability handler");
    return handler->path();
}
void add_xml(const std::string& name, const std::string& value, std::string* output) {
    if (!output)
        throw std::invalid_argument("Null XML buffer");
    *output += "<key>";
    for (char16_t c : framework::utf16_replacing(name))
        *output += static_cast<char>(c & 255);
    *output += "</key>";
    if (value.empty()) {
        *output += "<string />";
        return;
    }
    *output += "<string>";
    for (char c : value) {
        switch (c) {
        case '&':
            *output += "&amp;";
            break;
        case '<':
            *output += "&lt;";
            break;
        case '>':
            *output += "&gt;";
            break;
        case '\"':
            *output += "&quot;";
            break;
        case '\'':
            *output += "&apos;";
            break;
        default:
            *output += c;
        }
    }
    *output += "</string>";
}
}
CapsHandlers::CapsHandlers(std::shared_ptr<CapabilityListener> value, Name host, std::uint32_t port)
    : listener_(std::move(value)) {
    base_url_ = (listener_ && listener_->use_ssl() ? "https://" : "http://") + host.value_or("") + ":" +
                std::to_string(port);
}
void CapsHandlers::remove_stream_entries(const std::string& name) {
    for (const auto& method : {"POST", "PUT", "GET", "DELETE"}) {
        auto target = path(streams_.at(name));
        listener(listener_).remove_stream(method, target);
    }
    streams_.erase(name);
}
void CapsHandlers::remove(const Name& name) {
    const auto& k = key(name);
    {
        std::lock_guard lock(streams_mutex_);
        if (streams_.contains(k))
            remove_stream_entries(k);
    }
    std::shared_ptr<CapabilitySimpleHandler> handler;
    bool found = false;
    {
        std::lock_guard lock(simple_mutex_);
        auto it = simple_.find(k);
        if (it != simple_.end()) {
            handler = it->second;
            simple_.erase(it);
            found = true;
        }
    }
    if (found) {
        auto target = path(handler);
        listener(listener_).remove_simple(target);
    }
}
void CapsHandlers::add_simple(const Name& name, std::shared_ptr<CapabilitySimpleHandler> handler,
                              bool add_to_listener) {
    if (contains(name))
        remove(name);
    const auto& k = key(name);
    bool added;
    {
        std::lock_guard lock(simple_mutex_);
        added = simple_.emplace(k, handler).second;
    }
    if (added && add_to_listener)
        listener(listener_).add_simple(handler);
}
bool CapsHandlers::contains(const Name& name) const {
    const auto& k = key(name);
    {
        std::lock_guard lock(streams_mutex_);
        if (streams_.contains(k))
            return true;
    }
    std::lock_guard lock(simple_mutex_);
    return simple_.contains(k);
}
std::shared_ptr<CapabilityStreamHandler> CapsHandlers::get(const Name& name) const {
    std::lock_guard lock(streams_mutex_);
    return streams_.at(key(name));
}
void CapsHandlers::set(const Name& name, std::shared_ptr<CapabilityStreamHandler> handler) {
    std::lock_guard lock(streams_mutex_);
    const auto& k = key(name);
    if (streams_.contains(k))
        remove_stream_entries(k);
    if (!handler)
        return;
    streams_[k] = handler;
    listener(listener_).add_stream(handler);
}
std::vector<std::pair<std::string, std::shared_ptr<CapabilitySimpleHandler>>>
CapsHandlers::simple_snapshot() const {
    std::lock_guard lock(simple_mutex_);
    return {simple_.begin(), simple_.end()};
}
std::shared_ptr<CapabilitySimpleHandler> CapsHandlers::find_simple(const std::string& name,
                                                                   bool& exists) const {
    std::lock_guard lock(simple_mutex_);
    auto found = simple_.find(name);
    exists = found != simple_.end();
    return exists ? found->second : nullptr;
}
std::vector<std::string> CapsHandlers::caps() const {
    std::lock_guard lock(streams_mutex_);
    std::vector<std::string> names;
    for (const auto& item : streams_)
        names.push_back(item.first);
    for (const auto& item : simple_snapshot())
        names.push_back(item.first);
    return names;
}
CapsHandlers::Paths CapsHandlers::details(bool exclude_seed, const Requested* requested) const {
    Paths result;
    std::lock_guard lock(streams_mutex_);
    if (!requested) {
        for (const auto& item : simple_snapshot())
            result[item.first] = base_url_ + path(item.second).value_or("");
        for (const auto& entry : streams_) {
            auto name = entry.first;
            auto handler = entry.second;
            result[name] = base_url_ + path(handler).value_or("");
        }
        return result;
    }
    for (std::size_t i = 0; i < requested->size(); ++i) {
        auto name = (*requested)[i];
        if (exclude_seed && name == Name("SEED"))
            continue;
        const auto& k = key(name);
        bool exists;
        auto simple = find_simple(k, exists);
        if (exists) {
            result[k] = base_url_ + path(simple).value_or("");
            continue;
        }
        auto found = streams_.find(k);
        if (found != streams_.end()) {
            auto handler = found->second;
            result[k] = base_url_ + path(handler).value_or("");
        }
    }
    return result;
}
CapsHandlers::Paths CapsHandlers::local_paths() const {
    Paths result;
    std::lock_guard lock(streams_mutex_);
    for (const auto& item : simple_snapshot())
        result[item.first] = path(item.second);
    for (const auto& entry : streams_) {
        auto name = entry.first;
        auto handler = entry.second;
        result[name] = path(handler);
    }
    return result;
}
void CapsHandlers::append_details_xml(const Requested* requested, std::string* output) const {
    std::lock_guard lock(streams_mutex_);
    if (!requested)
        return;
    std::unordered_set<Name> visited;
    for (const auto& name : *requested) {
        if (!visited.insert(name).second)
            continue;
        const auto& k = key(name);
        bool exists;
        auto simple = find_simple(k, exists);
        if (exists) {
            auto target = base_url_ + path(simple).value_or("");
            add_xml(k, target, output);
        } else {
            auto found = streams_.find(k);
            if (found != streams_.end()) {
                auto handler = found->second;
                auto target = base_url_ + path(handler).value_or("");
                add_xml(k, target, output);
            }
        }
    }
}
CapsHandlers::Streams CapsHandlers::streams() const {
    std::lock_guard lock(streams_mutex_);
    return streams_;
}
void CapsHandlers::append_llsd_element(const std::string& name, const Name& value, std::string* output) {
    add_xml(name, value.value_or(""), output);
}
}
