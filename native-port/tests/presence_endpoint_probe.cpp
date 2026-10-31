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
#include "opensim/server/presence_endpoint.hpp"
#include "opensim/framework/filesystem.hpp"
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace opensim;
std::string hex(std::string_view text){std::ostringstream out;for(unsigned char c:text)out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<int>(c);return out.str();}
std::string unhex(const std::string& text){std::string result;for(std::size_t i=0;i<text.size();i+=2)result.push_back(static_cast<char>(std::stoi(text.substr(i,2),nullptr,16)));return result;}
int main(int argc,char** argv){try{
    const auto config=server::IniConfig::load(framework::utf8_path(argv[1]));services::NullPresenceStore store;const framework::Uuid first("00000000-0000-0000-0000-000000000001"),second("00000000-0000-0000-0000-000000000002"),region("00000000-0000-0000-0000-000000000003");std::string line;
    while(std::getline(std::cin,line)){
        if(argc>2&&std::string(argv[2])=="--forms"){
            const auto form=network::parse_service_form(unhex(line));std::vector<std::string> rows;
            for(const auto& [key,value]:form){std::string row=hex(key)+"=";if(const auto* scalar=std::get_if<std::string>(&value))row+="S"+hex(*scalar);else{row+="L";for(const auto& item:std::get<std::vector<std::string>>(value))row+=hex(item)+",";}rows.push_back(row);}std::sort(rows.begin(),rows.end());for(std::size_t i=0;i<rows.size();++i){if(i)std::cout<<'|';std::cout<<rows[i];}std::cout<<'\n';continue;
        }
        const auto a=line.find('|'),b=line.find('|',a+1);const auto body=unhex(line.substr(0,a)),authorization=unhex(line.substr(a+1,b-a-1));store.erase("UserID","alpha");store.erase("UserID","beta <&> \r\n\xe8\xa9\xa6\xe9\xa8\x93");double now=0;auto cache=std::make_shared<services::PresenceCache>([&]{return now;});services::PresenceService presence(store,false,cache);presence.login("alpha",first,{});presence.report(first,region);presence.login("beta <&> \r\n\xe8\xa9\xa6\xe9\xa8\x93",second,{});server::PresenceEndpoint endpoint(presence,config);
        network::HttpRequest request{"POST","/presence",body,"127.0.0.1","",{}};if(!authorization.empty())request.headers["Authorization"]=authorization;if(line.substr(b+1)=="1")request.headers["X-SecondLife-Shard"]="";
        try{const auto response=endpoint.handle(request);const auto auth=response.headers.find("WWW-Authenticate");std::cout<<response.status<<'|'<<hex(response.body)<<'|'<<hex(response.content_type)<<'|'<<(auth==response.headers.end()?"":hex(auth->second))<<'\n';}catch(const std::invalid_argument&){std::cout<<"BASE64_FORMAT_ERROR\n";}
    }return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
