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
#include "opensim/network/rpc.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/identity.hpp"
#include <pugixml.hpp>
#include <charconv>
#include <algorithm>
#include <unordered_set>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace opensim::network {
const Value& Value::view()const{const Value* value=this;for(unsigned depth=0;value->reference;++depth){if(depth==128)throw std::invalid_argument("Cyclic value reference");value=value->reference.get();}return *value;}
Value Value::alias(const std::shared_ptr<Value>& value){Value result;if(value){result.kind=value->kind;result.reference=value;}return result;}
Value Value::object(std::map<std::string,Value> fields) { Value result; result.kind=Kind::Map; result.map=std::move(fields); return result; }
void Value::set(std::string key,Value value){if(reference){const_cast<Value&>(view()).set(std::move(key),std::move(value));return;}if(!map.contains(key))map_order.push_back(key);map[std::move(key)]=std::move(value);}
std::vector<std::string> Value::ordered_keys()const{
    if(reference)return view().ordered_keys();
    std::vector<std::string> keys;keys.reserve(map.size());
    std::unordered_set<std::string_view> seen;seen.reserve(map.size());
    for(auto& key:map_order)if(map.contains(key)&&seen.insert(key).second)keys.push_back(key);
    for(auto& [key,value]:map)if(seen.insert(key).second)keys.push_back(key);
    return keys;
}
std::string Value::as_string() const {
    if(reference)return view().as_string();
    if(kind==Kind::Binary)return binary_base64(*this);
    if(kind==Kind::Long)return std::to_string(long_value);
    if (kind==Kind::Integer) return std::to_string(integer);
    if (kind==Kind::Boolean) return boolean ? "True" : "False";
    if (kind==Kind::Real) { std::ostringstream out; out.imbue(std::locale::classic()); out<<std::setprecision(17)<<real; return out.str(); }
    return text;
}
bool Value::contains(std::string_view name) const { if(reference)return view().contains(name);return map.find(std::string(name))!=map.end(); }
const Value& Value::get(std::string_view name) const {
    if(reference)return view().get(name);
    static const Value empty;
    auto found=map.find(std::string(name)); return found==map.end() ? empty : found->second;
}
namespace {
using Kind=Value::Kind;
pugi::xml_document document(std::string_view bytes) {
    if (bytes.find('\0')!=std::string_view::npos) throw std::invalid_argument("NUL in XML input");
    pugi::xml_document doc;
    auto parsed=doc.load_buffer(bytes.data(),bytes.size(),pugi::parse_default|pugi::parse_doctype,pugi::encoding_utf8);
    if (!parsed) throw std::invalid_argument("Malformed XML");
    int roots=0;
    for (auto child:doc.children()) {
        if (child.type()==pugi::node_doctype) throw std::invalid_argument("DTDs are not accepted");
        if (child.type()==pugi::node_element) ++roots;
    }
    if (roots!=1) throw std::invalid_argument("Expected a single XML document element");
    return doc;
}
std::int32_t integer(std::string_view value) {
    std::int32_t number=0;
    const auto parsed=std::from_chars(value.data(),value.data()+value.size(),number);
    if (parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()) throw std::invalid_argument("Invalid RPC integer");
    return number;
}
pugi::xml_node element(pugi::xml_node parent) {
    for (auto child:parent.children()) if (child.type()==pugi::node_element) return child;
    return {};
}
void depth_check(unsigned depth) { if (depth>64) throw std::invalid_argument("RPC nesting limit exceeded"); }
Value decode(pugi::xml_node node,bool llsd,unsigned depth) {
    depth_check(depth);
    if (!node) throw std::invalid_argument("Missing RPC value");
    if (!llsd) {
        if (std::string_view(node.name())!="value") throw std::invalid_argument("Expected XML-RPC value");
        auto typed=element(node);
        if (!typed) return Value(node.child_value());
        if (typed.next_sibling()) {
            for (auto sibling=typed.next_sibling();sibling;sibling=sibling.next_sibling())
                if (sibling.type()==pugi::node_element) throw std::invalid_argument("Multiple XML-RPC value types");
        }
        node=typed;
    }
    const std::string_view tag=node.name();
    Value result;
    if (tag=="struct"||tag=="map") {
        result.kind=Kind::Map;
        if (llsd) {
            for (auto key=element(node);key;) {
                if (std::string_view(key.name())!="key") throw std::invalid_argument("Expected LLSD key");
                auto value=key.next_sibling();
                while (value&&value.type()!=pugi::node_element) value=value.next_sibling();
                if (!value) throw std::invalid_argument("Missing LLSD map value");
                result.set(key.child_value(),decode(value,true,depth+1));
                key=value.next_sibling();
                while (key&&key.type()!=pugi::node_element) key=key.next_sibling();
            }
        } else for (auto member:node.children()) {
            if (member.type()!=pugi::node_element) continue;
            if (std::string_view(member.name())!="member"||!member.child("name")) throw std::invalid_argument("Invalid XML-RPC struct");
            result.set(member.child_value("name"),decode(member.child("value"),false,depth+1));
        }
    } else if (tag=="array") {
        result.kind=Kind::Array;
        auto values=llsd?node:node.child("data");
        if (!values) throw std::invalid_argument("Missing XML-RPC array data");
        for (auto child:values.children()) if(child.type()==pugi::node_element) result.array.push_back(decode(child,llsd,depth+1));
    } else if (tag=="string") {
        // The repository's Nwc.XmlRpc reader represents an empty typed string
        // as null. LLSD retains it as an empty string.
        if(llsd||node.child_value()[0]!='\0')result=Value(node.child_value());
    }
    else if (tag=="int"||tag=="i4"||tag=="integer") { result=Value(integer(node.child_value())); }
    else if (tag=="boolean") {
        const std::string_view text=node.child_value();
        if (text=="1"||text=="true") result=Value(true);
        else if(text=="0"||text=="false"||text.empty()) result=Value(false);
        else throw std::invalid_argument("Invalid RPC boolean");
    } else if(tag=="double"||tag=="real") {
        result.kind=Kind::Real;
        std::istringstream input(node.child_value()); input.imbue(std::locale::classic());
        input>>result.real;
        if (!input||!input.eof()) throw std::invalid_argument("Invalid RPC real");
    } else if(tag=="uuid") { result.kind=Kind::Uuid; result.text=node.child_value(); if(result.text.empty())result.text=framework::ZeroUuid; result.text=framework::normalize_uuid(result.text); }
    else if(tag=="base64"||tag=="binary") { result.kind=Kind::Binary; result.text=node.child_value(); }
    else if(tag=="dateTime.iso8601"||tag=="date") { result.kind=Kind::Date; result.text=node.child_value(); }
    else if(tag=="uri") { result.kind=Kind::Uri; result.text=node.child_value(); }
    else if(tag!="nil"&&tag!="undef") throw std::invalid_argument("Unknown RPC value type");
    return result;
}
void encode(pugi::xml_node parent,const Value& input,bool llsd,unsigned depth=0) {
    const auto& value=input.view();
    depth_check(depth);
    if(!llsd)parent=parent.append_child("value");
    if(value.kind==Kind::Map) {
        auto map=parent.append_child(llsd?"map":"struct");
        for(const auto& key:value.ordered_keys()) {
            const auto& child=value.map.at(key);
            auto container=map;
            if(llsd) map.append_child("key").text().set(key.c_str());
            else { container=map.append_child("member"); container.append_child("name").text().set(key.c_str()); }
            encode(container,child,llsd,depth+1);
        }
    } else if(value.kind==Kind::Array) {
        auto array=parent.append_child("array");
        if(!llsd)array=array.append_child("data");
        for(const auto& item:value.array)encode(array,item,llsd,depth+1);
    } else {
        const char* name="string";
        switch(value.kind) {
        case Kind::Undefined: name=llsd?"undef":"nil"; break;
        case Kind::Integer: name=llsd?"integer":"int"; break;
        case Kind::Boolean: name="boolean"; break;
        case Kind::Real: name=llsd?"real":"double"; break;
        case Kind::Uuid: name=llsd?"uuid":"string"; break;
        case Kind::Date: name=llsd?"date":"dateTime.iso8601"; break;
        case Kind::Uri: name=llsd?"uri":"string"; break;
        case Kind::Binary: name=llsd?"binary":"base64"; break;
        case Kind::Long: name=llsd?"binary":"string"; break;
        default: break;
        }
        auto node=parent.append_child(name);
        if(value.kind==Kind::Long&&llsd){
            constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            const auto bits=static_cast<std::uint64_t>(value.long_value);std::string encoded;
            for(int at=0;at<8;at+=3){std::uint32_t group=0;for(int j=0;j<3;++j)group=(group<<8)|(at+j<8?static_cast<std::uint32_t>((bits>>(56-(at+j)*8))&255):0u);
                encoded+=alphabet[(group>>18)&63];encoded+=alphabet[(group>>12)&63];encoded+=at+1<8?alphabet[(group>>6)&63]:'=';encoded+=at+2<8?alphabet[group&63]:'=';}
            node.text().set(encoded.c_str());
        }
        else if(value.kind==Kind::Boolean) node.text().set(value.boolean?"1":"0");
        else if(value.kind!=Kind::Undefined) node.text().set(value.as_string().c_str());
    }
}
std::string save(const pugi::xml_document& doc) {
    std::ostringstream out; doc.save(out,"",pugi::format_raw|pugi::format_no_declaration,pugi::encoding_utf8); return out.str();
}
std::string json_escape(std::string_view input) {
    std::string result="\""; const char hex[]="0123456789abcdef";
    for(unsigned char byte:input) {
        if(byte=='"'||byte=='\\') {result+='\\';result+=static_cast<char>(byte);}
        else if(byte<32) {result+="\\u00";result+=hex[byte>>4];result+=hex[byte&15];}
        else result+=static_cast<char>(byte);
    }
    return result+'"';
}
}
RpcCall parse_xml_rpc(std::string_view bytes) {
    auto doc=document(bytes); auto root=doc.document_element();
    if(std::string_view(root.name())!="methodCall"||!root.child("methodName")) throw std::invalid_argument("Expected XML-RPC methodCall");
    RpcCall call; call.method=root.child_value("methodName");
    for(auto param:root.child("params").children("param")) call.parameters.push_back(decode(param.child("value"),false,0));
    return call;
}
Value parse_llsd_xml(std::string_view bytes) {
    auto doc=document(bytes); auto root=doc.document_element();
    if(std::string_view(root.name())!="llsd")throw std::invalid_argument("Expected LLSD document");
    auto value=element(root);
    for(auto sibling=value.next_sibling();sibling;sibling=sibling.next_sibling())
        if(sibling.type()==pugi::node_element)throw std::invalid_argument("Multiple LLSD roots");
    return decode(value,true,0);
}
std::string xml_rpc_request(const RpcCall& call) {
    pugi::xml_document doc;auto root=doc.append_child("methodCall");root.append_child("methodName").text().set(call.method.c_str());auto parameters=root.append_child("params");
    for(const auto& value:call.parameters)encode(parameters.append_child("param"),value,false);
    return save(doc);
}
Value parse_xml_rpc_response(std::string_view bytes) {
    auto doc=document(bytes);const auto root=doc.document_element();
    if(std::string_view(root.name())!="methodResponse"||root.child("fault"))throw std::invalid_argument("Invalid or fault XML-RPC response");
    const auto value=root.child("params").child("param").child("value");if(!value)throw std::invalid_argument("Missing XML-RPC response value");
    return decode(value,false,0);
}
std::string xml_rpc_response(const Value& value) {
    pugi::xml_document doc;
    encode(doc.append_child("methodResponse").append_child("params").append_child("param"),value,false);
    return save(doc);
}
std::string xml_rpc_fault(std::int32_t code,const std::string& message) {
    pugi::xml_document doc;
    encode(doc.append_child("methodResponse").append_child("fault"),Value::object({{"faultCode",Value(code)},{"faultString",Value(message)}}),false);
    return save(doc);
}
std::string llsd_xml(const Value& value) { pugi::xml_document doc; encode(doc.append_child("llsd"),value,true);return save(doc); }
std::string grid_info_xml(const std::map<std::string,std::string>& info) {
    pugi::xml_document doc; auto root=doc.append_child("gridinfo");
    for(const auto& [key,value]:info) {
        if(key.empty()||key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")!=std::string::npos||
            (key.front()>='0'&&key.front()<='9')) throw std::invalid_argument("Invalid GridInfo XML key");
        root.append_child(key.c_str()).text().set(value.c_str());
    }
    return save(doc);
}
std::string json_strings(const std::map<std::string,std::string>& fields) {
    std::string result="{";
    for(const auto& [key,value]:fields) {if(result.size()>1)result+=',';result+=json_escape(key)+':'+json_escape(value);}
    return result+'}';
}
}
