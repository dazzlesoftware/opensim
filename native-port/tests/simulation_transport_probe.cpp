#include "opensim/services/simulation_connector.hpp"
#include "opensim/network/osd_json.hpp"
#include "simulation_http_fixture.hpp"
#include "opensim/framework/avatar_appearance.hpp"
#include <iostream>
using namespace opensim;
int main(int argc, char** argv) {
    try {
        services::GridRegion destination;
        destination.id = framework::Uuid("00000000-0000-0000-0000-000000000001");
        destination.name = "Remote";
        destination.x = 25600;
        destination.y = 51200;
        destination.set_server_uri(argv[1]);
        services::SimulationConnector connector(simulation_http_requester());
        framework::EntityTransferContext context;
        std::vector<framework::Uuid> features{framework::Uuid("00000000-0000-0000-0000-000000000003")};
        auto circuit = std::make_shared<framework::AgentCircuitData>();
        circuit->agent_id = framework::Uuid("00000000-0000-0000-0000-000000000007");
        circuit->first_name = "First";
        circuit->last_name = "Last";
        circuit->caps_path = "caps/";
        circuit->appearance = std::make_shared<framework::AvatarAppearance>();
        circuit->start_position = {1.25f, 2, 30};
        if (argc > 2) {
            circuit->service_urls = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
            (*circuit->service_urls)["Large"] = std::string(150000, 'x');
        }
        std::string query_reason, create_reason;
        const bool query = connector.query_access(destination, circuit->agent_id, std::nullopt, true,
                                                  circuit->start_position, features, context, query_reason);
        bool created = false;
        if (query)
            created = connector.create_agent(nullptr, destination, circuit, 4096, context, create_reason);
        auto result = network::Value::object({});
        result.set("query", network::Value(query));
        result.set("query_reason", query_reason);
        result.set("create", network::Value(created));
        result.set("create_reason", create_reason);
        result.set("features", network::Value(static_cast<std::int32_t>(features.size())));
        result.set("context", context.pack());
        std::cout << network::osd_json(result, true);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
