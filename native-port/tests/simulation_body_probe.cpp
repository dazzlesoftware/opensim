#include "opensim/network/simulation_body.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>
#include <sstream>
std::string decode(const std::string& s){std::string out;if(s==".")return out;for(std::size_t i=0;i<s.size();i+=2)out+=static_cast<char>(std::stoi(s.substr(i,2),nullptr,16));return out;}
std::string hex(const std::string& s){std::string out;const char* h="0123456789abcdef";for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
int main(){std::string line;while(std::getline(std::cin,line)){std::istringstream in(line);int mode;std::string body;in>>mode>>body;opensim::network::HttpRequest request;request.body=decode(body);switch(mode){case 1:request.content_type="application/x-gzip";break;case 2:request.headers["Content-Encoding"]="gzip";break;case 3:request.headers["X-Content-Encoding"]="gzip";break;case 4:request.headers["X-Content-Encoding"]="GZIP";break;case 5:request.content_type="application/x-gzip; charset=utf-8";break;case 6:request.headers["content-encoding"]="gzip";break;case 7:request.headers["Content-Encoding"]="gzip,gzip";break;}auto value=opensim::network::simulation_json_body(request);std::cout<<(value?hex(opensim::network::osd_json(*value,true)):"-")<<"\n";}}
