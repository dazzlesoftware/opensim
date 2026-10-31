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
#include "opensim/framework/http_uri.hpp"
#include <algorithm>
#include <charconv>
#include <vector>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif
namespace opensim::framework {
namespace {
std::string lower(std::string value){for(auto& c:value)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);return value;}
int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
bool unreserved(unsigned char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='.'||c=='_'||c=='~';}
std::string escape(std::string_view value,bool path){std::string result;constexpr char hex[]="0123456789ABCDEF";for(std::size_t i=0;i<value.size();++i){auto c=static_cast<unsigned char>(value[i]);if(c=='%'&&i+2<value.size()&&nibble(value[i+1])>=0&&nibble(value[i+2])>=0){const auto decoded=static_cast<unsigned char>((nibble(value[i+1])<<4)|nibble(value[i+2]));if(unreserved(decoded))result+=static_cast<char>(decoded);else result.append(value.substr(i,3));i+=2;continue;}if(unreserved(c)||std::string_view("/:@!$&'()*+,;=[]").find(static_cast<char>(c))!=std::string_view::npos||(!path&&(c=='?'||c=='#')))result+=static_cast<char>(c);else{result+='%';result+=hex[c>>4];result+=hex[c&15];}}return result;}
std::string normalized_path(std::string value){std::replace(value.begin(),value.end(),'\\','/');value=escape(value,true);std::vector<std::string> segments;std::size_t start=1;bool terminal=false;while(start<=value.size()){const auto end=value.find('/',start);const auto segment=value.substr(start,end==std::string::npos?end:end-start);terminal=end==std::string::npos&&(segment=="."||segment=="..");if(segment==".."){if(!segments.empty())segments.pop_back();}else if(segment!=".")segments.push_back(segment);if(end==std::string::npos)break;start=end+1;}std::string result="/";for(std::size_t i=0;i<segments.size();++i){if(i)result+='/';result+=segments[i];}if(terminal&&!result.ends_with('/'))result+='/';return result;}
std::optional<std::string> numeric(const std::string& host){
#ifdef _WIN32
 struct Runtime {Runtime(){WSADATA data;WSAStartup(MAKEWORD(2,2),&data);}~Runtime(){WSACleanup();}};static Runtime runtime;
#endif
 addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_flags=AI_NUMERICHOST;addrinfo* found=nullptr;if(getaddrinfo(host.c_str(),nullptr,&hints,&found)!=0)return std::nullopt;std::unique_ptr<addrinfo,decltype(&freeaddrinfo)> guard(found,freeaddrinfo);char text[INET6_ADDRSTRLEN]{};const void* bytes=found->ai_family==AF_INET?static_cast<void*>(&reinterpret_cast<sockaddr_in*>(found->ai_addr)->sin_addr):static_cast<void*>(&reinterpret_cast<sockaddr_in6*>(found->ai_addr)->sin6_addr);if(!inet_ntop(found->ai_family,bytes,text,sizeof(text)))return std::nullopt;return std::string(text);
}
}
HttpUri::HttpUri(const std::optional<std::string>& input,bool with_dns,const RegionUri::Resolver& resolver):HttpHost(std::nullopt){port=-1;if(!input||input->empty())return;flags=None;
 try{auto value=*input;const auto first=value.find_first_not_of(" \t\r\n");if(first==std::string::npos)return;value=value.substr(first,value.find_last_not_of(" \t\r\n")-first+1);auto colon=value.find(':');if(colon==std::string::npos)return;const auto schema=lower(value.substr(0,colon));if(schema!="http"&&schema!="https")return;const auto start=colon+3;if(value.size()<start||((value[colon+1]!='/'&&value[colon+1]!='\\')||(value[colon+2]!='/'&&value[colon+2]!='\\')))return;auto end=value.find_first_of("/\\?#",start);if(end==std::string::npos)end=value.size();auto authority=value.substr(start,end-start);auto at=authority.rfind('@');std::string user;if(at!=std::string::npos){if(authority.find('@')!=at)return;user=escape(authority.substr(0,at),true)+"@";authority.erase(0,at+1);}if(authority.empty())return;
 std::string candidate;std::string sport;bool explicit_port=false,ipv6=false;if(authority.starts_with('[')){const auto close=authority.find(']');if(close==std::string::npos)return;candidate=authority.substr(1,close-1);ipv6=true;if(close+1<authority.size()){if(authority[close+1]!=':')return;explicit_port=true;sport=authority.substr(close+2);}}else{const auto split=authority.find(':');if(split!=std::string::npos){candidate=authority.substr(0,split);explicit_port=true;sport=authority.substr(split+1);}else candidate=authority;}
 candidate=lower(candidate);const auto address=numeric(candidate);if(address)candidate=*address;else{auto check=candidate;for(std::size_t i=0;i<check.size();++i)if((i==0||check[i-1]=='.')&&(check[i]=='-'||check[i]=='_'))check[i]='a';if(ipv6||!valid_http_host(check))return;}int parsed_port=schema=="https"?443:80;if(explicit_port&&!sport.empty()){if(!std::all_of(sport.begin(),sport.end(),[](char c){return c>='0'&&c<='9';}))return;const auto parsed=std::from_chars(sport.data(),sport.data()+sport.size(),parsed_port);if(parsed.ec!=std::errc{}||parsed.ptr!=sport.data()+sport.size()||parsed_port<0||parsed_port>65535)return;}
 const auto path_end=value.find_first_of("?#",end);auto raw_path=value.substr(end,path_end==std::string::npos?path_end:path_end-end);if(raw_path.empty())raw_path="/";host=std::move(candidate);port=parsed_port;path=normalized_path(raw_path);url=schema+"://"+host+":"+std::to_string(port);uri=schema+"://"+user+(ipv6?"["+host+"]":host);if(port!=(schema=="https"?443:80))uri+=":"+std::to_string(port);uri+=path;if(path_end!=std::string::npos)uri+=escape(value.substr(path_end),false);flags=ValidHost;
 if(with_dns){RegionUri lookup("");lookup.flags=RegionUri::HasHost;lookup.host=host;if(lookup.resolve_dns(resolver)){ip=lookup.ip;flags=ValidResolved;}}
 }catch(...){flags=None;ip.reset();uri.clear();}
}
}
