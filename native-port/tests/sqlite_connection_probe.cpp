// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/sqlite_connection.hpp"
#include <iostream>
std::string hex(std::string_view value){std::string out;for(unsigned char c:value){out+="0123456789abcdef"[c>>4];out+="0123456789abcdef"[c&15];}return out;}
int main(){std::string line;while(std::getline(std::cin,line)){try{char mode=line.at(0);std::string value;for(size_t i=1;i<line.size();i+=2)value+=char(std::stoi(line.substr(i,2),nullptr,16));if(mode=='B')std::cout<<(opensim::services::sqlite_boolean(value)?"1":"0");else if(mode=='M')std::cout<<hex(opensim::services::sqlite_uri_path(value));else{auto fields=opensim::services::parse_sqlite_connection(value,mode=='A');if(mode=='C'){std::cout<<hex(opensim::services::sqlite_connection_path(fields))<<'\n';continue;}std::string out;for(auto& [key,v]:fields)out+=hex(key)+":"+hex(v)+";";std::cout<<out;}std::cout<<'\n';}catch(...){std::cout<<"EX\n";}}}
