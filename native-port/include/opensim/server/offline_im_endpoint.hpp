// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/service_protocol.hpp"
#include "opensim/framework/uuid.hpp"

namespace opensim::server {
struct OfflineImOperations {
    std::function<std::vector<network::ServiceXmlValue>(framework::Uuid)> get;
    std::function<std::pair<bool, std::string>(framework::Uuid, const network::ServiceXmlValue&)> store;
    std::function<void(framework::Uuid)> erase;
};

class OfflineImEndpoint {
public:
    OfflineImEndpoint(OfflineImOperations operations, const IniConfig& config, const std::string& section = "Messaging")
        : operations_(std::move(operations)), auth_(config, section) {}

    network::HttpResponse handle(const network::HttpRequest& request);
    std::string process(std::string_view body);

private:
    OfflineImOperations operations_;
    network::ServiceAuth auth_;
};
}
