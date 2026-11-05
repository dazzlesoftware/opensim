// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/ordered_dictionary.hpp"
#include "opensim/network/http_server.hpp"
#include <mutex>
#include <optional>
#include <unordered_map>
namespace opensim::server {
class CapabilityStreamHandler {
  public:
    virtual ~CapabilityStreamHandler() = default;
    virtual std::optional<std::string> path() const = 0;
    virtual std::string method() const = 0;
    virtual std::optional<std::string> http_method() const {
        return method();
    }
    virtual bool streamed() const {
        return true;
    }
    virtual network::HttpResponse handle_stream(const std::string&, const network::HttpRequest& request) {
        return handle(request);
    }
    virtual network::HttpResponse handle(const network::HttpRequest& request) = 0;
};
class CapabilitySimpleHandler {
  public:
    virtual ~CapabilitySimpleHandler() = default;
    virtual std::optional<std::string> path() const = 0;
    virtual network::HttpResponse handle(const network::HttpRequest& request) = 0;
};
class CapabilityListener {
  public:
    virtual ~CapabilityListener() = default;
    virtual bool use_ssl() const = 0;
    virtual void add_stream(const std::shared_ptr<CapabilityStreamHandler>& handler) = 0;
    virtual void remove_stream(const std::string& method, const std::optional<std::string>& path) = 0;
    virtual void add_simple(const std::shared_ptr<CapabilitySimpleHandler>& handler) = 0;
    virtual void remove_simple(const std::optional<std::string>& path) = 0;
};
class CapsHandlers {
  public:
    using Name = std::optional<std::string>;
    using Requested = std::vector<Name>;
    using Paths = framework::OrderedDictionary<std::string, std::optional<std::string>>;
    using Streams = framework::OrderedDictionary<std::string, std::shared_ptr<CapabilityStreamHandler>>;
    CapsHandlers(std::shared_ptr<CapabilityListener> listener, Name host, std::uint32_t port);
    const std::string& base_url() const {
        return base_url_;
    }
    void remove(const Name& name);
    void add_simple(const Name& name, std::shared_ptr<CapabilitySimpleHandler> handler,
                    bool add_to_listener = true);
    bool contains(const Name& name) const;
    std::shared_ptr<CapabilityStreamHandler> get(const Name& name) const;
    void set(const Name& name, std::shared_ptr<CapabilityStreamHandler> handler);
    std::vector<std::string> caps() const;
    Paths details(bool exclude_seed, const Requested* requested) const;
    Paths local_paths() const;
    void append_details_xml(const Requested* requested, std::string* output) const;
    static void append_llsd_element(const std::string& name, const Name& value, std::string* output);
    Streams streams() const;

  private:
    std::shared_ptr<CapabilityListener> listener_;
    std::string base_url_;
    mutable std::recursive_mutex streams_mutex_, simple_mutex_;
    Streams streams_;
    std::unordered_map<std::string, std::shared_ptr<CapabilitySimpleHandler>> simple_;
    std::vector<std::pair<std::string, std::shared_ptr<CapabilitySimpleHandler>>> simple_snapshot() const;
    std::shared_ptr<CapabilitySimpleHandler> find_simple(const std::string& key, bool& exists) const;
    void remove_stream_entries(const std::string& name);
};
}
