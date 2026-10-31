// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/viewer_name.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/network/osd_value.hpp"
#include <iomanip>
#include <iostream>
#include <sstream>
using namespace opensim;using network::Value;
std::string hex(std::string_view s){std::string out;const char* h="0123456789abcdef";for(unsigned char c:s){out+=h[c>>4];out+=h[c&15];}return out;}
std::string decode(std::string_view s){std::string out;for(std::size_t i=0;i<s.size();i+=2)out+=static_cast<char>(std::stoi(std::string(s.substr(i,2)),nullptr,16));return out;}
std::string units(std::u16string_view s){std::ostringstream out;out<<std::hex<<std::setfill('0');for(char16_t c:s)out<<std::setw(4)<<static_cast<unsigned int>(c);return out.str();}
std::u16string scalar(std::uint32_t c){if(c<65536)return {static_cast<char16_t>(c)};c-=65536;return {static_cast<char16_t>(0xd800+(c>>10)),static_cast<char16_t>(0xdc00+(c&1023))};}
int main(int argc,char** argv){if(argc>1&&std::string_view(argv[1])=="profile"){std::cout<<"runtime:native\n";std::string bits(8192,'\0');for(unsigned int i=0;i<65536;++i)if(framework::viewer_char_is_letter(static_cast<char16_t>(i)))bits[i/8]=static_cast<char>(static_cast<unsigned char>(bits[i/8])|(1<<(i%8)));std::cout<<"letters:"<<hex(bits)<<"\n";for(std::uint32_t i=0;i<=0x10ffff;++i){auto value=scalar(i),lower=framework::lower_invariant_culture(std::u16string_view(value));if(value!=lower)std::cout<<"case:"<<std::hex<<i<<":"<<units(lower)<<"\n";}return 0;}if(argc>1&&std::string_view(argv[1])=="all"){framework::AgentCircuitData agent;agent.channel="Channel";std::string stream;for(std::uint32_t i=0;i<=0x10ffff;++i){agent.viewer(framework::utf8(scalar(i))+" 1");auto text=framework::viewer_name(agent);auto size=static_cast<std::uint32_t>(text.size());for(int shift=0;shift<32;shift+=8)stream+=static_cast<char>((size>>shift)&255);stream+=text;}std::cout<<"all:"<<stream.size()<<":"<<framework::md5(stream)<<"\n";return 0;}std::string line;while(std::getline(std::cin,line)){try{if(line.starts_with("L ")){std::u16string value;for(std::size_t i=2;i<line.size();i+=4)value+=static_cast<char16_t>(std::stoi(line.substr(i,4),nullptr,16));std::cout<<units(framework::lower_invariant_culture(std::u16string_view(value)))<<"\n";continue;}auto args=network::parse_osd_json(decode(line));auto opt=[&](const char* key)->std::optional<std::string>{return args.get(key).kind==Value::Kind::Undefined?std::nullopt:std::optional<std::string>(network::osd_string(args.get(key)));};framework::AgentCircuitData agent;agent.viewer(opt("viewer"));agent.channel=opt("channel");std::cout<<hex(framework::viewer_name(network::osd_boolean(args.get("null_agent"))?nullptr:&agent))<<"\n";}catch(const std::exception&){std::cout<<"EX\n";}}}
