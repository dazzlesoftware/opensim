// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/atomic_int32_property.hpp"
#include "opensim/network/http_server.hpp"
#include <any>
#include <functional>
#include <optional>
#include <stdexcept>
#include <vector>
namespace opensim::server {
class PollResponse;
class PollTable;
using PollBytes=std::shared_ptr<std::vector<std::uint8_t>>;
using PollTablePtr=std::shared_ptr<PollTable>;
using PollResponsePtr=std::shared_ptr<PollResponse>;
using PollName=std::optional<std::string>;
struct PollStringObject {std::function<PollName()> to_string;};
struct PollOpaqueObject {std::string type_name="System.Object";};
bool poll_object_null(const std::any& value);
// The string-key operations used by poll responses, retaining typed object
// values and non-string header keys so invalid casts keep their source boundary.
class PollTable {
public:
    virtual ~PollTable()=default;
    virtual bool contains(const std::string& key)const;
    virtual std::any get(const std::string& key)const;
    virtual void set(const std::string& key,std::any value);
    void add_key(std::any key,std::any value);
    void for_each_key(const std::function<void(const std::any&)>& visitor)const;
private:
    std::vector<std::pair<std::any,std::any>> entries_;
    std::uint64_t version_=0;
};
class PollBody {
public:
    virtual ~PollBody()=default;
    virtual bool can_read()const=0;
    virtual void dispose()=0;
};
class PollContext {
public:
    virtual ~PollContext()=default;
    virtual bool can_send()const=0;
    virtual bool is_sending()const=0;
};
class PollIncomingRequest {
public:
    virtual ~PollIncomingRequest()=default;
    virtual std::shared_ptr<PollBody> body()const=0;
    virtual std::shared_ptr<PollContext> context()const=0;
};
class PollResponse {
public:
    virtual ~PollResponse()=default;
    virtual void status_code(std::int32_t value)=0;
    virtual void content_length(std::int64_t value)=0;
    virtual void utf8_encoding()=0;
    virtual void keep_alive(bool value)=0;
    virtual void keep_alive_timeout(std::int32_t value)=0;
    virtual void protocol_version(PollName value)=0;
    virtual void status_description(PollName value)=0;
    virtual void add_header(const std::string& name,PollName value)=0;
    virtual void redirect(PollName url,std::int32_t status)=0;
    virtual void buffer_start(std::int32_t value)=0;
    virtual void buffer_length(std::int32_t value)=0;
    virtual void buffer(PollBytes value)=0;
    virtual void send()=0;
};
class PollSocketError:public std::runtime_error {
public:
    PollSocketError(std::string message,bool reset):std::runtime_error(std::move(message)),connection_reset(reset){}
    bool connection_reset;
};
}
namespace opensim::services {
struct CapabilityPollHandler {
    using RequestMethod=std::function<server::PollResponsePtr(framework::Uuid,const network::HttpRequest&)>;
    using HasEvents=std::function<bool(framework::Uuid,framework::Uuid)>;
    using GetEvents=std::function<server::PollTablePtr(framework::Uuid,framework::Uuid)>;
    using Drop=std::function<void(framework::Uuid,framework::Uuid)>;
    virtual ~CapabilityPollHandler()=default;
    CapabilityPollHandler()=default;
    CapabilityPollHandler(RequestMethod request,server::PollName url,HasEvents has_events,GetEvents get_events,GetEvents no_events,Drop drop,framework::Uuid id,std::int32_t timeout);
    RequestMethod request;
    server::PollName url;
    HasEvents has_events;
    GetEvents get_events,no_events;
    Drop drop;
    framework::Uuid id;
    std::int32_t timeout_ms=0;
    framework::AtomicInt32Property requests_received,requests_handled;
};
}
