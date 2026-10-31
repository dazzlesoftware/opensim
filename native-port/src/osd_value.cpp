// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_value.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/sdk_boolean_ignorables.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/http_uri.hpp"
#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <limits>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace opensim::network {
namespace {
bool white(char16_t c){return c==' '||(c>='\t'&&c<='\r');}
bool number(std::string_view text,double& value){
    const auto storage=framework::utf16_replacing(text);auto special=std::u16string_view(storage);
    while(!special.empty()&&framework::unicode_whitespace(special.front()))special.remove_prefix(1);
    while(!special.empty()&&framework::unicode_whitespace(special.back()))special.remove_suffix(1);
    bool negative=false;if(!special.empty()&&(special.front()=='+'||special.front()=='-')){negative=special.front()=='-';special.remove_prefix(1);}
    std::u16string lower(special);for(auto& c:lower)if(c>='A'&&c<='Z')c+=32;
    if(lower==u"nan"){value=std::bit_cast<double>(0xfff8000000000000ull);return true;}
    if(lower==u"infinity"){value=negative?-std::numeric_limits<double>::infinity():std::numeric_limits<double>::infinity();return true;}
    auto input=std::u16string_view(storage);while(!input.empty()&&white(input.front()))input.remove_prefix(1);
    if(input.empty())return false;negative=false;if(input.front()=='+'||input.front()=='-'){negative=input.front()=='-';input.remove_prefix(1);}
    std::size_t position=0,digits=0,before=0,first_nonzero=std::string::npos;std::string normalized;
    const auto digit=[&](char16_t c){if(c!='0'&&first_nonzero==std::string::npos)first_nonzero=digits;++digits;normalized+=static_cast<char>(c);};
    while(position<input.size()){
        const auto c=input[position];if(c>='0'&&c<='9'){digit(c);++before;++position;}else if(c==','&&digits){++position;}else break;
    }
    if(position<input.size()&&input[position]=='.'){normalized+='.';++position;while(position<input.size()&&input[position]>='0'&&input[position]<='9')digit(input[position++]);}
    if(!digits)return false;int exponent=0;
    if(position<input.size()&&(input[position]=='e'||input[position]=='E')){
        normalized+='e';++position;bool minus=false;if(position<input.size()&&(input[position]=='+'||input[position]=='-')){minus=input[position]=='-';normalized+=static_cast<char>(input[position++]);}
        const auto start=position;while(position<input.size()&&input[position]>='0'&&input[position]<='9'){exponent=std::min(100000,exponent*10+(input[position]-'0'));normalized+=static_cast<char>(input[position++]);}
        if(start==position)return false;if(minus)exponent=-exponent;
    }
    while(position<input.size()&&white(input[position]))++position;
    while(position<input.size()&&input[position]==0)++position;
    if(position!=input.size())return false;
    const auto parsed=std::from_chars(normalized.data(),normalized.data()+normalized.size(),value,std::chars_format::general);
    if(parsed.ec==std::errc::result_out_of_range){const auto power=exponent+static_cast<std::int64_t>(before)-static_cast<std::int64_t>(first_nonzero==std::string::npos?digits:first_nonzero)-1;value=power>=0?std::numeric_limits<double>::infinity():0.;}
    else if(parsed.ec!=std::errc{}||parsed.ptr!=normalized.data()+normalized.size())return false;
    if(negative)value=-value;return true;
}
std::vector<std::uint8_t> binary(std::string_view encoded){
    std::string text;for(char c:encoded)if(c!=' '&&c!='\t'&&c!='\r'&&c!='\n')text+=c;
    if(text.size()%4)throw std::invalid_argument("Invalid OSD binary encoding");
    const auto decode=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
    std::vector<std::uint8_t> result;
    for(std::size_t i=0;i<text.size();i+=4){int a=decode(text[i]),b=decode(text[i+1]),c=text[i+2]=='='?0:decode(text[i+2]),d=text[i+3]=='='?0:decode(text[i+3]);
        if(a<0||b<0||c<0||d<0||(text[i+2]=='='&&text[i+3]!='=')||((text[i+2]=='='||text[i+3]=='=')&&i+4!=text.size()))throw std::invalid_argument("Invalid OSD binary encoding");
        const auto bits=static_cast<std::uint32_t>((a<<18)|(b<<12)|(c<<6)|d);result.push_back(static_cast<std::uint8_t>(bits>>16));if(text[i+2]!='=')result.push_back(static_cast<std::uint8_t>(bits>>8));if(text[i+3]!='=')result.push_back(static_cast<std::uint8_t>(bits));
    }return result;
}
std::int64_t date_ticks(std::string_view text){
    const auto read=[&](std::size_t at,std::size_t count){int value=0;if(at+count>text.size())throw std::invalid_argument("Invalid OSD date");for(std::size_t i=at;i<at+count;++i){if(text[i]<'0'||text[i]>'9')throw std::invalid_argument("Invalid OSD date");value=value*10+text[i]-'0';}return value;};
    if(text.size()<19||text[4]!='-'||text[7]!='-'||(text[10]!='T'&&text[10]!='t')||text[13]!=':'||text[16]!=':')throw std::invalid_argument("Invalid OSD date");
    using namespace std::chrono;const auto year_value=read(0,4);const year_month_day calendar{year{year_value},month{static_cast<unsigned>(read(5,2))},day{static_cast<unsigned>(read(8,2))}};
    const int h=read(11,2),m=read(14,2),s=read(17,2);if(year_value<1||!calendar.ok()||h>23||m>59||s>59)throw std::invalid_argument("Invalid OSD date");
    std::size_t at=19;std::int64_t fraction=0;int fractional_digits=0;
    if(at<text.size()&&text[at]=='.'){++at;while(at<text.size()&&text[at]>='0'&&text[at]<='9'){if(fractional_digits>=7)throw std::invalid_argument("OSD date exceeds tick precision");fraction=fraction*10+text[at++]-'0';++fractional_digits;}if(!fractional_digits)throw std::invalid_argument("Invalid OSD date");}
    while(fractional_digits++<7)fraction*=10;
    int offset=0;if(at<text.size()&&(text[at]=='Z'||text[at]=='z'))++at;
    else if(at<text.size()&&(text[at]=='+'||text[at]=='-')){const bool minus=text[at]=='-';if(at+6!=text.size()||text[at+3]!=':')throw std::invalid_argument("Invalid OSD date offset");const int oh=read(at+1,2),om=read(at+4,2);if(oh>14||om>59||(oh==14&&om))throw std::invalid_argument("Invalid OSD date offset");offset=(oh*60+om)*(minus?-60:60);at+=6;}
    if(at!=text.size())throw std::invalid_argument("Invalid OSD date");
    const auto seconds=duration_cast<std::chrono::seconds>(sys_days{calendar}.time_since_epoch()).count()+h*3600+m*60+s-offset;
    return seconds*10000000+fraction;
}
std::int32_t date_integer(std::string_view text){
    const auto truncated=static_cast<std::int64_t>(static_cast<double>(date_ticks(text))/10000000.);
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(truncated));
}
std::string base64(const std::vector<std::uint8_t>& bytes){
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";std::string result;
    for(std::size_t i=0;i<bytes.size();i+=3){std::uint32_t bits=static_cast<std::uint32_t>(bytes[i])<<16;if(i+1<bytes.size())bits|=static_cast<std::uint32_t>(bytes[i+1])<<8;if(i+2<bytes.size())bits|=bytes[i+2];result+=alphabet[(bits>>18)&63];result+=alphabet[(bits>>12)&63];result+=i+1<bytes.size()?alphabet[(bits>>6)&63]:'=';result+=i+2<bytes.size()?alphabet[bits&63]:'=';}return result;
}
std::string real_text(double value){
    if(std::isnan(value))return "NaN";if(std::isinf(value))return std::signbit(value)?"-\xe2\x88\x9e":"\xe2\x88\x9e";
    char buffer[128];const auto converted=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::scientific);if(converted.ec!=std::errc{})throw std::runtime_error("OSD real formatting failed");std::string scientific(buffer,converted.ptr);const auto at=scientific.find('e');const int exponent=std::stoi(scientific.substr(at+1));
    if(exponent>=-4&&exponent<17){std::string digits=scientific.substr(0,at),sign;if(digits.front()=='-'){sign="-";digits.erase(0,1);}std::erase(digits,'.');const int point=exponent+1;if(point<=0)return sign+"0."+std::string(static_cast<std::size_t>(-point),'0')+digits;if(static_cast<std::size_t>(point)>=digits.size())return sign+digits+std::string(static_cast<std::size_t>(point)-digits.size(),'0');return sign+digits.substr(0,point)+"."+digits.substr(point);}
    return scientific;
}
std::string date_text(std::string_view text){
    using namespace std::chrono;const auto ticks=date_ticks(text);auto seconds_value=ticks/10000000,remainder=ticks%10000000;if(remainder<0){--seconds_value;remainder+=10000000;}
    const sys_seconds instant{seconds{seconds_value}};const auto date=floor<days>(instant);const year_month_day calendar{date};const hh_mm_ss time{instant-date};std::ostringstream out;out.imbue(std::locale::classic());out<<std::setfill('0')<<std::setw(4)<<int(calendar.year())<<'-'<<std::setw(2)<<unsigned(calendar.month())<<'-'<<std::setw(2)<<unsigned(calendar.day())<<'T'<<std::setw(2)<<time.hours().count()<<':'<<std::setw(2)<<time.minutes().count()<<':'<<std::setw(2)<<time.seconds().count();if(remainder/10000>0)out<<'.'<<std::setw(2)<<remainder/100000;return out.str()+"Z";
}
}
bool try_parse_invariant_real(std::string_view text,double& value){return number(text,value);}
framework::Uuid osd_uuid(const Value& input){const auto& value=input.view();
    if(value.kind!=Value::Kind::String&&value.kind!=Value::Kind::Uuid)return {};
    framework::Uuid result;framework::try_parse_sdk_uuid(value.text,result);return result;
}
bool osd_boolean(const Value& input){const auto& value=input.view();
    switch(value.kind){
    case Value::Kind::Boolean:return value.boolean;
    case Value::Kind::Integer:return value.integer!=0;
    case Value::Kind::Long:return value.long_value!=0;
    case Value::Kind::Real:return !std::isnan(value.real)&&value.real!=0;
    case Value::Kind::Uuid:return !osd_uuid(value).is_zero();
    case Value::Kind::Array:return !value.array.empty();
    case Value::Kind::Map:return !value.map.empty();
    case Value::Kind::String:{
        if(value.text.empty()||value.text=="0")return false;
        auto text=framework::utf16_replacing(value.text);std::string compared;
        for(std::size_t i=0;i<text.size();++i){
            char32_t point=text[i];
            if(point>=0xd800&&point<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff){point=0x10000+((point-0xd800)<<10)+(text[++i]-0xdc00);}
            auto ignored=std::lower_bound(detail::boolean_ignorables.begin(),detail::boolean_ignorables.end(),point,[](auto range,char32_t n){return range.second<n;});
            if(ignored!=detail::boolean_ignorables.end()&&ignored->first<=point)continue;
            if(point>127)return true;
            auto c=static_cast<char>(point);if(c>='A'&&c<='Z')c+=32;compared+=c;
        }
        return compared!="false";
    }
    default:return false;
    }
}
double osd_real(const Value& input){const auto& value=input.view();
    switch(value.kind){case Value::Kind::Boolean:return value.boolean?1.:0.;case Value::Kind::Integer:return value.integer;case Value::Kind::Long:return static_cast<double>(value.long_value);case Value::Kind::Real:return value.real;case Value::Kind::String:{double result=0;return number(value.text,result)?result:0.;}default:return 0.;}
}
std::int32_t osd_integer(const Value& input){const auto& value=input.view();
    constexpr auto low=std::numeric_limits<std::int32_t>::min(),high=std::numeric_limits<std::int32_t>::max();
    switch(value.kind){
    case Value::Kind::Integer:return value.integer;
    case Value::Kind::Long:return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(value.long_value));
    case Value::Kind::Boolean:return value.boolean?1:0;
    case Value::Kind::Real:{const auto n=value.real;if(std::isnan(n))return 0;if(n>high)return high;if(n<low)return low;const auto floor=std::floor(n),part=n-floor;return static_cast<std::int32_t>(floor+((part>.5||(part==.5&&std::fmod(floor,2.)!=0.))?1.:0.));}
    case Value::Kind::String:{double n=0;if(!number(value.text,n))return 0;n=std::floor(n);if(!std::isfinite(n)||n<low||n>high)return low;return static_cast<std::int32_t>(n);}
    case Value::Kind::Binary:{const auto bytes=osd_binary(value);if(bytes->size()<4)throw std::invalid_argument("Short OSD binary integer is undefined in the source SDK");return std::bit_cast<std::int32_t>((static_cast<std::uint32_t>((*bytes)[0])<<24)|(static_cast<std::uint32_t>((*bytes)[1])<<16)|(static_cast<std::uint32_t>((*bytes)[2])<<8)|(*bytes)[3]);}
    case Value::Kind::Array:{if(value.array.size()<4)return 0;std::uint32_t result=0;for(std::size_t i=0;i<4;++i)result=(result<<8)|static_cast<std::uint8_t>(osd_integer(value.array[i]));return std::bit_cast<std::int32_t>(result);}
    case Value::Kind::Date:return date_integer(value.text);
    default:return 0;
    }
}
std::uint32_t osd_unsigned(const Value& input){const auto& value=input.view();
    switch(value.kind){
    case Value::Kind::Real:{const auto n=value.real;if(std::isnan(n)||n<0)return 0;if(n>4294967295.)return 4294967295u;const auto low=std::floor(n),part=n-low;return static_cast<std::uint32_t>(low+((part>.5||(part==.5&&std::fmod(low,2.)!=0.))?1.:0.));}
    case Value::Kind::String:{double n=0;if(!number(value.text,n))return 0;n=std::floor(n);if(!std::isfinite(n)||n< -9223372036854775808.||n>=9223372036854775808.)return 0;return static_cast<std::uint32_t>(static_cast<std::int64_t>(n));}
    default:return static_cast<std::uint32_t>(osd_integer(value));
    }
}
Value osd_from_binary(BinaryData bytes){static const auto empty=std::make_shared<std::vector<std::uint8_t>>();Value result;result.kind=Value::Kind::Binary;result.binary_data=bytes?std::move(bytes):empty;return result;}
Value osd_from_unsigned(std::uint32_t number){auto bytes=std::make_shared<std::vector<std::uint8_t>>(4);for(int i=0;i<4;++i)(*bytes)[i]=static_cast<std::uint8_t>(number>>(24-i*8));return osd_from_binary(std::move(bytes));}
BinaryData osd_binary(const Value& input){const auto& value=input.view();
    if(value.kind==Value::Kind::Binary){auto existing=value.binary_data.load();if(existing)return existing;auto decoded=std::make_shared<std::vector<std::uint8_t>>(binary(value.text));if(value.binary_data.initialize(existing,decoded))return decoded;return existing;}
    if(value.kind==Value::Kind::Boolean){static auto yes=std::make_shared<std::vector<std::uint8_t>>(1,49),no=std::make_shared<std::vector<std::uint8_t>>(1,48);return value.boolean?yes:no;}
    auto result=std::make_shared<std::vector<std::uint8_t>>();
    const auto append=[&](std::uint64_t bits,int count,bool little=false){for(int i=0;i<count;++i)result->push_back(static_cast<std::uint8_t>(bits>>((little?i:count-1-i)*8)));};
    switch(value.kind){
    case Value::Kind::Integer:append(static_cast<std::uint32_t>(value.integer),4);break;
    case Value::Kind::Long:append(static_cast<std::uint64_t>(value.long_value),8);break;
    case Value::Kind::Real:append(std::bit_cast<std::uint64_t>(value.real),8);break;
    case Value::Kind::Date:append(std::bit_cast<std::uint64_t>(static_cast<double>(date_ticks(value.text))/10000000.),8,true);break;
    case Value::Kind::String:result->assign(value.text.begin(),value.text.end());break;
    case Value::Kind::Uri:{const auto text=osd_string(value);result->assign(text.begin(),text.end());break;}
    case Value::Kind::Uuid:{const auto id=osd_uuid(value);result->assign(id.bytes().begin(),id.bytes().end());break;}
    case Value::Kind::Array:for(const auto& item:value.array)result->push_back(static_cast<std::uint8_t>(osd_integer(item)));break;
    case Value::Kind::Map:throw std::invalid_argument("SDK map-to-binary cast is invalid");
    default:break;
    }return result;
}
std::string binary_base64(const Value& input){const auto& value=input.view();const auto data=value.binary_data.load();return data?base64(*data):value.text;}
std::string osd_to_string(const Value& input){const auto& value=input.view();if(value.kind==Value::Kind::Map||value.kind==Value::Kind::Array)return osd_json(value,true);if(value.kind==Value::Kind::Undefined)return "undef";if(value.kind==Value::Kind::Binary){auto bytes=osd_binary(value);std::string out;const char* h="0123456789ABCDEF";for(std::size_t i=0;i<bytes->size();++i){if(i)out+=i%16?' ':'\n';auto c=(*bytes)[i];out+=h[c>>4];out+=h[c&15];}return out;}return osd_string(value);}
std::string osd_string(const Value& input){const auto& value=input.view();
    switch(value.kind){
    case Value::Kind::Boolean:return value.boolean?"1":"0";
    case Value::Kind::Integer:return std::to_string(value.integer);
    case Value::Kind::Long:return std::to_string(value.long_value);
    case Value::Kind::Real:return real_text(value.real);
    case Value::Kind::String:return value.text;
    case Value::Kind::Binary:return base64(*osd_binary(value));
    case Value::Kind::Uuid:return osd_uuid(value).str();
    case Value::Kind::Date:return date_text(value.text);
    case Value::Kind::Uri:{framework::HttpUri uri(value.text);return uri.valid()?uri.uri:value.text;}
    default:return "";
    }
}
}
