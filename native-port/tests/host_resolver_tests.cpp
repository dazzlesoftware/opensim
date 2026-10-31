#include "opensim/framework/host_resolver.hpp"
#include "opensim/services/login_response.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace opensim;
void check(bool passed,const char* message){if(!passed)throw std::runtime_error(message);}
int main(){try{
 auto address=std::make_shared<framework::IpAddress>(*framework::IpAddress::parse("2001:db8::1"));std::atomic<int> lookups=0;
 auto resolver=std::make_shared<framework::HostResolver>([&](const auto&)->std::optional<std::vector<framework::HostResolver::Address>>{++lookups;return std::vector<framework::HostResolver::Address>{address};});check(resolver->host_from_dns("fixture")==address,"cache retains DNS address identity");
 std::atomic<bool> valid=true;std::vector<std::thread> readers;for(int n=0;n<8;++n)readers.emplace_back([&]{for(int i=0;i<1000;++i){auto endpoint=resolver->endpoint(std::string("fixture"),9000);if(!endpoint||endpoint->address!=address||endpoint->port!=9000)valid=false;}});for(auto& reader:readers)reader.join();check(valid&&lookups==1&&resolver->count()==1,"concurrent positive lookups refresh the same cache entry");
 auto old_endpoint=resolver->endpoint(std::string("fixture"),9001);address->scope_id(7);check(old_endpoint->str()=="[2001:db8::1%7]:9001","existing endpoints observe shared IP scope changes");resolver->clear();check(old_endpoint->address==address&&resolver->count()==0,"clearing cache retains held endpoint addresses");
 services::LoginResponseEnvironment environment;bool timezone_called=false;environment.local_daylight=[&]{timezone_called=true;return true;};services::bind_login_response_network(environment,resolver);services::GridRegion region;region.external_host="fixture";region.internal_endpoint=services::RegionEndpoint{"0.0.0.0",9000};region.set_server_uri("http://destination:9001/");auto endpoint=environment.external_endpoint(region);check(endpoint&&endpoint->address=="2001:db8::1%7"&&endpoint->port==9000,"response provider uses resolver and simulator port");check(environment.server_uri(region)==std::optional<std::string>("http://destination:9001/")&&environment.local_daylight()&&timezone_called,"network binding preserves independent timezone providers");region.internal_endpoint.reset();bool threw=false;try{environment.external_endpoint(region);}catch(const std::invalid_argument&){threw=true;}check(threw,"null internal region endpoint fails before DNS");
 auto automatic=std::make_unique<framework::HostResolver>();auto local=automatic->host_from_dns("localhost");check(local&&!local->unspecified(),"native system DNS resolves localhost");auto start=std::chrono::steady_clock::now();automatic.reset();check(std::chrono::steady_clock::now()-start<std::chrono::seconds(5),"active cache timer shuts down without waiting for its next sweep");
 std::cout<<"PASS: native host resolution, cache concurrency and response binding\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what();return 1;}}
