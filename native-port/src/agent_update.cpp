// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/agent_update.hpp"
#include "opensim/services/avatar_data.hpp"
namespace opensim::server {
void AgentUpdateHandler::put_agent(network::Value& args, network::HttpResponse& response) {
    using namespace network;
    if (args.view().kind != Value::Kind::Map)
        throw std::invalid_argument("Agent update requires an OSD map");
    framework::EntityTransferContext context;
    services::GridRegion destination;
    destination.name = "";
    if (args.contains("destination_x"))
        services::parse_account_integer(osd_string(args.get("destination_x")), destination.x);
    if (args.contains("destination_y"))
        services::parse_account_integer(osd_string(args.get("destination_y")), destination.y);
    if (args.contains("destination_uuid"))
        framework::try_parse_sdk_uuid(osd_string(args.get("destination_uuid")), destination.id);
    if (args.contains("destination_name"))
        destination.name = osd_to_string(args.get("destination_name"));
    if (args.contains("context") && args.get("context").view().kind == Value::Kind::Map)
        context.unpack(args.get("context").view());
    const auto message = osd_string(args.get("message_type"));
    bool accepted = true;
    if (message == "AgentData") {
        auto agent = std::make_shared<framework::AgentData>();
        try {
            if (!service_)
                throw std::invalid_argument("Null simulation service");
            auto scene = service_->get_scene(destination.id);
            agent->unpack(args, scene.get(), &context);
        } catch (const std::exception&) {
            response.status = 400;
            response.body = "false";
            return;
        }
        accepted = update_agent(destination, agent);
    } else if (message == "AgentPosition") {
        auto agent = std::make_shared<framework::AgentPosition>();
        try {
            if (!service_)
                throw std::invalid_argument("Null simulation service");
            auto scene = service_->get_scene(destination.id);
            agent->unpack(args, scene.get(), &context);
        } catch (const std::exception&) {
            response.status = 400;
            response.body = "false";
            return;
        }
        accepted = service_->update_agent(destination, agent);
    }
    response.status = 200;
    response.body = accepted ? "True" : "False";
}
bool AgentUpdateHandler::update_agent(services::GridRegion& destination,
                                      const std::shared_ptr<framework::AgentData>& data) {
    if (!service_)
        throw std::invalid_argument("Null simulation service");
    framework::EntityTransferContext context;
    return service_->update_agent(destination, data, context);
}
}
