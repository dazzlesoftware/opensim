// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/seed_caps.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/http_uri.hpp"
#include <pugixml.hpp>
#include <charconv>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <unordered_set>
#include <sstream>
#include <stdexcept>
namespace opensim::server {
namespace {
using network::Value;
struct Token {enum Kind{Start,End,Text,White,Comment,Declaration,Other} kind;std::string name,text;bool empty=false;pugi::xml_node node;};
using Node=SdkLlsdNode;
std::string local(std::string_view name){auto colon=name.find(':');return std::string(name.substr(colon==name.npos?0:colon+1));}
bool white(std::string_view text){return text.find_first_not_of(" \t\r\n")==text.npos;}
std::string date_string(std::string text){
    Value date;date.kind=Value::Kind::Date;date.text=text;
    if(text.size()>19&&(text.back()=='Z'||text.back()=='z'||text.find('+',19)!=text.npos||text.find('-',19)!=text.npos))try{return network::osd_string(date);}catch(const std::exception&){}
    std::tm calendar{};bool parsed=false;int milliseconds=0;
    for(const char* format:{"%Y-%m-%dT%H:%M:%S","%Y-%m-%d","%m/%d/%Y"}){std::istringstream input(text);input.imbue(std::locale::classic());std::tm value{};input>>std::get_time(&value,format);int ms=0;if(!input.fail()&&input.rdbuf()->sgetc()=='.'){input.rdbuf()->sbumpc();int count=0;while(input.rdbuf()->sgetc()>='0'&&input.rdbuf()->sgetc()<='9'){int digit=input.rdbuf()->sbumpc()-'0';if(count<3)ms=ms*10+digit;++count;}while(count++<3)ms*=10;}using namespace std::chrono;const year_month_day day{year{value.tm_year+1900},month{static_cast<unsigned>(value.tm_mon+1)},std::chrono::day{static_cast<unsigned>(value.tm_mday)}};if(!input.fail()&&input.rdbuf()->sgetc()==std::char_traits<char>::eof()&&day.ok()&&int(day.year())>=1&&int(day.year())<=9999&&value.tm_hour<24&&value.tm_min<60&&value.tm_sec<60){calendar=value;milliseconds=ms;parsed=true;break;}}
    if(!parsed){calendar={};calendar.tm_year=70;calendar.tm_mday=1;}
    calendar.tm_isdst=-1;const auto instant=std::mktime(&calendar);std::tm utc{};
#ifdef _WIN32
    if(gmtime_s(&utc,&instant)!=0)throw std::invalid_argument("Invalid LLSD date");
#else
    if(!gmtime_r(&instant,&utc))throw std::invalid_argument("Invalid LLSD date");
#endif
    std::ostringstream output;output.imbue(std::locale::classic());output<<std::put_time(&utc,"%Y-%m-%dT%H:%M:%S");if(milliseconds>0)output<<'.'<<std::setfill('0')<<std::setw(2)<<milliseconds/10;output<<'Z';return output.str();
}
std::string decoded(std::string_view text){std::string out;for(std::size_t at=0;at<text.size();){if(text[at]!='&'){out+=text[at++];continue;}auto end=text.find(';',at);if(end==text.npos)throw std::invalid_argument("Invalid XML entity");auto value=text.substr(at+1,end-at-1);if(value=="amp")out+='&';else if(value=="lt")out+='<';else if(value=="gt")out+='>';else if(value=="apos")out+='\'';else if(value=="quot")out+='"';else{
    if(!value.starts_with('#'))throw std::invalid_argument("Unknown XML entity");value.remove_prefix(1);int base=10;if(value.starts_with('x')){base=16;value.remove_prefix(1);}std::uint32_t number=0;auto parsed=std::from_chars(value.data(),value.data()+value.size(),number,base);if(value.empty()||parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size())throw std::invalid_argument("Invalid XML character entity");std::u16string units;if(number<65536)units.push_back(static_cast<char16_t>(number));else{number-=65536;units.push_back(static_cast<char16_t>(0xd800+(number>>10)));units.push_back(static_cast<char16_t>(0xdc00+(number&1023)));}out+=framework::utf8(units);
    }at=end+1;}return out;}
void bound_prefix(pugi::xml_node node,std::string_view name){auto colon=name.find(':');if(colon==name.npos)return;auto prefix=name.substr(0,colon);if(prefix=="xml"||prefix=="xmlns")return;const auto attribute="xmlns:"+std::string(prefix);for(auto parent=node;parent;parent=parent.parent())if(auto binding=parent.attribute(attribute.c_str());binding&&binding.value()[0])return;throw std::invalid_argument("Unbound XML prefix");}
struct Lex {enum Kind{Start,End,White,Comment,Declaration,Other} kind;std::size_t end;std::string name;bool empty=false;};
Lex lex(std::string_view source,std::size_t at){if(at>=source.size())throw std::invalid_argument("Unexpected XML EOF");auto finish=[&](std::string_view ending,std::size_t from){auto end=source.find(ending,from);if(end==source.npos)throw std::invalid_argument("Unfinished XML token");return end+ending.size();};
    if(source[at]!='<'){auto end=source.find('<',at);if(end==source.npos)end=source.size();return {white(source.substr(at,end-at))?Lex::White:Lex::Other,end};}
    if(source.substr(at).starts_with("<!--"))return {Lex::Comment,finish("-->",at+4)};
    if(source.substr(at).starts_with("<![CDATA["))return {Lex::Other,finish("]]>",at+9)};
    if(source.substr(at).starts_with("<?"))return {source.substr(at).starts_with("<?xml ")?Lex::Declaration:Lex::Other,finish("?>",at+2)};
    if(source.substr(at).starts_with("<!DOCTYPE")){char quote=0;int subset=0;for(auto i=at+9;i<source.size();++i){char c=source[i];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='[')++subset;else if(c==']')--subset;else if(c=='>'&&subset==0)return {Lex::Declaration,i+1};}throw std::invalid_argument("Unfinished doctype");}
    bool end=source.substr(at).starts_with("</");auto begin=at+(end?2:1);auto stop=source.find_first_of(" \t\r\n/>",begin);if(stop==source.npos||stop==begin)throw std::invalid_argument("Invalid XML tag");std::string name(source.substr(begin,stop-begin));char quote=0;for(auto i=stop;i<source.size();++i){char c=source[i];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='>')return {end?Lex::End:Lex::Start,i+1,std::move(name),i>0&&source[i-1]=='/'};}throw std::invalid_argument("Unfinished XML tag");
}
// XmlTextReader validates on demand. The SDK reads one token beyond the first
// LLSD value and returns; unrelated trailing input is never consumed. Build a
// complete DOM only for that consumed prefix, retaining the real next token.
std::string consumed_prefix(std::string_view source){std::size_t at=source.starts_with("\xef\xbb\xbf")?3:0;Lex token;
    do{token=lex(source,at);at=token.end;}while(token.kind==Lex::White||token.kind==Lex::Comment||token.kind==Lex::Declaration);
    if(token.kind!=Lex::Start||token.empty)return std::string(source);std::vector<std::string> stack={token.name};
    do{token=lex(source,at);at=token.end;}while(token.kind==Lex::White||token.kind==Lex::Comment);
    if(token.kind!=Lex::Start)return std::string(source);if(!token.empty)stack.push_back(token.name);
    while(stack.size()>1){token=lex(source,at);at=token.end;if(token.kind==Lex::Start&&!token.empty)stack.push_back(token.name);else if(token.kind==Lex::End){if(stack.back()!=token.name)throw std::invalid_argument("Mismatched XML tag");stack.pop_back();}}
    token=lex(source,at);at=token.end;if(token.kind==Lex::Start&&!token.empty)stack.push_back(token.name);else if(token.kind==Lex::End){if(stack.back()!=token.name)throw std::invalid_argument("Mismatched XML tag");stack.pop_back();}
    std::string prefix(source.substr(0,at));for(auto i=stack.rbegin();i!=stack.rend();++i)prefix+="</"+*i+">";return prefix;
}
class Reader {
    std::vector<Token> tokens_;std::size_t index_=0;std::string_view source_;
    bool empty(pugi::xml_node node){auto offset=node.offset_debug();if(offset<0)return false;char quote=0;for(auto i=static_cast<std::size_t>(offset);i<source_.size();++i){char c=source_[i];if(quote){if(c==quote)quote=0;}else if(c=='\''||c=='"')quote=c;else if(c=='>')return i>0&&source_[i-1]=='/';}return false;}
    void append(pugi::xml_node node){switch(node.type()){
        case pugi::node_element:{bool e=empty(node);tokens_.push_back({Token::Start,local(node.name()),{},e,node});if(!e){for(auto child:node.children())append(child);tokens_.push_back({Token::End,local(node.name())});}break;}
        case pugi::node_pcdata:case pugi::node_cdata:{std::string text=node.value();tokens_.push_back({node.type()!=pugi::node_cdata&&white(text)?Token::White:Token::Text,{},std::move(text),false,node});break;}
        case pugi::node_comment:tokens_.push_back({Token::Comment});break;
        case pugi::node_declaration:tokens_.push_back({Token::Declaration});break;
        case pugi::node_pi:tokens_.push_back({Token::Other});break;
        default:break;
    }}
public:
    Reader(pugi::xml_document& doc,std::string_view source):source_(source){for(auto child:doc.children())append(child);tokens_.push_back({Token::Other});}
    const Token& current()const{const auto& token=tokens_.at(index_);if(token.kind==Token::Start){bound_prefix(token.node,token.node.name());std::unordered_set<std::string> attributes;for(auto a:token.node.attributes()){bound_prefix(token.node,a.name());if(!attributes.insert(a.name()).second)throw std::invalid_argument("Duplicate XML attribute");}}return token;}
    void read(){if(index_+1<tokens_.size())++index_;(void)current();}
    void skip(){while(current().kind==Token::White||current().kind==Token::Comment||current().kind==Token::Declaration)read();}
    std::string read_string(){if(current().kind==Token::Start){if(current().empty)return {};read();}std::string result;while(current().kind==Token::Text||current().kind==Token::White){if(current().node.type()==pugi::node_pcdata){auto at=current().node.offset_debug();auto end=source_.find('<',static_cast<std::size_t>(at));result+=decoded(source_.substr(static_cast<std::size_t>(at),end-static_cast<std::size_t>(at)));}else result+=current().text;read();}return result;}
    Node parse(){skip();auto token=current();if(token.kind!=Token::Start)throw std::invalid_argument("Expected LLSD XML element");const auto& tag=token.name;Node out;out.value=Value{};out.tag=tag;
        if(tag=="array"||tag=="map"){
            out.value->kind=tag=="array"?Value::Kind::Array:Value::Kind::Map;read();if(token.empty)return out;
            for(;;){skip();if(current().kind==Token::End&&current().name==tag){read();return out;}
                if(tag=="map"){if(current().kind!=Token::Start||current().name!="key")throw std::invalid_argument("Expected LLSD key");out.keys.push_back(read_string());if(current().kind!=Token::End||current().name!="key")throw std::invalid_argument("Expected key end");read();}
                out.children.push_back(parse());
            }
        }
        if(tag=="undef"){read();if(token.empty)return out;skip();}
        else if(tag=="boolean"||tag=="integer"||tag=="real"||tag=="uuid"||tag=="date"||tag=="string"||tag=="binary"||tag=="uri"){
            if(tag=="binary"&&!token.empty){auto encoding=token.node.attribute("encoding");if(encoding&&std::string_view(encoding.value())!="base64")throw std::invalid_argument("Unsupported binary encoding");}
            read();auto text=token.empty?std::string{}:read_string();
            if(tag=="string")*out.value=Value(text);
            else if(tag=="boolean")*out.value=Value(framework::trim_utf8(text)=="true"||framework::trim_utf8(text)=="1");
            else if(tag=="integer"){auto trimmed=framework::trim_utf8(text);while(!trimmed.empty()&&trimmed.back()=='\0')trimmed.pop_back();std::string_view v=trimmed;if(v.starts_with('+'))v.remove_prefix(1);std::int32_t n=0;auto parsed=std::from_chars(v.data(),v.data()+v.size(),n);*out.value=Value(parsed.ec==std::errc{}&&parsed.ptr==v.data()+v.size()?n:0);}
            else if(tag=="real"){out.value->kind=Value::Kind::Real;auto number=framework::trim_utf8(text);for(auto& c:number)if(c>='A'&&c<='Z')c+=32;out.value->real=number=="∞"||number=="+∞"?std::numeric_limits<double>::infinity():number=="-∞"?-std::numeric_limits<double>::infinity():number.find(',')!=number.npos||number.find("infinity")!=number.npos?0:network::osd_real(Value(number));}
            else if(tag=="uuid"){out.value->kind=Value::Kind::Uuid;out.value->text=network::osd_uuid(Value(text)).str();}
            else if(tag=="date"){*out.value=Value(date_string(framework::trim_utf8(text)));}
            else if(tag=="uri"){auto trimmed=framework::trim_utf8(text);std::string lower=trimmed;for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;if((lower.starts_with("http:")||lower.starts_with("https:"))&&!framework::HttpUri(trimmed).valid())throw std::invalid_argument("Invalid LLSD URI");out.value->kind=Value::Kind::Uri;out.value->text=text;}
            else {out.value->kind=Value::Kind::Binary;out.value->text=framework::trim_utf8(text);network::osd_binary(*out.value);}
            if(token.empty)return out;
        }
        else {read();out.value=std::nullopt;}
        if(current().kind!=Token::End||current().name!=tag)throw std::invalid_argument("Expected LLSD element end");read();return out;
    }
};
}
SdkLlsdNode deserialize_sdk_llsd_node(std::string_view xml){
    std::string normalized;if(xml.size()>=2&&((static_cast<unsigned char>(xml[0])==255&&static_cast<unsigned char>(xml[1])==254)||(static_cast<unsigned char>(xml[0])==254&&static_cast<unsigned char>(xml[1])==255)||xml[0]=='\0'||xml[1]=='\0')){bool little=static_cast<unsigned char>(xml[0])==255||xml[1]=='\0';std::size_t at=(static_cast<unsigned char>(xml[0])>=254)?2:0;std::u16string units;for(;at+1<xml.size();at+=2){auto a=static_cast<unsigned char>(xml[at]),b=static_cast<unsigned char>(xml[at+1]);units.push_back(static_cast<char16_t>(little?a|(b<<8):(a<<8)|b));}if(at!=xml.size())throw std::invalid_argument("Odd UTF16 byte count");normalized=framework::utf8(units);xml=normalized;}
    auto prefix=consumed_prefix(xml);xml=prefix;
    for(auto c:framework::utf16(xml))if((c<32&&c!=9&&c!=10&&c!=13)||c==0xfffe||c==0xffff)throw std::invalid_argument("Invalid XML character");
    pugi::xml_document doc;auto result=doc.load_buffer(xml.data(),xml.size(),pugi::parse_full|pugi::parse_ws_pcdata,pugi::encoding_utf8);if(!result){auto root=doc.document_element();auto closing="</"+std::string(root.name())+">";auto at=xml.find(closing);if(!root||at==xml.npos||at+closing.size()>static_cast<std::size_t>(result.offset))throw std::invalid_argument("Malformed LLSD XML");xml=xml.substr(0,at+closing.size());result=doc.load_buffer(xml.data(),xml.size(),pugi::parse_full|pugi::parse_ws_pcdata,pugi::encoding_utf8);if(!result)throw std::invalid_argument("Malformed LLSD XML");}
    Reader reader(doc,xml);reader.skip();reader.read();return reader.parse();
}
std::optional<services::Caps::Requested> deserialize_seed_caps(std::string_view xml){
    auto root=deserialize_sdk_llsd_node(xml);if(!root.value)return std::nullopt;if(root.value->kind!=Value::Kind::Array)throw std::invalid_argument("Expected OSDArray");
    services::Caps::Requested names;for(const auto& item:root.children)names.push_back(item.value?std::optional<std::string>(network::osd_string(*item.value)):std::nullopt);return names;
}
}
