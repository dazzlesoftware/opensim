#include "opensim/framework/host_resolver.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim;
std::string hex(std::string_view s){constexpr char d[]="0123456789abcdef";std::string r;for(unsigned char c:s){r+=d[c>>4];r+=d[c&15];}return r;}
std::optional<std::string> text(std::string token){return token=="-"?std::nullopt:std::optional<std::string>(token=="."?"":fixture::unhex(token));}
std::string address(const std::shared_ptr<framework::IpAddress>& value){if(!value)return "null";auto bytes=value->bytes();std::string data(reinterpret_cast<const char*>(bytes.data()),value->v6()?16:4);return std::string(value->v6()?"6:":"4:")+hex(value->str())+":"+hex(data)+":"+std::to_string(value->v6()?value->scope_id():value->ipv4_address())+":"+(value->unspecified()?"1":"0");}
int main(int argc,char**){bool system=argc>1;std::unique_ptr<framework::HostResolver> resolver;std::shared_ptr<framework::IpAddress> last;std::shared_ptr<framework::IpEndpoint> endpoint;double now=0;int clocks=0;std::string trace,line;auto ip=[](std::string value){return std::make_shared<framework::IpAddress>(*framework::IpAddress::parse(value));};
 auto lookup=[&](const std::string& name)->std::optional<std::vector<std::shared_ptr<framework::IpAddress>>>{trace+=name+";";if(system)return framework::HostResolver::system_lookup(name);if(name=="throw")throw std::runtime_error("DNS");if(name=="empty")return std::vector<std::shared_ptr<framework::IpAddress>>{};if(name=="null")return {};if(name=="badlist")return std::vector<std::shared_ptr<framework::IpAddress>>{nullptr,ip("127.0.0.1")};if(name=="later-null")return std::vector<std::shared_ptr<framework::IpAddress>>{ip("::1"),nullptr,ip("127.0.0.1")};if(name=="first-v4-null")return std::vector<std::shared_ptr<framework::IpAddress>>{ip("127.0.0.1"),nullptr};if(name=="v6")return std::vector<std::shared_ptr<framework::IpAddress>>{ip("2001:db8::1"),ip("::1")};if(name=="mixed")return std::vector<std::shared_ptr<framework::IpAddress>>{ip("2001:db8::1"),ip("203.0.113.5"),ip("198.51.100.2")};if(name=="any")return std::vector<std::shared_ptr<framework::IpAddress>>{ip("0.0.0.0")};return std::vector<std::shared_ptr<framework::IpAddress>>{ip("198.51.100.7")};};
 while(std::getline(std::cin,line)){std::istringstream in(line);std::string command,key,value;in>>command>>key>>value;std::string result="OK";try{
 if(command=="p"){auto parsed=framework::IpAddress::parse(text(key));std::cout<<(parsed?address(std::make_shared<framework::IpAddress>(*parsed)):"null")<<'\n';continue;}
 if(command=="reset"){resolver.reset();now=0;clocks=0;trace.clear();last.reset();endpoint.reset();resolver=std::make_unique<framework::HostResolver>(lookup,[&]{++clocks;return now;},false);}
 else if(command=="h")last=resolver->host_from_dns(text(key));else if(command=="u")last=resolver->host_from_url(text(key));else if(command=="e"){endpoint=resolver->endpoint(text(key),std::stoi(value));if(endpoint)last=endpoint->address;}
 else if(command=="d"){auto parsed=framework::IpAddress::parse(text(key));endpoint=framework::HostResolver::endpoint(parsed?std::make_shared<framework::IpAddress>(*parsed):nullptr,std::stoi(value));if(endpoint)last=endpoint->address;}
 else if(command=="scope"){if(!last)throw std::runtime_error("null");last->scope_id(std::stoll(key));}else if(command=="v4"){if(!last)throw std::runtime_error("null");last->ipv4_address(std::stoll(key));}
 else if(command=="time")now=std::stod(key);else if(command=="purge")resolver->purge();else if(command=="clear")resolver->clear();
 }catch(...){result="EX";}
 std::cout<<result<<'|'<<clocks<<'|'<<(resolver?resolver->count():0)<<'|'<<hex(trace)<<'|'<<address(last)<<'|'<<(endpoint?hex(endpoint->str()):"null")<<'\n';
 }}
