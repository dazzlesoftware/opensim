// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/asset_base.hpp"
#include <span>
namespace opensim::framework {
// NRBF AssetBase graph codec. Never instantiates CLR classes or executes payload
// types. File mode retains private state; legacy mode follows DecodeFromNRBF.
struct AssetNrbfCodec {
 static std::vector<std::uint8_t> encode(const AssetBase&);
 static bool decode_file(std::span<const std::uint8_t>,std::shared_ptr<AssetBase>&);
 static bool decode_legacy(std::span<const std::uint8_t>,std::shared_ptr<AssetBase>&);
private:static bool decode(std::span<const std::uint8_t>,std::shared_ptr<AssetBase>&,bool file);
};
}
