/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace opensim::network {
class HttpResponseChannel;
struct HttpRequest {
    std::string method,path,body,remote_ip,content_type;
    std::map<std::string,std::string> headers;
    std::string raw_target;
    int remote_port=0;
    std::string http_version="HTTP/1.1";
    std::shared_ptr<HttpResponseChannel> response_channel;
    std::uint32_t request_id=0;
    double arrival_timestamp=0;
};
struct HttpResponse {
    int status=200;
    std::string content_type="text/plain; charset=utf-8",body;
    std::map<std::string,std::string> headers;
    bool keep_alive=false;
    bool deferred=false;
};
struct HttpServerOptions {
    std::string host="127.0.0.1";
    int port=9000,workers=8,queued_requests=64,timeout_seconds=5;
    std::size_t maximum_body=1024*1024;
    std::string certificate,private_key;
    std::vector<std::string> custom_methods;
    bool decompress_request_body=true;
    std::string pkcs12_bundle, pkcs12_password;
};
class HttpServer {
public:
    using Handler=std::function<HttpResponse(const HttpRequest&)>;
    HttpServer(HttpServerOptions options,Handler handler);
    ~HttpServer();
    HttpServer(const HttpServer&)=delete;
    HttpServer& operator=(const HttpServer&)=delete;
    int start(const std::function<void(int)>& on_bound = {});
    void stop();
    bool is_running() const;
    static bool tls_available() noexcept;
    static bool gzip_available() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
