// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/network/rest_forms.hpp"
#include "opensim/services/grid_user_service.hpp"

namespace opensim::services {
class GridUserConnector {
public:
    GridUserConnector(const server::IniConfig& config, std::shared_ptr<network::WebHttpClients> clients,
                      std::function<void(const std::string&)> log = {});
    ~GridUserConnector();

    std::optional<GridUserInfo> logged_in(const std::string& user);
    std::optional<GridUserInfo> info(const std::string& user);
    std::vector<GridUserInfo> info(const std::vector<std::string>& users);
    bool logged_out(const std::string& user, framework::Uuid region,
                    framework::Vector3 position, framework::Vector3 look);
    bool set_home(const std::string& user, framework::Uuid region,
                  framework::Vector3 position, framework::Vector3 look);
    bool set_last_position(const std::string& user, framework::Uuid region,
                           framework::Vector3 position, framework::Vector3 look);

private:
    using Fields = std::vector<std::pair<std::string, std::optional<std::string>>>;
    network::ServiceXmlValue query(std::string_view method, const Fields& fields,
                                   const std::vector<std::string>& users = {});
    bool set(std::string_view method, const std::string& user, framework::Uuid region,
             framework::Vector3 position, framework::Vector3 look);

    struct Cache;
    std::unique_ptr<Cache> cache_;
    std::string endpoint_;
    network::RestFormsRequest request_;
    std::function<void(const std::string&)> log_;
};
}
