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
#include "opensim/network/service_protocol.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/service_xml_names.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::network {
namespace {
int hex(char16_t c){return c>=u'0'&&c<=u'9'?c-u'0':c>=u'a'&&c<=u'f'?c-u'a'+10:c>=u'A'&&c<=u'F'?c-u'A'+10:-1;}
std::string decode(std::string_view input){
    const auto text=framework::utf16_replacing(input);std::u16string result;std::string bytes;
    const auto flush=[&]{result+=framework::utf16_replacing(bytes);bytes.clear();};
    for(std::size_t i=0;i<text.size();++i){auto c=text[i];if(c==u'+')c=u' ';
        else if(c==u'%'){
            if(i+5<text.size()&&text[i+1]==u'u'&&hex(text[i+2])>=0&&hex(text[i+3])>=0&&hex(text[i+4])>=0&&hex(text[i+5])>=0){flush();result.push_back(static_cast<char16_t>((hex(text[i+2])<<12)|(hex(text[i+3])<<8)|(hex(text[i+4])<<4)|hex(text[i+5])));i+=5;continue;}
            if(i+2<text.size()&&hex(text[i+1])>=0&&hex(text[i+2])>=0){bytes.push_back(static_cast<char>((hex(text[i+1])<<4)|hex(text[i+2])));i+=2;continue;}
        }
        if(c<128)bytes.push_back(static_cast<char>(c));else{flush();result.push_back(c);}
    }flush();return framework::utf8(result);
}
bool blank(std::string_view value){return framework::trim_utf8(value).empty();}
std::string escaped(std::string_view text){
    std::u16string value;constexpr char16_t digits[]=u"0123456789ABCDEF";
    for(auto c:framework::utf16_replacing(text)){switch(c){case u'&':value+=u"&amp;";break;case u'<':value+=u"&lt;";break;case u'>':value+=u"&gt;";break;default:
        if((c<32&&c!=u'\t'&&c!=u'\r'&&c!=u'\n')||c==0xfffe||c==0xffff){value+=u"&#x";bool started=false;for(int shift=12;shift>=0;shift-=4){const auto digit=(c>>shift)&15;if(digit||started||shift==0){value+=digits[digit];started=true;}}value+=u';';}else value+=c;
    }}return framework::utf8(value);
}
void xml_fields(std::string& out,const ServiceXmlValue& value){for(const auto& [key,child]:value.fields){const auto name=service_xml_name(key);if(name.empty())throw std::invalid_argument("Empty service XML key");out+='<'+name;if(child.list){out+=" type=\"List\"";if(child.fields.empty())out+=" />";else{out+='>';xml_fields(out,child);out+="</"+name+'>';}}else{out+='>';out+=escaped(child.text);out+="</"+name+'>';}}}
std::string unbase64(std::string value){
    std::erase_if(value,[](char c){return c==' '||c=='\t'||c=='\r'||c=='\n';});if(value.size()%4)throw std::invalid_argument("Invalid Base64 length");std::string result;
    const auto digit=[](char c){return c>='A'&&c<='Z'?c-'A':c>='a'&&c<='z'?c-'a'+26:c>='0'&&c<='9'?c-'0'+52:c=='+'?62:c=='/'?63:-1;};
    for(std::size_t i=0;i<value.size();i+=4){const auto a=digit(value[i]),b=digit(value[i+1]);if(a<0||b<0)throw std::invalid_argument("Invalid Base64 character");const auto pad=value[i+2]=='='?2:value[i+3]=='='?1:0;if(pad&&(i+4!=value.size()||(pad==2&&value[i+3]!='=')))throw std::invalid_argument("Invalid Base64 padding");const auto c=pad==2?0:digit(value[i+2]),d=pad?0:digit(value[i+3]);if(c<0||d<0)throw std::invalid_argument("Invalid Base64 character");result.push_back(static_cast<char>((a<<2)|(b>>4)));if(pad<2)result.push_back(static_cast<char>((b<<4)|(c>>2)));if(!pad)result.push_back(static_cast<char>((c<<6)|d));}
    return framework::utf8(framework::utf16_replacing(result));
}
std::string header(const HttpRequest& request,std::string wanted,bool& found){for(const auto& [name,value]:request.headers){auto lower=name;for(auto& c:lower)if(c>='A'&&c<='Z')c=static_cast<char>(c+32);if(lower==wanted){found=true;return value;}}found=false;return {};}
}
std::vector<std::string> Form::ordered_keys()const{
    std::vector<std::string> keys;for(const auto& key:insertion_order)if(contains(key)&&std::find(keys.begin(),keys.end(),key)==keys.end())keys.push_back(key);
    for(const auto& [key,value]:*this)if(std::find(keys.begin(),keys.end(),key)==keys.end())keys.push_back(key);return keys;
}
std::string service_xml_name(std::string_view input){
    const auto text=framework::utf16_replacing(input);std::u16string result;
    auto valid=[](char16_t c,const auto& ranges){const auto i=std::lower_bound(ranges.begin(),ranges.end(),c,[](auto r,char16_t n){return r.second<n;});return i!=ranges.end()&&i->first<=c;};
    auto looks_encoded=[&](std::size_t i){if(text[i]!=u'_'||i+6>=text.size()||(text[i+1]!=u'x'&&text[i+1]!=u'X'))return false;for(std::size_t n=2;n<6;++n)if(hex(text[i+n])<0)return false;if(text[i+6]==u'_')return true;if(i+10>=text.size()||text[i+10]!=u'_')return false;for(std::size_t n=6;n<10;++n)if(hex(text[i+n])<0)return false;return true;};
    constexpr char16_t digits[]=u"0123456789ABCDEF";
    for(std::size_t i=0;i<text.size();++i){auto c=text[i];if((i==0?valid(c,detail::xml_name_start):valid(c,detail::xml_name_char))&&!looks_encoded(i)){result+=c;continue;}
        std::uint32_t point=c;int width=4;if(c>=0xd800&&c<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff){point=0x10000+((c-0xd800)<<10)+(text[++i]-0xdc00);width=8;}
        result+=u"_x";for(int n=width-1;n>=0;--n)result+=digits[(point>>(n*4))&15];result+=u'_';
    }return framework::utf8(result);
}
Form parse_service_form(std::string_view input){
    Form result;std::size_t start=0;
    while(start<=input.size()){const auto end=input.find('&',start);auto term=input.substr(start,end==std::string_view::npos?input.size()-start:end-start);const auto equals=term.find('=');const auto raw_name=term.substr(0,equals);
        if(!blank(raw_name)){auto name=decode(raw_name);auto raw_value=equals==std::string_view::npos?std::string_view{}:term.substr(equals+1);raw_value=raw_value.substr(0,raw_value.find('='));const auto value=blank(raw_value)?std::string{}:decode(raw_value);
            if(name.ends_with("[]")){name.resize(name.size()-2);if(!name.empty()){auto found=result.find(name);if(found==result.end()){result.insertion_order.push_back(name);result[name]=std::vector<std::string>{value};}else if(auto* list=std::get_if<std::vector<std::string>>(&found->second))list->push_back(value);}}
            else if(!result.contains(name)){result.insertion_order.push_back(name);result[name]=value;}
        }if(end==std::string_view::npos)break;start=end+1;
    }return result;
}
std::string service_url_decode(std::string_view text){return decode(text);}
std::string service_url_encode(std::string_view text){
    const auto bytes=framework::utf8(framework::utf16_replacing(text));
    constexpr char digits[]="0123456789abcdef";std::string result;
    for(unsigned char c:bytes){
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='!'||c=='('||c==')'||c=='*'||c=='-'||c=='.'||c=='_')result+=static_cast<char>(c);
        else if(c==' ')result+='+';
        else{result+='%';result+=digits[c>>4];result+=digits[c&15];}
    }
    return result;
}
ServiceXmlValue ServiceXmlValue::object(std::vector<std::pair<std::string,ServiceXmlValue>> fields){ServiceXmlValue result;result.fields=std::move(fields);result.list=true;return result;}
std::string service_xml(const ServiceXmlValue& value){if(value.fields.empty())return "<?xml version=\"1.0\"?><ServerResponse />";std::string result="<?xml version=\"1.0\"?><ServerResponse>";xml_fields(result,value);return result+"</ServerResponse>";}
std::string read_service_body(std::string_view bytes){
    auto byte=[&](std::size_t i){return static_cast<unsigned char>(bytes[i]);};
    const bool le32=bytes.starts_with(std::string_view("\xff\xfe\0\0",4)),be32=bytes.starts_with(std::string_view("\0\0\xfe\xff",4));
    if(le32||be32){std::u16string decoded;std::size_t i=4;for(;i+3<bytes.size();i+=4){std::uint32_t c=0;for(int n=0;n<4;++n)c|=std::uint32_t(byte(i+n))<<((le32?n:3-n)*8);if(c>0x10ffff||(c>=0xd800&&c<=0xdfff))c=0xfffd;if(c<0x10000)decoded+=static_cast<char16_t>(c);else{c-=0x10000;decoded+=static_cast<char16_t>(0xd800+(c>>10));decoded+=static_cast<char16_t>(0xdc00+(c&1023));}}if(i<bytes.size())decoded+=u'\ufffd';return framework::utf8(decoded);}
    const bool le16=bytes.starts_with("\xff\xfe"),be16=bytes.starts_with("\xfe\xff");
    if(le16||be16){std::u16string decoded;std::size_t i=2;for(;i+1<bytes.size();i+=2)decoded+=static_cast<char16_t>(le16?byte(i)|(byte(i+1)<<8):(byte(i)<<8)|byte(i+1));if(i<bytes.size())decoded+=u'\ufffd';return framework::utf8(decoded);}
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.remove_prefix(3);return framework::utf8(framework::utf16_replacing(bytes));
}

