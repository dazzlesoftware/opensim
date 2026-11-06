// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/asset_store.hpp"

namespace opensim::framework {
class ConsoleCommandResolver;
}

namespace opensim::server {
class AssetEndpoint {
public:
    AssetEndpoint(std::shared_ptr<services::AssetService> service, const IniConfig& config,
                  const std::string& section = "AssetService");
    static bool matches(const network::HttpRequest& request);
    network::HttpResponse handle(const network::HttpRequest& request);
    void register_console_commands(framework::ConsoleCommandResolver& commands,
                                   const std::function<void(const std::string&)>& output) const;

private:
    std::shared_ptr<services::AssetService> service_;
    network::ServiceAuth auth_;
    std::string redirect_;
    bool allow_delete_;
    bool allow_delete_all_;
};
}
