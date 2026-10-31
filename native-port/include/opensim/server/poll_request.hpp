// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/poll_types.hpp"
namespace opensim::server {
class PollRequestBackend {
public:
    virtual ~PollRequestBackend()=default;
    virtual std::int32_t tick_count()const;
    virtual framework::Uuid random_id()const{return framework::Uuid::random();}
    virtual PollResponsePtr make_response(const std::shared_ptr<PollIncomingRequest>& request)=0;
    virtual void warn(const std::string& message,const std::exception& error)=0;
};
class PollServiceHttpRequest {
public:
    PollServiceHttpRequest(std::shared_ptr<services::CapabilityPollHandler> args,std::shared_ptr<PollIncomingRequest> request,PollRequestBackend& backend);
    const std::shared_ptr<services::CapabilityPollHandler> poll_args;
    const std::shared_ptr<PollIncomingRequest> request;
    const std::int32_t request_time;
    const framework::Uuid request_id;
    void do_http_grunt_work(PollTablePtr response_data);
    void send_no_content_error(PollResponsePtr response);
    void do_http_stop();
private:
    PollRequestBackend& backend_;
};
}
