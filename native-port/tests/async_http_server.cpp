#include "opensim/network/async_http_server.hpp"
#include <iostream>
#include <mutex>
#include <unordered_map>
using namespace opensim::network;
int main(int argc,char**argv){try{std::mutex mutex;std::unordered_map<std::string,std::shared_ptr<HttpResponseChannel>> pending;HttpServerOptions options;options.port=0;options.workers=1;options.queued_requests=128;options.timeout_seconds=2;if(argc==3){options.certificate=argv[1];options.private_key=argv[2];}
    AsyncHttpServer server(options,[&](const HttpRequest& request,std::shared_ptr<HttpResponseChannel> channel){HttpResponse response;
        if(request.path.starts_with("/wait/")){std::lock_guard lock(mutex);pending[request.path.substr(6)]=std::move(channel);return;}
        if(request.path=="/stats"){std::vector<std::shared_ptr<HttpResponseChannel>>snapshot;{std::lock_guard lock(mutex);for(auto&[key,c]:pending)snapshot.push_back(c);}std::size_t live=0;for(auto&c:snapshot)if(c->can_send())++live;response.body=std::to_string(snapshot.size())+":"+std::to_string(live);}
        else if(request.path.starts_with("/release/")||request.path.starts_with("/cancel/")){const bool cancel=request.path.starts_with("/cancel/");const auto key=request.path.substr(cancel?8:9);std::shared_ptr<HttpResponseChannel> target;{std::lock_guard lock(mutex);auto i=pending.find(key);if(i!=pending.end()){target=i->second;pending.erase(i);}}if(target){if(cancel)target->cancel();else{HttpResponse reply;reply.body="released:"+key;reply.keep_alive=true;target->complete(std::move(reply));}response.body="ok";}else response.status=404;}
        else if(request.path=="/throw")throw std::runtime_error("fixture");
        else if(request.path=="/asset-sized")response.body=std::string(79234,'a');
        else if(request.path=="/large")response.body=std::string(16*1024*1024,'x');
        else if(request.path=="/ready"){response.status=503;response.body="Native world runtime incomplete\n";}
        else{response.body=request.method+"|"+request.path+"|"+request.raw_target+"|"+request.body+"|"+request.http_version;response.keep_alive=true;}
        channel->complete(std::move(response));
    });server.start([](int port){std::cout<<"LISTENING "<<port<<std::endl;});std::string command;std::getline(std::cin,command);server.stop();return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
