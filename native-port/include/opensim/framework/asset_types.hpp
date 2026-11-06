// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace opensim::framework {
enum class AssetType : std::int8_t {
    OSMaterial = -2,
    Unknown = -1,
    Texture = 0,
    Sound = 1,
    CallingCard = 2,
    Landmark = 3,
    Clothing = 5,
    Object = 6,
    Notecard = 7,
    Folder = 8,
    LSLText = 10,
    LSLBytecode = 11,
    TextureTGA = 12,
    Bodypart = 13,
    SoundWAV = 17,
    ImageTGA = 18,
    ImageJPEG = 19,
    Animation = 20,
    Gesture = 21,
    Simstate = 22,
    Link = 24,
    LinkFolder = 25,
    MarketplaceFolder = 26,
    Mesh = 49,
    Settings = 56,
    Material = 57,
    GLTF = 58,
    GLTF_bin = 59,
};
AssetType asset_type_from_code(std::int8_t code);
std::string_view asset_type_enum_name(std::int8_t code);
std::optional<AssetType> asset_type_from_enum_name(std::string_view name);
std::int8_t asset_name_type(const std::optional<std::string>& name);
std::int8_t inventory_name_type(const std::optional<std::string>& name);
std::string asset_content_type(int type);
std::string inventory_content_type(int type);
std::string asset_extension(int type);
std::int8_t content_asset_type(const std::optional<std::string>& content);
std::int8_t content_inventory_type(const std::optional<std::string>& content);
}
