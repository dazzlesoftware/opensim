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
#include "opensim/server/config.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/services/sqlite_connection.hpp"
#include <charconv>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace opensim::server {
namespace {
std::string trim(std::string text) {
    const auto start=text.find_first_not_of(" \t\r\n");
    if(start==std::string::npos)return {};
    return text.substr(start,text.find_last_not_of(" \t\r\n")-start+1);
}
}
IniConfig IniConfig::load(const std::filesystem::path& path) {
    std::ifstream input(path);
    if(!input)throw std::runtime_error("Cannot open configuration file: "+path.string());
    IniConfig result; result.directory=std::filesystem::absolute(path).parent_path();
    std::string line,current;
    int number=0;
    while(std::getline(input,line)) {
        ++number;
        if(number==1&&line.starts_with("\xef\xbb\xbf"))line.erase(0,3);
        line=trim(line);
        if(line.empty()||line[0]==';'||line[0]=='#')continue;
        if(line[0]=='[') {
            auto end=line.find(']');
            if(end==std::string::npos)throw std::runtime_error("Invalid INI section at line "+std::to_string(number));
            current=trim(line.substr(1,end-1)); result.sections_.try_emplace(current); continue;
        }
        auto equals=line.find('=');
        if(equals==std::string::npos||current.empty())throw std::runtime_error("Invalid INI setting at line "+std::to_string(number));
        auto key=trim(line.substr(0,equals)),value=trim(line.substr(equals+1));
        if(key.starts_with("Include-"))throw std::runtime_error("INI Include-* loading is not ported yet; use an explicit native configuration");
        if(!value.empty()&&value.front()=='"') {
            const auto end=value.find('"',1);
            if(end==std::string::npos)throw std::runtime_error("Unterminated INI quoted value");
            value=value.substr(1,end-1);
        } else { const auto comment=value.find(';'); if(comment!=std::string::npos)value=trim(value.substr(0,comment)); }
        result.sections_[current][key]=value;
    }
    if(input.bad())throw std::runtime_error("Configuration read failed");
    return result;
}
std::string IniConfig::get(const std::string& section_name,const std::string& key,const std::string& fallback) const {
    const auto found=sections_.find(section_name);
    if(found==sections_.end())return fallback;
    const auto value=found->second.find(key);
    if(value==found->second.end())return fallback;
    std::string result=value->second;
    // Nini-style section/key interpolation used by the supplied server config.
    // A thread-local bound detects cycles across recursive calls.
    static thread_local unsigned depth=0;
    struct Guard { Guard(){if(++depth>16){--depth;throw std::runtime_error("INI interpolation cycle");}} ~Guard(){--depth;} } guard;
    for(std::size_t start=result.find("${");start!=std::string::npos;start=result.find("${")) {
        const auto end=result.find('}',start+2);
        if(end==std::string::npos)throw std::runtime_error("Unterminated INI interpolation");
        auto reference=result.substr(start+2,end-start-2);
        auto separator=reference.find('|');
        const auto target_section=separator==std::string::npos?section_name:reference.substr(0,separator);
        const auto target_key=separator==std::string::npos?reference:reference.substr(separator+1);
        auto replacement=get(target_section,target_key,"");
        result.replace(start,end-start+1,replacement);
    }
    return result;
}
int IniConfig::integer(const std::string& section_name,const std::string& key,int fallback) const {
    const auto value=get(section_name,key,std::to_string(fallback)); int result;
    auto parse=std::from_chars(value.data(),value.data()+value.size(),result);
    if(parse.ec!=std::errc{}||parse.ptr!=value.data()+value.size())throw std::runtime_error("Invalid integer configuration: "+section_name+"."+key);
    return result;
}
bool IniConfig::boolean(const std::string& section_name,const std::string& key,bool fallback) const {
    auto value=get(section_name,key,fallback?"true":"false");
    for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(value=="true"||value=="yes"||value=="1")return true;
    if(value=="false"||value=="no"||value=="0")return false;
    throw std::runtime_error("Invalid boolean configuration: "+section_name+"."+key);
}
std::map<std::string,std::string> IniConfig::section(const std::string& name) const {
    std::map<std::string,std::string> result;
    auto found=sections_.find(name);if(found!=sections_.end())for(const auto& [key,value]:found->second)result[key]=get(name,key);
    return result;
}
std::string database_path(const IniConfig& config,const std::string& service,const std::string& native_key) {
    auto path=config.get(service,"ConnectionString",config.get("NativeServer",native_key,"native.db"));
    if(path.find('=')!=std::string::npos)
        path=services::sqlite_connection_path(services::parse_sqlite_connection(path));
    if(path==":memory:")return path;
    auto resolved=framework::utf8_path(path);
    if(resolved.is_relative())resolved=config.directory/resolved;
    auto utf8=resolved.lexically_normal().u8string();
    return {reinterpret_cast<const char*>(utf8.data()),utf8.size()};
}
}
