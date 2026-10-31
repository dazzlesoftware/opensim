// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/poll_runtime.hpp"
#include "opensim/server/http_route_registry.hpp"
#include "opensim/network/async_http_server.hpp"
#include "opensim/server/http_response.hpp"
namespace opensim::server {
class PollHttpService final:public PollRequestBackend {
public:
    using Log=std::function<void(const std::string&,const std::exception&)>;
    explicit PollHttpService(std::uint32_t workers=2,Log log={});
    ~PollHttpService()override;
    void start(){manager_.start();}
    void stop(){manager_.stop();}
    std::size_t pending_count()const{return manager_.retry_count();}
    void accept(std::shared_ptr<services::CapabilityPollHandler> handler,const network::HttpRequest& request,std::shared_ptr<network::HttpResponseChannel> channel,bool count_received=true);
    void dispatch(const RegistryCapabilityListener& listener,const network::HttpRequest& request,std::shared_ptr<network::HttpResponseChannel> channel,const network::HttpServer::Handler& fallback={});
    static PollResponsePtr response(const network::HttpRequest& request);
    static std::shared_ptr<HttpResponseState> response_state(const PollResponsePtr& response);
    PollResponsePtr make_response(const std::shared_ptr<PollIncomingRequest>& request)override;
    void warn(const std::string& message,const std::exception& error)override;
private:
    Log log_;
    PollRuntimeBackend runtime_;
    PollServiceRequestManager manager_;
};
}
