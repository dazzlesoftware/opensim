// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/framework/uuid.hpp"
#include "opensim/network/service_protocol.hpp"
#include "opensim/services/mute_list_store.hpp"

namespace opensim::server {
class MuteListEndpoint {
public:
    explicit MuteListEndpoint(const IniConfig& config);

    std::vector<std::uint8_t> get(framework::Uuid agent, std::uint32_t crc);
    bool store(framework::Uuid agent, framework::Uuid mute, const std::string& name,
               std::int32_t type, std::int32_t flags, std::optional<std::int32_t> stamp = {});
    bool remove(framework::Uuid agent, framework::Uuid mute, const std::string& name);
    network::HttpResponse handle(const network::HttpRequest& request);

private:
    std::string process(std::string_view body);

    network::ServiceAuth auth_;
    services::MuteListStore store_;
};
}
