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
#include "opensim/framework/uuid.hpp"
#include "opensim/framework/identity.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::framework {
Uuid parse_sdk_uuid(std::string_view text){while(text.starts_with(' '))text.remove_prefix(1);if(text.size()<32)throw std::invalid_argument("Invalid SDK UUID");return Uuid(text.substr(0,text[8]=='-'?36:32));}
bool try_parse_sdk_uuid(std::string_view text,Uuid& result){result={};while(text.starts_with(' '))text.remove_prefix(1);while(text.ends_with(' '))text.remove_suffix(1);if(text.size()!=32&&text.size()!=36)return false;try{result=Uuid(text);return true;}catch(const std::invalid_argument&){return false;}}
Uuid::Uuid(std::string_view text) {
    const auto normalized=normalize_uuid(text);
    std::size_t position=0;
    auto nibble=[](char c){return static_cast<unsigned>(c>='a'?c-'a'+10:c-'0');};
    for(std::size_t i=0;i<normalized.size();) {
        if(normalized[i]=='-'){++i;continue;}
        bytes_[position++]=static_cast<std::uint8_t>((nibble(normalized[i])<<4)|nibble(normalized[i+1]));i+=2;
    }
}
Uuid Uuid::from_bytes(std::span<const std::uint8_t> bytes) {
    if(bytes.size()<16)throw std::out_of_range("UUID requires sixteen bytes");
    Uuid result;std::copy_n(bytes.begin(),16,result.bytes_.begin());return result;
}
Uuid Uuid::random(){return Uuid(random_uuid());}
std::string Uuid::str() const {
    constexpr char hex[]="0123456789abcdef";
    std::string result;result.reserve(36);
    for(std::size_t i=0;i<bytes_.size();++i){if(i==4||i==6||i==8||i==10)result+='-';result+=hex[bytes_[i]>>4];result+=hex[bytes_[i]&15];}
    return result;
}
bool Uuid::is_zero()const noexcept{return std::all_of(bytes_.begin(),bytes_.end(),[](auto value){return value==0;});}
std::size_t UuidHash::operator()(const Uuid& id)const noexcept{
    std::size_t hash=static_cast<std::size_t>(2166136261u);
    for(auto byte:id.bytes())hash=(hash^byte)*16777619u;
    return hash;
}
}
