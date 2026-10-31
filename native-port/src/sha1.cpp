// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/sha1.hpp"
#include <algorithm>
#include <bit>
namespace opensim::framework {
std::array<uint8_t,20> sha1(std::span<const uint8_t> input){
 std::array<uint32_t,5> state{0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u,0xc3d2e1f0u};
 auto compress=[&](const uint8_t* block){std::array<uint32_t,80> words{};for(size_t i=0;i<16;++i)for(size_t j=0;j<4;++j)words[i]=(words[i]<<8)|block[4*i+j];for(size_t i=16;i<80;++i)words[i]=std::rotl(words[i-3]^words[i-8]^words[i-14]^words[i-16],1);auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4];for(size_t i=0;i<80;++i){uint32_t f,k;if(i<20){f=(b&c)|(~b&d);k=0x5a827999u;}else if(i<40){f=b^c^d;k=0x6ed9eba1u;}else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcu;}else{f=b^c^d;k=0xca62c1d6u;}auto next=std::rotl(a,5)+f+e+k+words[i];e=d;d=c;c=std::rotl(b,30);b=a;a=next;}state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;};
 size_t offset=0;while(input.size()-offset>=64){compress(input.data()+offset);offset+=64;}std::array<uint8_t,128> tail{};auto remain=input.size()-offset;if(remain)std::copy_n(input.data()+offset,remain,tail.data());tail[remain]=0x80;auto length=remain<56?size_t(64):size_t(128);uint64_t bits=static_cast<uint64_t>(input.size())*8;for(size_t i=0;i<8;++i){tail[length-1-i]=static_cast<uint8_t>(bits);bits>>=8;}compress(tail.data());if(length==128)compress(tail.data()+64);std::array<uint8_t,20> result{};for(size_t i=0;i<5;++i)for(size_t j=0;j<4;++j)result[4*i+j]=static_cast<uint8_t>(state[i]>>(24-8*j));return result;
}
std::string sha1_hex(std::span<const uint8_t> input){constexpr char digits[]="0123456789abcdef";auto bytes=sha1(input);std::string out;out.reserve(40);for(auto b:bytes){out+=digits[b>>4];out+=digits[b&15];}return out;}
}
