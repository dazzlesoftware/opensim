// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/object_handler.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/network/simulation_body.hpp"
#include "opensim/framework/vector_text.hpp"
#include <stdexcept>
namespace opensim::server {
void ObjectHandler::process_request(const network::HttpRequest& request, network::HttpResponse& response) {
    response.keep_alive = false;
    if (!service_) {
        response.status = 500;
        response.body = "false";
        return;
    }
    for (const auto& [name, value] : request.headers) {
        auto lower = name;
        for (auto& c : lower)
            if (c >= 'A' && c <= 'Z')
                c += 32;
        if (lower == "x-secondlife-shard") {
            response.status = 406;
            response.body = "false";
            break;
        }
    }
    if (request.method != "POST") {
        response.status = 405;
        return;
    }
    auto args = network::simulation_json_body(request);
    if (!args) {
        response.status = 400;
        response.body = "false";
        return;
    }
    post_object(*args, response);
}
void ObjectHandler::post_object(const network::Value& args, network::HttpResponse& response) {
    using namespace network;
    if (args.view().kind != Value::Kind::Map)
        throw std::invalid_argument("Object post requires an OSD map");
    if (!args.contains("destination_uuid"))
        return;
    auto uuid = osd_uuid(args.get("destination_uuid"));
    if (!service_)
        throw std::invalid_argument("Null simulation service");
    auto scene = service_->get_scene(uuid);
    if (!scene)
        return;
    auto xml = osd_string(args.get("sog"));
    if (!args.contains("sog") || xml.empty())
        return;
    auto x = osd_integer(args.get("destination_x")), y = osd_integer(args.get("destination_y"));
    auto name = osd_string(args.get("destination_name"));
    framework::Vector3 position{};
    const auto& value = args.get("new_position").view();
    if (value.kind == Value::Kind::String)
        position = framework::parse_sdk_vector3(value.text);
    else if (value.kind == Value::Kind::Array && value.array.size() == 3)
        position = {static_cast<float>(osd_real(value.array[0])),
                    static_cast<float>(osd_real(value.array[1])),
                    static_cast<float>(osd_real(value.array[2]))};
    auto extra = osd_string(args.get("extra"));
    std::shared_ptr<services::SimulationObject> object;
    try {
        object = std::dynamic_pointer_cast<services::SimulationObject>(scene->deserialize_object(xml));
        if (!object)
            throw std::invalid_argument("Null transfer object");
        object->extra_from_xml_string(extra);
    } catch (const std::exception&) {
        response.status = 400;
        return;
    }
    object->has_group_changed(args.contains("modified") && osd_boolean(args.get("modified")));
    auto state = osd_string(args.get("state"));
    if (args.contains("state") && scene->allow_script_crossings()) {
        if (!state.empty())
            try {
                object->set_state(state, scene.get());
            } catch (const std::exception&) {
            }
    }
    services::GridRegion destination;
    destination.id = uuid;
    destination.x = x;
    destination.y = y;
    destination.name = name;
    bool result;
    try {
        result = create_object(destination, position, object);
    } catch (const std::exception&) {
        result = false;
    }
    response.status = 200;
    response.body = result ? "True" : "False";
}
bool ObjectHandler::create_object(services::GridRegion& destination, const framework::Vector3& position,
                                  const std::shared_ptr<services::SimulationObject>& object) {
    if (!service_)
        throw std::invalid_argument("Null simulation service");
    return service_->create_object(&destination, position, object, false);
}
}
