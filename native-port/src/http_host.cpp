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
#include "opensim/framework/http_host.hpp"
#include "opensim/framework/text.hpp"
#include <charconv>
#include <stdexcept>
namespace opensim::framework {
HttpHost::HttpHost(const std::optional<std::string>& input,bool with_dns,const RegionUri::Resolver& resolver){
    if(!input||input->empty())return;flags=None;auto url=*input;for(auto& c:url)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);
    try{auto length=url.size();if(url[length-1]=='/')--length;std::size_t start=0;bool secure=false;if(url.starts_with("http")){if(url.size()<5)throw std::out_of_range("Incomplete HTTP prefix");if(url[4]=='s'){start=8;secure=true;}else start=7;}if(start>length)throw std::out_of_range("Invalid HTTP host span");const auto colon=url.find(':',start);auto end=length;if(colon!=std::string::npos&&colon<length&&colon>0)end=colon;
        auto candidate=url.substr(start,end-start);if(!valid_http_host(candidate))return;
        if(end!=length){auto text=trim_utf8(std::string_view(url).substr(end+1,length-end-1));if(text.starts_with('+')){text.erase(0,1);if(text.starts_with('+')||text.starts_with('-'))return;}int number=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size()||number<0||number>65535)return;port=number;}else port=secure?443:80;
        flags=ValidHost;host=std::move(candidate);uri=std::string(secure?"https://":"http://")+host+":"+std::to_string(port);if(with_dns){RegionUri lookup("");lookup.flags=RegionUri::HasHost;lookup.host=host;if(lookup.resolve_dns(resolver)){flags=ValidResolved;ip=lookup.ip;}}
    }catch(...){flags=None;ip.reset();uri.clear();}
}
bool HttpHost::resolve_dns(const RegionUri::Resolver& resolver){RegionUri lookup("");lookup.flags=RegionUri::HasHost;lookup.host=host;if(!lookup.resolve_dns(resolver)){flags=static_cast<unsigned char>(flags&~Resolved);return false;}flags=static_cast<unsigned char>(flags|Resolved);return true;}
bool HttpHost::valid_and_resolved(std::string& error)const{if(!valid()){error="failed to parse uri";return false;}if(!resolved()){error="failed DNS resolve of uri host";return false;}error.clear();return true;}
int HttpHost::compare(const HttpHost& other)const{if(port!=other.port||((flags&other.flags)&ValidHost)==0)return -1;return host<other.host?-1:host>other.host?1:0;}
}
