#include "opensim/network/osd_json.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim::network;
std::string hex(std::string_view s){constexpr char d[]="0123456789abcdef";std::string r;for(unsigned char c:s){r+=d[c>>4];r+=d[c&15];}return r;}
std::string types(const Value& v){static const char* names[]={"u","s","i","b","r","id","d","uri","bin","a","m","l"};std::string s=names[static_cast<int>(v.kind)];if(v.kind==Value::Kind::Array)for(const auto& x:v.array)s+=":"+types(x);if(v.kind==Value::Kind::Map)for(const auto& key:v.ordered_keys())s+=":"+hex(key)+"="+types(v.get(key));return s;}
int main(){std::string line;while(std::getline(std::cin,line)){try{Value v;if(line.starts_with("p "))v=parse_osd_json(fixture::unhex(line.substr(2)));else {auto scalar=fixture::build(line.substr(2));v=scalar;if(line.starts_with("m ")){v=Value::object({});v.set("field",scalar);Value nested=Value::object({});nested.set("field",scalar);v.set("nested",nested);Value a;a.kind=Value::Kind::Array;a.array={scalar,nested};v.set("array",a);}}std::cout<<hex(osd_json(v,false))<<'|'<<hex(osd_json(v,true))<<'|'<<types(v)<<'\n';}catch(...){std::cout<<"EX\n";}}}
