// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once

#include "opensim/network/rest_forms.hpp"
#include "opensim/services/presence_service.hpp"

namespace opensim::services {
class PresenceConnector final : public IPresenceService {
public:
    PresenceConnector(const server::IniConfig& config, std::shared_ptr<network::WebHttpClients> clients,
                      std::function<void(const std::string&)> log = {});

    bool login(const std::string& user, const framework::Uuid& session,
               const framework::Uuid& secure_session) override;
    bool logout(const framework::Uuid& session) override;
    bool logout_from_region(const framework::Uuid& session, const framework::Uuid& region) override;
    bool logout_region(const framework::Uuid& region) override;
    bool report(const framework::Uuid& session, const framework::Uuid& region) override;
    std::optional<PresenceInfo> agent(const framework::Uuid& session) override;
    std::vector<PresenceInfo> agents(const std::vector<std::string>& users) override;

private:
    using Fields = std::vector<std::pair<std::string, std::optional<std::string>>>;
    std::string post(std::string_view method, const Fields& fields = {},
                     const std::vector<std::string>& users = {});
    bool simple(std::string_view method, const Fields& fields);
    network::ServiceXmlValue query(std::string_view method, const Fields& fields,
                                   const std::vector<std::string>& users = {});

    std::string endpoint_;
    network::RestFormsRequest request_;
    std::function<void(const std::string&)> log_;
};
}
