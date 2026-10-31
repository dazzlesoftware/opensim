// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/asset_text.hpp"
#include "opensim/framework/text.hpp"
#include <stdexcept>
namespace opensim::script {
std::string asset_text(std::span<const std::uint8_t> bytes){
 if(bytes.empty())return {};
 std::string_view text(reinterpret_cast<const char*>(bytes.data()),bytes.size());
 if(text.back()=='\0')text.remove_suffix(1);
 return framework::utf8(framework::utf16_replacing(text));
}
std::string asset_text(const framework::AssetBytes& bytes){if(!bytes)throw std::invalid_argument("Null script asset data");return asset_text(std::span<const std::uint8_t>(*bytes));}
}
