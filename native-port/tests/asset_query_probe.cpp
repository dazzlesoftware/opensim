// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/get_assets.hpp"
#include <iomanip>
#include <iostream>
void hex(const std::string& text){for(unsigned char c:text)std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<int(c);}
int main(){std::string line;while(std::getline(std::cin,line)){try{std::string text;for(size_t i=0;i<line.size();i+=2)text+=char(std::stoi(line.substr(i,2),nullptr,16));auto result=opensim::server::asset_query_dictionary(text);for(auto& [key,value]:*result){hex(key);std::cout<<':';if(value)hex(*value);else std::cout<<'N';std::cout<<';';}std::cout<<'\n';}catch(...){std::cout<<"EX\n";}}}
