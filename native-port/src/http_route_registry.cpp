// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/http_route_registry.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/service_protocol.hpp"
#include <stdexcept>
namespace opensim::server {
namespace {
const std::string& key(const HttpRouteRegistry::Name& name) {
    if (!name)
        throw std::invalid_argument("Null HTTP route key");
    return *name;
}
template <class T> T& require(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::invalid_argument("Null HTTP route handler");
    return *value;
}
}
template <class V> std::vector<std::string> HttpRouteRegistry::keys(const Map<V>& map) {
    std::lock_guard lock(map.mutex);
    std::vector<std::string> result;
    for (const auto& item : map.values)
        result.push_back(item.first);
    return result;
}
template <class V> bool HttpRouteRegistry::get(const Map<V>& map, const Name& name, V& value) {
    const auto& k = key(name);
    std::lock_guard lock(map.mutex);
    auto i = map.values.find(k);
    if (i == map.values.end()) {
        value = nullptr;
        return false;
    }
    value = i->second;
    return true;
}
template <class V> bool HttpRouteRegistry::add(Map<V>& map, const Name& name, const V& value) {
    const auto& k = key(name);
    std::lock_guard lock(map.mutex);
    return map.values.emplace(k, value).second;
}
template <class V> bool HttpRouteRegistry::erase(Map<V>& map, const Name& name) {
    const auto& k = key(name);
    V removed;
    {
        std::lock_guard lock(map.mutex);
        auto i = map.values.find(k);
        if (i == map.values.end())
            return false;
        removed = i->second;
        map.values.erase(i);
    }
    return true;
}
void HttpRouteRegistry::add_stream(Stream handler) {
    auto path = require(handler).path();
    if (key(path) == "/") {
        auto method = handler->http_method();
        if (!method)
            throw std::invalid_argument("Null root HTTP method");
        if (*method == "GET") {
            std::lock_guard lock(root_mutex_);
            root_ = handler;
        }
        return;
    }
    auto method = handler->http_method();
    auto route = handler->path();
    add(streams_, handler_key(method, route), handler);
}
void HttpRouteRegistry::add_generic_stream(Stream handler) {
    auto path = require(handler).path();
    if (!path || framework::trim_utf8(*path).empty())
        return;
    add(streams_, handler->path(), handler);
}
void HttpRouteRegistry::add_simple(Simple handler, bool variable) {
    auto path = require(handler).path();
    add(variable ? simple_variable_ : simple_, path, handler);
}
bool HttpRouteRegistry::add_poll(const Name& url, const Poll& handler) {
    return add(poll_, url, handler);
}
bool HttpRouteRegistry::add_poll(Poll handler) {
    return add(poll_, require(handler).url, handler);
}
bool HttpRouteRegistry::add_poll_variable(Poll handler) {
    return add(poll_variable_, require(handler).url, handler);
}
void HttpRouteRegistry::remove_stream(const Name& method, const Name& path) {
    if (erase(streams_, path))
        return;
    erase(streams_, handler_key(method, path));
}
void HttpRouteRegistry::remove_stream(const Name& path) {
    erase(streams_, path);
}
void HttpRouteRegistry::remove_simple(const Name& path) {
    if (erase(simple_, path))
        return;
    erase(simple_variable_, path);
}
void HttpRouteRegistry::remove_poll(const Name& path) {
    if (!erase(poll_, path))
        erase(poll_variable_, path);
}
bool HttpRouteRegistry::try_stream(const Name& name, Stream& handler) const {
    if (get(streams_, name, handler))
        return true;
    std::optional<std::string> best;
    std::size_t length = 0;
    {
        std::lock_guard scan(stream_scan_mutex_);
        for (const auto& pattern : keys(streams_))
            if (prefix_(key(name), pattern)) {
                auto units = framework::utf16_replacing(pattern).size();
                if (!best || units > length) {
                    best = pattern;
                    length = units;
                }
            }
    }
    if (best) {
        std::lock_guard lock(streams_.mutex);
        handler = streams_.values.at(*best);
        return true;
    }
    handler = nullptr;
    return false;
}
bool HttpRouteRegistry::try_simple(const Name& path, Simple& handler) const {
    if (get(simple_, path, handler))
        return true;
    handler = nullptr;
    auto units = framework::utf16_replacing(key(path));
    if (units.size() < 3)
        return false;
    auto at = units.find(u'/', 2);
    if (at == units.npos || at == units.size() - 1)
        return false;
    return get(simple_variable_, framework::utf8(std::u16string_view(units).substr(0, at)), handler);
}
bool HttpRouteRegistry::try_poll(const Name& path, Poll& handler) const {
    if (get(poll_, path, handler))
        return true;
    bool any;
    {
        std::lock_guard lock(poll_variable_.mutex);
        any = !poll_variable_.values.empty();
    }
    auto units = framework::utf16_replacing(key(path));
    if (any && units.size() >= 45) {
        auto at = units.find(u'/', 44);
        if (at == units.npos)
            return get(poll_variable_, path, handler);
        return get(poll_variable_, framework::utf8(std::u16string_view(units).substr(0, at)), handler);
    }
    handler = nullptr;
    return false;
}
HttpRouteRegistry::Stream HttpRouteRegistry::root_get() const {
    std::lock_guard lock(root_mutex_);
    return root_;
}
std::vector<std::string> HttpRouteRegistry::stream_keys() const {
    return keys(streams_);
}
std::vector<std::string> HttpRouteRegistry::simple_keys() const {
    auto result = keys(simple_);
    auto extra = keys(simple_variable_);
    result.insert(result.end(), extra.begin(), extra.end());
    return result;
}
std::vector<std::string> HttpRouteRegistry::poll_keys() const {
    auto result = keys(poll_);
    auto extra = keys(poll_variable_);
    result.insert(result.end(), extra.begin(), extra.end());
    return result;
}
network::HttpResponse RegistryCapabilityListener::dispatch(const network::HttpRequest& request,
                                                           const network::HttpServer::Handler& fallback,
                                                           const PollRequest& poll_request) const {
    auto path = request.path;
    if (path.size() > 1 && path.back() == '/')
        path.pop_back();
    auto poll_path = path;
    if (!request.raw_target.empty()) {
        const auto raw = std::string_view(request.raw_target);
        const auto raw_path = raw.substr(0, raw.find_first_of("?#"));
        if (raw_path != request.path || raw_path.find_first_of(".\\") != raw_path.npos) {
            if (const auto target = network::canonical_service_request_target(raw)) {
                poll_path = target->path;
                if (poll_path.size() > 1 && poll_path.back() == '/')
                    poll_path.pop_back();
            }
        }
    }
    HttpRouteRegistry::Poll poll;
    if (routes.try_poll(poll_path, poll)) {
        if (!poll)
            throw std::invalid_argument("Null poll route");
        poll->requests_received++;
        if (poll_request)
            return poll_request(poll, request);
        network::HttpResponse response;
        response.status = 503;
        response.body = "Poll service unavailable\n";
        return response;
    }
    if (request.method == "OPTIONS") {
        network::HttpResponse response;
        response.headers = {{"Access-Control-Allow-Origin", "*"},
                            {"Access-Control-Allow-Methods", "GET, POST, DELETE, PUT, OPTIONS"},
                            {"Access-Control-Allow-Headers", "Content-Type"}};
        return response;
    }
    if (request.path == "/") {
        auto root = routes.root_get();
        if (root && request.method == "GET") {
            network::HttpResponse response;
            response.status = 404;
            if (root->streamed()) {
                response = root->handle_stream(request.path, request);
                response.status = 200;
            }
            return response;
        }
        if (fallback)
            return fallback(request);
        network::HttpResponse response;
        response.status = 404;
        return response;
    }
    HttpRouteRegistry::Simple simple;
    if (routes.try_simple(path, simple))
        return require(simple).handle(request);
    HttpRouteRegistry::Stream stream;
    if (routes.try_stream(HttpRouteRegistry::handler_key(request.method, path), stream))
        return require(stream).handle_stream(path, request);
    if (fallback)
        return fallback(request);
    network::HttpResponse response;
    response.status = 404;
    return response;
}
}
