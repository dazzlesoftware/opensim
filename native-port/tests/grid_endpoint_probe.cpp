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
#include "opensim/server/grid_endpoint.hpp"
#include "opensim/framework/filesystem.hpp"
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <sstream>
using namespace opensim;
std::string hex(std::string_view text){std::ostringstream out;for(unsigned char c:text)out<<std::hex<<std::setfill('0')<<std::setw(2)<<static_cast<int>(c);return out.str();}
std::string unhex(const std::string& text){std::string result;for(std::size_t i=0;i<text.size();i+=2)result.push_back(static_cast<char>(std::stoi(text.substr(i,2),nullptr,16)));return result;}
int main(int argc,char** argv){try{
    const auto config=server::IniConfig::load(framework::utf8_path(argv[1]));if(argc>2){std::cout<<hex(network::service_xml(server::grid_extra_features(config)))<<'\n';return 0;}std::string line;
    while(std::getline(std::cin,line)){
        const auto a=line.find('|'),b=line.find('|',a+1);const auto body=unhex(line.substr(0,a)),authorization=unhex(line.substr(a+1,b-a-1));
        services::NullRegionStore store;const auto previous=store.find("%",{});if(previous)for(const auto& row:*previous)store.erase(row->id);services::GridService service(store,config,[]{return 1700000000;});
        for(int i=1;i<=2;++i){services::GridRegion region;region.id=framework::Uuid("00000000-0000-0000-0000-00000000000"+std::to_string(i));region.name=i==1?"Alpha":"Beta";region.x=25600+(i-1)*256;region.y=25600;region.internal_endpoint=services::RegionEndpoint{"0.0.0.0",9000};region.external_host="127.0.0.1";auto row=std::make_shared<services::RegionData>(services::region_data(region));row->data["flags"]=i==1?"1031":"516";store.store(row);}
        server::GridEndpoint endpoint(service,config,[]{return network::ServiceXmlValue::object({{"ExportSupported","true"}});});
        network::HttpRequest request{"POST","/grid",body,"127.0.0.1","",{}};if(!authorization.empty())request.headers["Authorization"]=authorization;if(line.substr(b+1)=="1")request.headers["X-SecondLife-Shard"]="";
        try{const auto response=endpoint.handle(request);const auto auth=response.headers.find("WWW-Authenticate");std::cout<<response.status<<'|'<<hex(response.body)<<'|'<<hex(response.content_type)<<'|'<<(auth==response.headers.end()?"":hex(auth->second))<<'\n';}catch(const std::invalid_argument&){std::cout<<"BASE64_FORMAT_ERROR\n";}
    }return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
