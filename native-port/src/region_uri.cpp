/*
 * Copyright (c) Contributors, http://opensimulator.org/
 * See CONTRIBUTORS.TXT for a full list of copyright holders.
 *
 * - Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Neither the name of the openmetaverse.org nor the names
 *   of its contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "opensim/framework/region_uri.hpp"
#include "opensim/framework/host_resolver.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <arpa/inet.h>
#endif
namespace opensim::framework {
namespace {
void trim_start(std::string_view& value,std::string_view chars){const auto first=value.find_first_not_of(chars);value.remove_prefix(first==std::string_view::npos?value.size():first);}
int nibble(char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;}
std::string unescape(std::string_view value){
    std::string result;std::size_t i=0;while(i<value.size()){
        if(value[i]!='%'||i+2>=value.size()||nibble(value[i+1])<0||nibble(value[i+2])<0){result+=value[i++];continue;}
        const auto byte=static_cast<unsigned char>((nibble(value[i+1])<<4)|nibble(value[i+2]));std::size_t count=byte<0x80?1:byte>=0xc2&&byte<=0xdf?2:byte>=0xe0&&byte<=0xef?3:byte>=0xf0&&byte<=0xf4?4:0;std::string decoded;bool valid=count!=0;
        for(std::size_t j=0;j<count&&valid;++j){const auto pos=i+3*j;if(pos+2>=value.size()||value[pos]!='%'||nibble(value[pos+1])<0||nibble(value[pos+2])<0){valid=false;break;}const auto c=static_cast<unsigned char>((nibble(value[pos+1])<<4)|nibble(value[pos+2]));if(j&&(c<0x80||c>0xbf||(j==1&&((byte==0xe0&&c<0xa0)||(byte==0xed&&c>=0xa0)||(byte==0xf0&&c<0x90)||(byte==0xf4&&c>=0x90)))))valid=false;decoded+=static_cast<char>(c);}
        if(valid){result+=decoded;i+=3*count;}else{result.append(value.substr(i,3));i+=3;}
    }std::replace(result.begin(),result.end(),'+',' ');return result;
}
bool valid_host(const std::string& host){
    if(host.empty()||host.size()>255)return false;auto value=std::string_view(host);if(value.ends_with('.'))value.remove_suffix(1);if(value.empty())return false;std::size_t start=0;
    while(start<value.size()){const auto end=value.find('.',start);const auto label=value.substr(start,end==std::string_view::npos?value.size()-start:end-start);if(label.empty()||label.size()>63)return false;for(unsigned char c:label)if(c<0x80&&!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;if(label.front()=='-'||label.front()=='_')return false;if(end==std::string_view::npos)break;start=end+1;}return true;
}
std::string legacy_bytes(std::string value){if(value.ends_with('\0'))value.pop_back();const auto nul=value.find('\0');if(nul!=std::string::npos)std::fill(value.begin()+static_cast<std::ptrdiff_t>(nul),value.end(),'\0');return value;}
std::string decoded_text(std::string_view value){return utf8(utf16_replacing(value));}
std::int32_t digits(std::string_view value,std::size_t& used){std::uint32_t result=0;used=0;while(used<value.size()&&value[used]>='0'&&value[used]<='9'){result=result*10u+static_cast<unsigned>(value[used]-'0');++used;}return std::bit_cast<std::int32_t>(result);}
std::optional<std::string> resolve(const std::string& host){auto address=host_resolver().host_from_dns(host);return address?std::optional<std::string>(address->str()):std::nullopt;}
}
bool valid_http_host(const std::string& host){return valid_host(host);}
RegionUri::RegionUri(const std::optional<std::string>& value):original(value){parse(value);if(!has_host()&&has_region_name())flags|=IsLocalGrid;}
RegionUri::RegionUri(const std::optional<std::string>& value,const LocalGrid& grid,const Resolver& resolver):original(value){parse(value);if(!has_host()){if(!has_region_name())flags=0;else flags|=IsLocalGrid;return;}if(!grid)return;if(grid(host_url())==1){set_local(true);return;}if(!resolve_dns(resolver))flags=0;}
void RegionUri::parse(const std::optional<std::string>& value){
    flags=0;if(!value||trim_utf8(*value).empty())return;const auto backing=legacy_bytes(*value);auto input=std::string_view(backing);
    const auto sub=[&](std::string_view view,std::size_t start,std::size_t length=std::string_view::npos){if(start>=view.size())return view.substr(view.size());length=std::min(length,view.size()-start);if(!length)return view.substr(start,0);auto end=start+length-1;const auto absolute=static_cast<std::size_t>(view.data()-backing.data())+start;if(absolute>0&&(static_cast<unsigned char>(view[start])&0x80)){if((static_cast<unsigned char>(view[start])&0xc0)!=0xc0)throw std::invalid_argument("Invalid source slice start");if(end>start)--end;else return view.substr(start,0);}if(end>start&&(static_cast<unsigned char>(view[end])&0x80)){do{--end;}while(end>start&&(static_cast<unsigned char>(view[end])&0xc0)!=0xc0);}return view.substr(start,end-start+1);};trim_start(input," ");trim_start(input,"+");auto first_dot=input.find('.');if(first_dot==0)return;
    auto separator=input.find("://");if(separator==0)return;if(separator==std::string_view::npos)separator=input.find("|!!");if(separator==0)return;
    if(separator!=std::string_view::npos){if(separator<2||input.size()<separator+4||(first_dot!=std::string_view::npos&&separator>first_dot))return;auto prefix=decoded_text(sub(input,0,separator));for(auto& c:prefix)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);bool secure=false;if(prefix.ends_with('s')){secure=true;while(prefix.ends_with('s'))prefix.pop_back();}if(prefix!="http"&&prefix!="hg"&&prefix!="hop"&&prefix!="surl"&&prefix!="x-grid-info")return;if(secure){schema="https://";port=443;}const auto amount=separator+3;input=sub(input,amount);if(first_dot!=std::string_view::npos)first_dot-=amount;}
    std::size_t name_start=0;
    if(first_dot!=std::string_view::npos&&first_dot>0){auto host_part=input;const auto port_sep=input.find_first_of(":|");std::size_t host_end=std::string_view::npos;
        if(port_sep!=std::string_view::npos&&port_sep>0){if(port_sep<first_dot)return;host_end=port_sep;std::size_t used=0;const auto parsed=digits(input.substr(port_sep+1),used);if(used){if(parsed>65536)return;port=parsed;}input=sub(input,port_sep+1+used);trim_start(input,":/ +|");}
        else{const auto end=input.find_first_of(":/ +|");if(end==std::string_view::npos){host_end=input.size();name_start=std::string_view::npos;}else{host_end=end;name_start=end+1;}}
        if(host_end==0||host_end==std::string_view::npos)return;host_part=sub(host_part,0,host_end);const auto at=host_part.find('@');if(at!=std::string_view::npos){if(at>0){auto user=sub(host_part,0,at);username=user.find_first_of("+%")!=std::string_view::npos?unescape(decoded_text(user)):decoded_text(user);if(!username.empty())flags|=HasUserName;}host_part=sub(host_part,at+1);}if(host_part.empty()){flags=0;return;}
        host=host_part.find_first_of("+%")!=std::string_view::npos?decoded_text(legacy_bytes(unescape(decoded_text(host_part)))):decoded_text(host_part);for(auto& c:host)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);if(!valid_host(host)){flags=0;return;}flags|=HasHost;
    }
    if(name_start==std::string_view::npos||input.empty())return;input=sub(input,name_start);trim_start(input," ");trim_start(input,"+");auto coordinate=input.find('/');if(coordinate==0){flags=0;return;}if(coordinate==std::string_view::npos)coordinate=input.find('(');auto name=coordinate!=std::string_view::npos&&coordinate>0?sub(input,0,coordinate):input;auto decoded=name.find_first_of("+%")!=std::string_view::npos?decoded_text(legacy_bytes(unescape(decoded_text(name)))):decoded_text(name);while(decoded.ends_with(' '))decoded.pop_back();if(decoded.empty())return;const auto forbidden=decoded.find_first_of(".,:;\\/");if(forbidden!=std::string::npos&&forbidden>0){flags=0;return;}region_name=decoded;flags|=HasRegionName;
    if(coordinate==std::string_view::npos||coordinate==0)return;const bool tuple=input[coordinate]=='(';auto tail=sub(input,coordinate+1);if(tuple){const auto close=tail.find(')');if(close==0)return;if(close!=std::string_view::npos)tail=sub(tail,0,close);trim_start(tail," ");trim_start(tail,"+");}
    std::size_t used=0;auto parsed=digits(tail,used);if(used==0){flags=0;return;}x=parsed;tail=sub(tail,used);const auto second=tail.find(tuple?',':'/');if(second==std::string_view::npos)return;tail=sub(tail,second+1);if(tuple){trim_start(tail," ");trim_start(tail,"+");}parsed=digits(tail,used);if(used==0){flags=0;return;}y=parsed;tail=sub(tail,used);const auto third=tail.find(tuple?',':'/');if(third==std::string_view::npos)return;tail=sub(tail,third+1);if(tuple){trim_start(tail," ");trim_start(tail,"+");}bool negative=false;if(tail.starts_with('-')){negative=true;tail.remove_prefix(1);}parsed=digits(tail,used);if(!used&&!negative){flags=0;return;}z=negative?std::bit_cast<std::int32_t>(0u-static_cast<std::uint32_t>(parsed)):parsed;
}
bool RegionUri::resolve_dns(const Resolver& resolver){if(!has_host())return false;const auto address=resolver?resolver(host):resolve(host);if(!address)return false;ip=address;flags|=HasResolvedHost;return true;}
void RegionUri::set_local(bool value){if(value){host.clear();flags&=~HasHost;flags|=IsLocalGrid;}}
std::string RegionUri::host_url()const{return has_host()?schema+host+":"+std::to_string(port):"";}
std::string RegionUri::host_url_slash()const{return has_host()?host_url()+"/":"";}
std::string RegionUri::region_url_name()const{return host_url_slash()+(has_region_name()?region_name:"");}
std::string RegionUri::host_port_space_name()const{return (has_host()?host+":"+std::to_string(port)+"/ ":"")+(has_region_name()?region_name:"");}
}
