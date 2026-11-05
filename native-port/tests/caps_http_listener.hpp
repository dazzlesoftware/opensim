#pragma once
// Socket fixture: exact-path listener adapter, not a BaseHttpServer replacement.
#include "opensim/services/caps.hpp"
#include <atomic>
#include "opensim/network/osd_json.hpp"
#include <iostream>
#include <map>
using namespace opensim;
struct Handler : server::CapabilityStreamHandler, server::CapabilitySimpleHandler {
    std::string route, verb, label;
    Handler(std::string p, std::string v, std::string l)
        : route(std::move(p)), verb(std::move(v)), label(std::move(l)) {
    }
    std::optional<std::string> path() const override {
        return route;
    }
    std::string method() const override {
        return verb;
    }
    network::HttpResponse handle(const network::HttpRequest& r) override {
        network::HttpResponse response;
        response.body = label + ":" + r.method + ":" + r.body;
        return response;
    }
};
struct Listener : services::CapabilityRuntimeListener {
    std::mutex mutex;
    std::map<std::pair<std::string, std::string>, std::shared_ptr<server::CapabilityStreamHandler>> streams;
    std::map<std::string, std::shared_ptr<server::CapabilitySimpleHandler>> simple;
    std::map<std::string, std::shared_ptr<services::CapabilityPollHandler>> polls;
    std::uint32_t port() const override {
        return 0;
    }
    std::uint32_t ssl_port() const override {
        return 0;
    }
    std::optional<std::string> ssl_common_name() const override {
        return {};
    }
    void add_poll(const std::shared_ptr<services::CapabilityPollHandler>& h) override {
        std::lock_guard lock(mutex);
        polls[h->url.value()] = h;
    }
    void remove_poll(const std::optional<std::string>& url) override {
        std::lock_guard lock(mutex);
        polls.erase(url.value());
    }
    bool use_ssl() const override {
        return false;
    }
    void add_stream(const std::shared_ptr<server::CapabilityStreamHandler>& h) override {
        std::lock_guard lock(mutex);
        streams[{h->method(), h->path().value()}] = h;
    }
    void remove_stream(const std::string& m, const std::optional<std::string>& p) override {
        std::lock_guard lock(mutex);
        streams.erase({m, p.value()});
    }
    void add_simple(const std::shared_ptr<server::CapabilitySimpleHandler>& h) override {
        std::lock_guard lock(mutex);
        simple[h->path().value()] = h;
    }
    void remove_simple(const std::optional<std::string>& p) override {
        std::lock_guard lock(mutex);
        simple.erase(p.value());
    }
    network::HttpResponse dispatch(const network::HttpRequest& r) {
        std::shared_ptr<server::CapabilityStreamHandler> stream;
        std::shared_ptr<server::CapabilitySimpleHandler> other;
        {
            std::lock_guard lock(mutex);
            if (auto i = simple.find(r.path); i != simple.end())
                other = i->second;
            else if (auto j = streams.find({r.method, r.path}); j != streams.end())
                stream = j->second;
        }
        if (other)
            return other->handle(r);
        if (stream)
            return stream->handle(r);
        network::HttpResponse result;
        result.status = 404;
        return result;
    }
};
