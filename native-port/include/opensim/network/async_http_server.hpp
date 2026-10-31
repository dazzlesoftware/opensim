// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/http_server.hpp"
#include <cstdint>
namespace opensim::network {
struct RawHttpResponse {
    std::string headers;
    std::shared_ptr<const std::vector<std::uint8_t>> body;
    std::size_t start=0,length=0;
    bool keep_alive=false;
};
class HttpResponseChannel {
public:
    virtual ~HttpResponseChannel()=default;
    virtual bool can_send()const=0;
    virtual bool is_sending()const=0;
    virtual bool is_closed()const=0;
    virtual bool complete(HttpResponse response)=0;
    virtual bool complete_raw(RawHttpResponse response)=0;
    virtual int requests_remaining()const=0;
    virtual int idle_timeout_ms()const=0;
    virtual void idle_timeout_ms(int value)=0;
    virtual bool send_chunk(std::shared_ptr<const std::vector<std::uint8_t>> bytes,std::size_t start,std::size_t length,std::function<void()> complete)=0;
    virtual void finish_response(bool keep_alive)=0;
    virtual void on_disconnect(std::function<void()> callback)=0;
    virtual void cancel()=0;
};
class AsyncHttpServer {
public:
    using Handler=std::function<void(const HttpRequest&,std::shared_ptr<HttpResponseChannel>)>;
    AsyncHttpServer(HttpServerOptions options,Handler handler);
    ~AsyncHttpServer();
    AsyncHttpServer(const AsyncHttpServer&)=delete;
    AsyncHttpServer& operator=(const AsyncHttpServer&)=delete;
    int start(const std::function<void(int)>& on_bound={});
    void stop();
    bool is_running()const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
