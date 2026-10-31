// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/poll_http_service.hpp"
#include <utility>
namespace opensim::server {
namespace {
class Incoming final:public PollIncomingRequest,public PollBody,public PollContext,public std::enable_shared_from_this<Incoming> {
public:
    network::HttpRequest native;
    std::atomic<bool> readable=true;
    explicit Incoming(network::HttpRequest request):native(std::move(request)){}
    std::shared_ptr<PollBody> body()const override{return std::const_pointer_cast<Incoming>(shared_from_this());}
    std::shared_ptr<PollContext> context()const override{return std::const_pointer_cast<Incoming>(shared_from_this());}
    bool can_read()const override{return readable.load(std::memory_order_relaxed);}
    void dispose()override{readable.store(false,std::memory_order_relaxed);}
    bool can_send()const override{return native.response_channel&&native.response_channel->can_send();}
    bool is_sending()const override{return native.response_channel&&native.response_channel->is_sending();}
};
class ResponseContext final:public HttpResponseContext,public std::enable_shared_from_this<ResponseContext> {
public:
    explicit ResponseContext(std::shared_ptr<network::HttpResponseChannel> channel):channel_(std::move(channel)){if(!channel_)throw std::invalid_argument("Missing asynchronous response channel");}
    void initialize(){channel_->on_disconnect([weak=weak_from_this()]{if(auto self=weak.lock())self->disconnected();});}
    bool is_closing()const override{return channel_->is_closed();}
    int32_t max_requests()const override{return channel_->requests_remaining();}
    int32_t timeout_keep_alive()const override{return channel_->idle_timeout_ms();}
    void timeout_keep_alive(int32_t value)override{channel_->idle_timeout_ms(value);}
    void start_send_response(std::shared_ptr<HttpResponseState> response)override{{std::lock_guard lock(mutex_);current_=std::move(response);}pump();}
    bool send_async_start(HttpBytes bytes,int32_t start,int32_t length)override{
        if(!channel_->can_send())return false;
        if(!bytes||start<0||length<0||size_t(start)>bytes->size()||size_t(length)>bytes->size()-size_t(start))throw std::out_of_range("Response async byte range");
        return channel_->send_chunk(bytes,static_cast<size_t>(start),static_cast<size_t>(length),[weak=weak_from_this()]{if(auto self=weak.lock()){auto response=self->current();if(response)response->check_send_next_async_continue();}});
    }
    void end_send_response(uint32_t,HttpConnectionType connection)override{auto response=take_current();if(response)response->clear();channel_->finish_response(connection==HttpConnectionType::KeepAlive);}
    void continue_send_response()override{pump();}
private:
    std::shared_ptr<HttpResponseState> current()const{std::lock_guard lock(mutex_);return current_;}
    std::shared_ptr<HttpResponseState> take_current(){std::lock_guard lock(mutex_);return std::exchange(current_,{});}
    void disconnected(){auto response=take_current();if(response)response->clear();}
    void pump(){auto response=current();if(!response)return;try{if(!response->send_next_async(128*1024)){std::lock_guard lock(mutex_);if(current_==response)current_.reset();}}catch(...){channel_->cancel();disconnected();}}
    std::shared_ptr<network::HttpResponseChannel>channel_;
    mutable std::mutex mutex_;
    std::shared_ptr<HttpResponseState>current_;
};
class ResponseRequest final:public HttpResponseRequest {
public:
    ResponseRequest(const network::HttpRequest& request,std::shared_ptr<ResponseContext> context):request_(request),context_(std::move(context)){}
    HttpName http_version()const override{return request_.http_version;}
    std::shared_ptr<HttpResponseContext> context()const override{return context_;}
    HttpConnectionType connection()const override{bool keep=request_.http_version!="HTTP/1.0";auto i=request_.headers.find("connection");if(i!=request_.headers.end()){auto value=i->second;for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);if(value.find("close")!=value.npos)keep=false;else if(value.find("keep-alive")!=value.npos)keep=true;}return keep?HttpConnectionType::KeepAlive:HttpConnectionType::Close;}
    uint32_t id()const override{return request_.request_id;}
    double arrival_timestamp()const override{return request_.arrival_timestamp;}
private:
    const network::HttpRequest& request_;
    std::shared_ptr<ResponseContext>context_;
};
class Response final:public PollResponse {
public:
    explicit Response(const network::HttpRequest& request){auto context=std::make_shared<ResponseContext>(request.response_channel);context->initialize();state=std::make_shared<HttpResponseState>(std::make_shared<ResponseRequest>(request,context));}
    void status_code(int32_t value)override{state->status(value);}
    void content_length(int64_t value)override{state->content_length(value);}
    void utf8_encoding()override{state->encoding(http_utf8_encoding());}
    void keep_alive(bool value)override{state->connection(value?HttpConnectionType::KeepAlive:HttpConnectionType::Close);}
    void keep_alive_timeout(int32_t value)override{state->connection(value==0?HttpConnectionType::Close:HttpConnectionType::KeepAlive);state->keep_alive(value);}
    void protocol_version(PollName value)override{state->protocol_version(std::move(value));}
    void status_description(PollName value)override{state->reason(std::move(value));}
    void add_header(const std::string& name,PollName value)override{state->add_header(name,std::move(value));}
    void redirect(PollName url,int32_t code)override{state->redirect(std::move(url),code);}
    void buffer_start(int32_t value)override{state->raw_start(value);}
    void buffer_length(int32_t value)override{state->raw_length(value);}
    void buffer(PollBytes value)override{state->raw_buffer(std::move(value));}
    void send()override{state->send();}
    std::shared_ptr<HttpResponseState>state;
};}
PollHttpService::PollHttpService(uint32_t workers,Log log):log_(std::move(log)),runtime_([this](const std::exception& error){warn("Exception in poll service thread: ",error);}),manager_(false,workers,0,runtime_){}
PollHttpService::~PollHttpService(){try{stop();}catch(...){} }
void PollHttpService::warn(const std::string& message,const std::exception& error){if(log_)log_(message,error);}
PollResponsePtr PollHttpService::response(const network::HttpRequest& request){return std::make_shared<Response>(request);}
std::shared_ptr<HttpResponseState> PollHttpService::response_state(const PollResponsePtr& response){auto adapter=std::dynamic_pointer_cast<Response>(response);if(!adapter)throw std::invalid_argument("Invalid polling response adapter");return adapter->state;}
PollResponsePtr PollHttpService::make_response(const std::shared_ptr<PollIncomingRequest>& request){auto incoming=std::dynamic_pointer_cast<Incoming>(request);if(!incoming)throw std::invalid_argument("Invalid poll HTTP request");return response(incoming->native);}
void PollHttpService::accept(std::shared_ptr<services::CapabilityPollHandler> handler,const network::HttpRequest& native,std::shared_ptr<network::HttpResponseChannel> channel,bool count_received){
    try{
        if(!handler)throw std::invalid_argument("Null poll handler");
        if(count_received)handler->requests_received++;
        auto request=native;request.response_channel=std::move(channel);auto incoming=std::make_shared<Incoming>(std::move(request));auto poll=std::make_shared<PollServiceHttpRequest>(handler,incoming,*this);
        auto callback=handler->request;if(!callback)manager_.enqueue(poll);else{auto direct=callback(poll->request_id,incoming->native);if(direct)direct->send();else manager_.enqueue(poll);}
    }catch(const std::exception& error){warn("[BASE HTTP SERVER]: OnRequest() failed: ",error);}
}
void PollHttpService::dispatch(const RegistryCapabilityListener& listener,const network::HttpRequest& request,std::shared_ptr<network::HttpResponseChannel> channel,const network::HttpServer::Handler& fallback){
    bool pending=false;
    auto reply=listener.dispatch(request,fallback,[&](const std::shared_ptr<services::CapabilityPollHandler>& handler,const network::HttpRequest& incoming){pending=true;accept(handler,incoming,channel,false);return network::HttpResponse{};});
    if(!pending&&!reply.deferred)channel->complete(std::move(reply));
}
}