std::string service_result(bool success){
#ifdef _WIN32
    constexpr auto newline="\r\n";
#else
    constexpr auto newline="\n";
#endif
    return std::string("<?xml version=\"1.0\"?>")+newline+"<ServerResponse>"+newline+"  <result>"+(success?"Success":"Failure")+"</result>"+newline+"</ServerResponse>";
}
ServiceAuth::ServiceAuth(const server::IniConfig& config,const std::string& section):allow_script_requests_(config.boolean(section,"AllowllHTTPRequestIn",config.boolean("Network","AllowllHTTPRequestIn",false))),basic_(config.get(section,"AuthType",config.get("Network","AuthType","None"))=="BasicHttpAuthentication"),username_(config.get(section,"HttpAuthUsername",config.get("Network","HttpAuthUsername"))),password_(config.get(section,"HttpAuthPassword",config.get("Network","HttpAuthPassword"))){}
bool ServiceAuth::authenticate(const HttpRequest& request,HttpResponse& response)const{
    bool found=false;header(request,"x-secondlife-shard",found);if(found&&!allow_script_requests_){response.status=403;response.content_type="text/plain";return false;}if(!basic_)return true;
    auto value=framework::trim_utf8(header(request,"authorization",found));if(found&&value.starts_with("Basic ")){for(auto pos=value.find("Basic ");pos!=std::string::npos;pos=value.find("Basic "))value.erase(pos,6);const auto credentials=unbase64(value);const auto first=credentials.find(':');if(first!=std::string::npos){const auto second=credentials.find(':',first+1);if(credentials.substr(0,first)==username_&&credentials.substr(first+1,second==std::string::npos?second:second-first-1)==password_)return true;}}
    response.status=401;response.content_type="text/plain";response.headers["WWW-Authenticate"]="Basic realm = \"Asset Server\"";return false;
}
}
