// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/viewer_name.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/viewer_unicode_data.hpp"
#include <algorithm>
#include <stdexcept>
namespace opensim::framework {
bool viewer_char_is_letter(char16_t value){auto found=std::lower_bound(viewer_unicode::letters.begin(),viewer_unicode::letters.end(),static_cast<std::uint32_t>(value),[](const auto& range,std::uint32_t scalar){return range[1]<scalar;});return found!=viewer_unicode::letters.end()&&(*found)[0]<=value;}
std::string viewer_name(const AgentCircuitData* circuit){
    if(!circuit)throw std::invalid_argument("Null agent circuit");
    auto raw=circuit->viewer();auto name=raw?trim_utf8(*raw):std::string{};
    for(char16_t c:utf16_replacing(name))if(viewer_char_is_letter(c))return name;
    if(circuit->channel&&!circuit->channel->empty())name=trim_utf8(*circuit->channel)+" "+name;
    return name;
}
std::u16string lower_invariant_culture(std::u16string_view text){
    std::u16string result;result.reserve(text.size());
    for(std::size_t i=0;i<text.size();++i){std::uint32_t scalar=text[i];if(scalar>=0xd800&&scalar<=0xdbff&&i+1<text.size()&&text[i+1]>=0xdc00&&text[i+1]<=0xdfff){scalar=0x10000+((scalar-0xd800)<<10)+(text[++i]-0xdc00);}
        auto found=std::lower_bound(viewer_unicode::lower.begin(),viewer_unicode::lower.end(),scalar,[](const auto& entry,std::uint32_t n){return entry[0]<n;});if(found!=viewer_unicode::lower.end()&&(*found)[0]==scalar)scalar=(*found)[1];
        if(scalar<0x10000)result.push_back(static_cast<char16_t>(scalar));else{scalar-=0x10000;result.push_back(static_cast<char16_t>(0xd800+(scalar>>10)));result.push_back(static_cast<char16_t>(0xdc00+(scalar&1023)));}
    }return result;
}
std::string lower_invariant_culture(std::string_view text){return utf8(lower_invariant_culture(std::u16string_view(utf16_replacing(text))));}
}
