#include "opensim/server/agent_delete.hpp"
#include <iostream>
std::string hex(std::string_view s){std::string out;const char* h="0123456789abcdef";for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
int main(){std::string line;while(std::getline(std::cin,line)){std::string target;for(std::size_t i=0;i<line.size();i+=2)target+=static_cast<char>(std::stoi(line.substr(i,2),nullptr,16));try{auto auth=opensim::server::agent_delete_auth(target);std::cout<<(auth?hex(*auth):"-")<<"\n";}catch(...){std::cout<<"EX\n";}}}
