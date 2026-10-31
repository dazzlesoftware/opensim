// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/asset_text.hpp"
#include <iostream>
using namespace opensim;
namespace {
std::string hex(std::string_view s){std::string out;for(unsigned char b:s){out+="0123456789abcdef"[b>>4];out+="0123456789abcdef"[b&15];}return out;}
std::vector<std::uint8_t> unhex(std::string_view s){if(s.size()%2)throw std::invalid_argument("hex");std::vector<std::uint8_t> bytes;auto digit=[](char c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;throw std::invalid_argument("hex");};for(std::size_t i=0;i<s.size();i+=2)bytes.push_back(static_cast<std::uint8_t>((digit(s[i])<<4)|digit(s[i+1])));return bytes;}
std::string probe(std::string_view s){try{framework::AssetBytes bytes;if(s!="null")bytes=std::make_shared<std::vector<std::uint8_t>>(unhex(s));return hex(script::asset_text(bytes));}catch(...){return "THROW";}}
}
int main(int argc,char**argv){try{if(argc>1&&std::string_view(argv[1])=="--probe"){std::string s;while(std::getline(std::cin,s))std::cout<<probe(s)<<'\n';return 0;}int checks=0;auto check=[&](bool v,const char*s){++checks;if(!v)throw std::runtime_error(s);};check(probe("null")=="THROW","null throws");check(probe("")=="","empty");check(probe("00")=="","oneNUL");check(probe("0000")=="00","onlyone trailingzero removed");check(probe("61006200")=="610062","embeddedNUL preserved");check(probe("efbbbf61")=="efbbbf61","BOM preserved");check(probe("e282")=="efbfbd","truncated prefix one fallback");check(probe("eda080")=="efbfbdefbfbdefbfbd","surrogate sequence perbyte fallback");check(probe("c080")=="efbfbdefbfbd","overlong");check(probe("f4908080")=="efbfbdefbfbdefbfbdefbfbd","out of range");std::cout<<"PASS: "<<checks<<" script asset text checks\n";return 0;}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
