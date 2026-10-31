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
#include "opensim/framework/text.hpp"
#include <stdexcept>
#include <cstdint>
namespace opensim::framework {
std::u16string utf16(std::string_view text){
    std::u16string result;for(std::size_t i=0;i<text.size();){auto first=static_cast<unsigned char>(text[i++]);std::uint32_t value=first;int continuation=0;if(first>=0xc2&&first<=0xdf){value&=31;continuation=1;}else if(first>=0xe0&&first<=0xef){value&=15;continuation=2;}else if(first>=0xf0&&first<=0xf4){value&=7;continuation=3;}else if(first>=128)throw std::invalid_argument("Invalid UTF-8");
        for(int n=0;n<continuation;++n){if(i==text.size()||(static_cast<unsigned char>(text[i])&192)!=128)throw std::invalid_argument("Invalid UTF-8 continuation");value=(value<<6)|(static_cast<unsigned char>(text[i++])&63);}
        if((continuation==1&&value<128)||(continuation==2&&value<2048)||(continuation==3&&value<65536)||value>0x10ffff||(value>=0xd800&&value<=0xdfff))throw std::invalid_argument("Invalid UTF-8 codepoint");
        if(value<65536)result.push_back(static_cast<char16_t>(value));else{value-=65536;result.push_back(static_cast<char16_t>(0xd800+(value>>10)));result.push_back(static_cast<char16_t>(0xdc00+(value&1023)));}
    }return result;
}
std::string utf8(std::u16string_view text){
    std::string result;for(std::size_t i=0;i<text.size();++i){std::uint32_t value=text[i];if(value>=0xd800&&value<=0xdbff){if(i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff)value=0x10000+((value-0xd800)<<10)+(text[++i]-0xdc00);else value=0xfffd;}else if(value>=0xdc00&&value<=0xdfff)value=0xfffd;
        if(value<128)result.push_back(static_cast<char>(value));else if(value<2048){result.push_back(static_cast<char>(0xc0|(value>>6)));result.push_back(static_cast<char>(0x80|(value&63)));}else if(value<65536){result.push_back(static_cast<char>(0xe0|(value>>12)));result.push_back(static_cast<char>(0x80|((value>>6)&63)));result.push_back(static_cast<char>(0x80|(value&63)));}else{result.push_back(static_cast<char>(0xf0|(value>>18)));result.push_back(static_cast<char>(0x80|((value>>12)&63)));result.push_back(static_cast<char>(0x80|((value>>6)&63)));result.push_back(static_cast<char>(0x80|(value&63)));}
    }return result;
}
std::string truncate_utf16(std::string_view text,std::size_t units){auto value=utf16(text);if(value.size()>units)value.resize(units);return utf8(value);}
std::u16string utf16_replacing(std::string_view text){
    std::u16string result;for(std::size_t i=0;i<text.size();){const auto first=static_cast<unsigned char>(text[i]);if(first<128){result.push_back(first);++i;continue;}const int length=first>=0xc2&&first<=0xdf?2:first>=0xe0&&first<=0xef?3:first>=0xf0&&first<=0xf4?4:0;
        if(!length){result.push_back(0xfffd);++i;continue;}int count=1;
        for(;count<length&&i+count<text.size();++count){const auto next=static_cast<unsigned char>(text[i+count]);if((next&192)!=128)break;if(count==1&&((first==0xe0&&next<0xa0)||(first==0xed&&next>=0xa0)||(first==0xf0&&next<0x90)||(first==0xf4&&next>=0x90)))break;}
        if(count==length)result+=utf16(text.substr(i,length));else result.push_back(0xfffd);i+=count;
    }return result;
}
bool unicode_whitespace(char16_t value){return (value>=9&&value<=13)||value==0x20||value==0x85||value==0xa0||value==0x1680||(value>=0x2000&&value<=0x200a)||value==0x2028||value==0x2029||value==0x202f||value==0x205f||value==0x3000;}
std::string trim_utf8(std::string_view text){const auto storage=utf16_replacing(text);std::u16string_view view=storage;while(!view.empty()&&unicode_whitespace(view.front()))view.remove_prefix(1);while(!view.empty()&&unicode_whitespace(view.back()))view.remove_suffix(1);return utf8(view);}
}
