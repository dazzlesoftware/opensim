// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/services/simulation_objects.hpp"
#include "opensim/network/http_server.hpp"
namespace opensim::server {
class ObjectHandler {
public:
    explicit ObjectHandler(services::ISimulationObjects* service):service_(service){}
    virtual ~ObjectHandler()=default;
    void process_request(const network::HttpRequest& request,network::HttpResponse& response);
    void post_object(const network::Value& args,network::HttpResponse& response);
protected:
    virtual bool create_object(services::GridRegion& destination,const framework::Vector3& position,const std::shared_ptr<services::SimulationObject>& object);
private:
    services::ISimulationObjects* service_;
};
}
