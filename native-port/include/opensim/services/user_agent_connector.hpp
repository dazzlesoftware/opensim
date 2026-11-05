// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <mutex>
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/services/grid_region.hpp"
#include "opensim/services/simulation_connector.hpp"
#include "opensim/framework/http_uri.hpp"
namespace opensim::network {
class WebHttpClients;
}
namespace opensim::services {
class AccountStore;
std::function<framework::Uuid(std::string_view, std::string_view, std::string_view)>
make_script_user_key_lookup(AccountStore&, std::shared_ptr<network::WebHttpClients>);

struct UserAgentConnectorServices {
    using Text = std::optional<std::u16string>;
    std::function<std::shared_ptr<network::XmlRpcRequesterClient>(std::int32_t)> client;
    std::function<void(const std::u16string&)> debug, error, warn;
    std::function<std::u16string(std::u16string_view)> lower;
    std::function<void(std::u16string_view)> validate_uri;
    std::function<Text(const network::Value&)> object_text;
    std::function<std::u16string(std::exception_ptr)> exception_message;
    SimulationConnector::Request simulation_request;
    SimulationPositionOptions simulation_options;
};
// User-agent directory, verification, home-region, friends and grid-agent login.
// Full configuration resolution and production host installation remain pending.
class UserAgentServiceConnector : public SimulationConnector, public IUserAgentLogin {
    using Value = network::Value;
    using Text = UserAgentConnectorServices::Text;
    UserAgentConnectorServices services_;
    Text server_url_;
    std::recursive_mutex gatekeeper_mutex_;
    std::shared_ptr<GridRegion> gatekeeper_;
    void initialise_services();
    void configure_url(const Text&, bool);
    bool set_service_url(const Text&);
    std::shared_ptr<Value> call_server(std::u16string_view, std::shared_ptr<Value>);
    std::shared_ptr<network::NwcXmlRpcResponse>
    send_request(const std::shared_ptr<network::NwcXmlRpcRequest>&);

  public:
    UserAgentServiceConnector(Text, UserAgentConnectorServices, bool from_configuration = false);
    UserAgentServiceConnector(std::shared_ptr<framework::NiniConfigSourceBase>, UserAgentConnectorServices,
                              framework::RegionUri::Resolver = {});
    const Text& server_url() const {
        return server_url_;
    }
    Value get_user_info(const framework::Uuid&);
    Value get_server_urls(const framework::Uuid&);
    Text locate_user(const framework::Uuid&);
    Text get_uui(const framework::Uuid&, const framework::Uuid&);
    framework::Uuid get_uuid(const Text& first, const Text& last);
    bool get_bool_response(const std::shared_ptr<network::NwcXmlRpcRequest>&, Text& reason);
    bool verify_agent(const framework::Uuid& session, const Text& token);
    bool verify_client(const framework::Uuid& session, const Text& token);
    bool is_agent_coming_home(const framework::Uuid& session, const Text& external_name);
    void logout_agent(const framework::Uuid& user, const framework::Uuid& session);
    void set_client_token(const framework::Uuid&, const Text&) {}
    std::shared_ptr<GridRegion> get_home_region(const framework::Uuid&, framework::Vector3& position,
                                                framework::Vector3& look_at);
    using Friends = std::shared_ptr<std::vector<Text>>;
    std::vector<framework::Uuid> status_notification(const Friends&, const framework::Uuid&, bool online);
    std::vector<framework::Uuid> get_online_friends(const framework::Uuid&, const Friends&);
    // Reference entry points borrow the gatekeeper for this connector's lifetime.
    // AgentLauncher uses the owning entry point below to retain source identity.
    bool login_agent_to_grid(const GridRegion*, const std::shared_ptr<framework::AgentCircuitData>&,
                             GridRegion&, GridRegion&, bool, std::string&) override;
    bool login_agent_to_grid(const GridRegion*, const std::shared_ptr<framework::AgentCircuitData>&,
                             GridRegion* gatekeeper, GridRegion* destination, bool, std::string&);
    bool login_agent_to_grid(const GridRegion* source,
                             const std::shared_ptr<framework::AgentCircuitData>& circuit,
                             GridRegion* gatekeeper, GridRegion* destination, std::string& reason) {
        return login_agent_to_grid(source, circuit, gatekeeper, destination, false, reason);
    }
    bool login_agent_to_grid_owned(const GridRegion*, const std::shared_ptr<framework::AgentCircuitData>&,
                                   const std::shared_ptr<GridRegion>&, const std::shared_ptr<GridRegion>&,
                                   bool, std::string&) override;

  protected:
    std::string agent_path() const override {
        return "homeagent/";
    }
    void pack_data(Value&, const GridRegion*, framework::AgentCircuitData&, const GridRegion&,
                   std::uint32_t) override;

  private:
    bool login_grid(const GridRegion*, const std::shared_ptr<framework::AgentCircuitData>&, GridRegion*,
                    std::shared_ptr<GridRegion>, GridRegion*, bool, std::string&);
    std::vector<framework::Uuid> online_friends_response(const std::shared_ptr<network::NwcXmlRpcRequest>&,
                                                         std::u16string_view operation);
};
}
