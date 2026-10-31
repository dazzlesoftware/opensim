// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/text.hpp"
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace opensim::network {
namespace {
std::string quote(std::string_view text){
    std::string result="\"";constexpr char digits[]="0123456789ABCDEF";
    for(char16_t c:framework::utf16_replacing(text)){switch(c){case u'"':result+="\\\"";break;case u'\\':result+="\\\\";break;case u'\n':result+="\\n";break;case u'\r':result+="\\r";break;case u'\t':result+="\\t";break;case u'\b':result+="\\b";break;case u'\f':result+="\\f";break;default:if(c>=32&&c<=126)result+=static_cast<char>(c);else{result+="\\u";for(int shift=12;shift>=0;shift-=4)result+=digits[(c>>shift)&15];}}}return result+'"';
}
bool omitted(const Value& input){const auto& v=input.view();using K=Value::Kind;switch(v.kind){case K::Undefined:return true;case K::Boolean:return !v.boolean;case K::Integer:return v.integer==0;case K::Long:return v.long_value==0;case K::Real:return v.real==0;case K::String:case K::Uri:return osd_string(v).empty();case K::Uuid:return osd_uuid(v).is_zero();case K::Binary:case K::Array:return false;case K::Map:return v.map.empty();default:return false;}}
std::string serialize(const Value& input,bool preserve,std::size_t depth){
    const auto& v=input.view();
    if(depth>512)throw std::invalid_argument("OSD JSON nesting exceeds native limit");using K=Value::Kind;
    switch(v.kind){case K::Undefined:return "null";case K::Boolean:return v.boolean?"true":"false";case K::Integer:return std::to_string(v.integer);case K::Long:return std::to_string(v.long_value);
    case K::Real:{auto s=std::isinf(v.real)?(v.real<0?std::string("-Infinity"):std::string("Infinity")):osd_string(v);for(auto& c:s)if(c=='e')c='E';if(s.find_first_of(".E")==std::string::npos)s+=".0";return s;}
    case K::String:case K::Uuid:case K::Date:case K::Uri:return quote(osd_string(v));
    case K::Binary:{auto bytes=osd_binary(v);std::string s="[";for(auto b:*bytes){if(s.size()>1)s+=',';s+=std::to_string(b);}return s+']';}
    case K::Array:{std::string s="[";for(const auto& child:v.array){if(s.size()>1)s+=',';s+=serialize(child,preserve,depth+1);}return s+']';}
    case K::Map:{std::string s="{";for(const auto& key:v.ordered_keys()){const auto& child=v.get(key);if(!preserve&&omitted(child))continue;if(s.size()>1)s+=',';s+=quote(key)+':'+serialize(child,preserve,depth+1);}return s+'}';}
    }return "null";
}
class Parser {
    std::u16string text_;std::size_t pos_=0;
    [[noreturn]] void bad()const{throw std::invalid_argument("Invalid OSD JSON");}
    static int hex(char16_t c){return c>=u'0'&&c<=u'9'?c-u'0':c>=u'A'&&c<=u'F'?c-u'A'+10:c>=u'a'&&c<=u'f'?c-u'a'+10:-1;}
    void space(){while(pos_<text_.size()){auto c=text_[pos_];if(c==u' '||c==u'\t'||c==u'\r'||c==u'\n'){++pos_;continue;}if(c==u'/'&&pos_+1<text_.size()){if(text_[pos_+1]==u'/'){pos_+=2;while(pos_<text_.size()&&text_[pos_]!=u'\n')++pos_;continue;}if(text_[pos_+1]==u'*'){pos_+=2;auto end=text_.find(u"*/",pos_);if(end==std::u16string::npos){pos_=text_.size();return;}pos_=end+2;continue;}}break;}}
    bool consume(char16_t c){space();if(pos_<text_.size()&&text_[pos_]==c){++pos_;return true;}return false;}
    std::string string(){space();if(pos_==text_.size()||(text_[pos_]!=u'"'&&text_[pos_]!=u'\''))bad();const auto delimiter=text_[pos_++];std::u16string result;while(pos_<text_.size()){auto c=text_[pos_++];if(c==delimiter)return framework::utf8(result);if(c==u'\\'){if(pos_==text_.size())bad();c=text_[pos_++];switch(c){case u'"':case u'\'':case u'/':case u'\\':break;case u'b':c=u'\b';break;case u'f':c=u'\f';break;case u'n':c=u'\n';break;case u'r':c=u'\r';break;case u't':c=u'\t';break;case u'u':{if(pos_+4>text_.size())bad();c=0;for(int i=0;i<4;++i){int n=hex(text_[pos_++]);if(n<0)bad();c=static_cast<char16_t>((c<<4)|n);}break;}default:bad();}}result+=c;}bad();}
    Value value(std::size_t depth){
        if(depth>512)bad();space();if(pos_==text_.size())bad();const auto c=text_[pos_];
        if(c==u'{' ){++pos_;Value map=Value::object({});if(consume(u'}'))return map;while(true){auto key=string();if(!consume(u':'))bad();map.set(std::move(key),value(depth+1));if(consume(u'}'))return map;if(!consume(u','))bad();}}
        if(c==u'['){++pos_;Value array;array.kind=Value::Kind::Array;if(consume(u']'))return array;while(true){array.array.push_back(value(depth+1));if(consume(u']'))return array;if(!consume(u','))bad();}}
        if(c==u'"'||c==u'\''){auto s=string();return s.empty()?Value{}:Value(std::move(s));}
        for(auto literal:{std::u16string_view(u"true"),std::u16string_view(u"false"),std::u16string_view(u"null")})if(std::u16string_view(text_).substr(pos_).starts_with(literal)){pos_+=literal.size();return literal==u"null"?Value{}:Value(literal==u"true");}
        const auto start=pos_;if(c==u'-')++pos_;const auto digits=pos_;while(pos_<text_.size()&&text_[pos_]>=u'0'&&text_[pos_]<=u'9')++pos_;if(pos_==digits||(pos_>digits+1&&text_[digits]==u'0'))bad();bool real=false;
        if(pos_<text_.size()&&text_[pos_]==u'.'){real=true;++pos_;const auto first=pos_;while(pos_<text_.size()&&text_[pos_]>=u'0'&&text_[pos_]<=u'9')++pos_;if(first==pos_)bad();}
        if(pos_<text_.size()&&(text_[pos_]==u'e'||text_[pos_]==u'E')){real=true;++pos_;if(pos_<text_.size()&&(text_[pos_]==u'+'||text_[pos_]==u'-'))++pos_;const auto first=pos_;while(pos_<text_.size()&&text_[pos_]>=u'0'&&text_[pos_]<=u'9')++pos_;if(first==pos_)bad();}
        auto numeric=framework::utf8(std::u16string_view(text_).substr(start,pos_-start));if(!real){std::int64_t n=0;auto r=std::from_chars(numeric.data(),numeric.data()+numeric.size(),n);if(r.ec==std::errc{})return n>=INT32_MIN&&n<=INT32_MAX?Value(static_cast<std::int32_t>(n)):Value::long_integer(n);std::uint64_t u=0;if(std::from_chars(numeric.data(),numeric.data()+numeric.size(),u).ec==std::errc{})bad();return Value(std::int32_t{0});}
        Value result;result.kind=Value::Kind::Real;double number=0;auto r=std::from_chars(numeric.data(),numeric.data()+numeric.size(),number,std::chars_format::general);if(r.ec==std::errc::result_out_of_range){Value s(numeric);number=osd_real(s);}else if(r.ec!=std::errc{})bad();result.real=number;return result;
    }
public:
    explicit Parser(std::string_view text):text_(framework::utf16_replacing(text)){}
    Value parse(){space();if(pos_==text_.size())return {};if(text_[pos_]!=u'{'&&text_[pos_]!=u'['){auto c=text_[pos_];if((c>=u'0'&&c<=u'9')||c==u'-'){const auto start=pos_;value(0);if(pos_==text_.size()){auto token=std::u16string_view(text_).substr(start);if(token==u"0"||token==u"-0")bad();return {};}}bad();}auto result=value(0);space();if(pos_<text_.size()){auto c=text_[pos_];if(c!=u'{'&&c!=u'}'&&c!=u'['&&c!=u']'&&c!=u':'&&c!=u',')value(0);}return result;}
};
}
std::string osd_json(const Value& value,bool preserve_defaults){return serialize(value,preserve_defaults,0);}
Value parse_osd_json(std::string_view text){return Parser(text).parse();}
}
