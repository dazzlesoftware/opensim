// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/asset_types.hpp"
#include <bit>
#include <stdexcept>
#include <string_view>
#include <utility>
namespace opensim::framework {
namespace {
struct Mapping {
    std::int8_t asset, inventory;
    std::string_view content, alias, extension;
};
constexpr Mapping mappings[] = {
    {-1, -1, "application/octet-stream", "", "bin"},
    {0, 0, "image/x-j2c", "image/jp2", "j2c"},
    {0, 15, "image/x-j2c", "image/jp2", "j2c"},
    {12, 0, "image/tga", "", "tga"},
    {18, 0, "image/tga", "", "tga"},
    {19, 0, "image/jpeg", "", "jpg"},
    {1, 1, "audio/ogg", "application/ogg", "ogg"},
    {17, 1, "audio/x-wav", "", "wav"},
    {2, 2, "application/vnd.ll.callingcard", "application/x-metaverse-callingcard", "callingcard"},
    {3, 3, "application/vnd.ll.landmark", "application/x-metaverse-landmark", "landmark"},
    {5, 18, "application/vnd.ll.clothing", "application/x-metaverse-clothing", "clothing"},
    {6, 6, "application/vnd.ll.primitive", "application/x-metaverse-primitive", "primitive"},
    {6, 17, "application/vnd.ll.primitive", "application/x-metaverse-primitive", "primitive"},
    {7, 7, "application/vnd.ll.notecard", "application/x-metaverse-notecard", "notecard"},
    {10, 10, "application/vnd.ll.lsltext", "application/x-metaverse-lsl", "lsl"},
    {11, 10, "application/vnd.ll.lslbyte", "application/x-metaverse-lso", "lso"},
    {13, 18, "application/vnd.ll.bodypart", "application/x-metaverse-bodypart", "bodypart"},
    {20, 19, "application/vnd.ll.animation", "application/x-metaverse-animation", "animation"},
    {21, 20, "application/vnd.ll.gesture", "application/x-metaverse-gesture", "gesture"},
    {22, 15, "application/x-metaverse-simstate", "", "simstate"},
    {24, -1, "application/vnd.ll.link", "", "link"},
    {25, -1, "application/vnd.ll.linkfolder", "", "linkfolder"},
    {49, 22, "application/vnd.ll.mesh", "", "llm"},
    {57, 26, "application/llsd+xml", "", "glftmat"},
    {8, -1, "application/vnd.ll.folder", "", "folder"},
    {8, 8, "application/vnd.ll.rootfolder", "", "rootfolder"},
    {8, 14, "application/vnd.ll.trashfolder", "", "trashfolder"},
    {8, 15, "application/vnd.ll.snapshotfolder", "", "snapshotfolder"},
    {8, 16, "application/vnd.ll.lostandfoundfolder", "", "lostandfoundfolder"},
    {8, 23, "application/vnd.ll.favoritefolder", "", "favoritefolder"},
    {8, 46, "application/vnd.ll.currentoutfitfolder", "", "currentoutfitfolder"},
    {8, 47, "application/vnd.ll.outfitfolder", "", "outfitfolder"},
    {8, 48, "application/vnd.ll.myoutfitsfolder", "", "myoutfitsfolder"},
    {8, 8, "application/vnd.ll.folder", "", "folder"},
    {-2, -1, "application/llsd+xml", "", "material"}};
struct NameMapping {
    std::string_view name;
    std::int8_t type;
};
constexpr NameMapping asset_names[] = {
    {"texture", 0},   {"sound", 1},    {"callcard", 2},  {"landmark", 3}, {"script", 4},   {"clothing", 5},
    {"object", 6},    {"notecard", 7}, {"category", 8},  {"lsltext", 10}, {"lslbyte", 11}, {"txtr_tga", 12},
    {"bodypart", 13}, {"snd_wav", 17}, {"img_tga", 18},  {"jpeg", 19},    {"animatn", 20}, {"gesture", 21},
    {"simstate", 22}, {"mesh", 49},    {"settings", 56}, {"material", 57}};
constexpr NameMapping inventory_names[] = {
    {"texture", 0},   {"sound", 1},      {"callcard", 2},       {"landmark", 3},      {"script", 4},
    {"clothing", 5},  {"object", 6},     {"notecard", 7},       {"root", 8},          {"lsltext", 10},
    {"bodypart", 13}, {"trash", 14},     {"snapshot", 15},      {"lostandfound", 16}, {"animatn", 20},
    {"gesture", 21},  {"favorites", 23}, {"currentoutfit", 46}, {"outfit", 47},       {"myoutfits", 48},
    {"mesh", 49},     {"settings", 56},  {"material", 57},      {"suitcase", 100}};
std::int8_t wrapped(int value) {
    return std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(value));
}
const std::string &nonnull(const std::optional<std::string> &value) {
    if (!value)
        throw std::invalid_argument("Null asset type lookup key");
    return *value;
}
}
AssetType asset_type_from_code(std::int8_t code) {
    switch (code) {
    case -2:
    case -1:
    case 0:
    case 1:
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 10:
    case 11:
    case 12:
    case 13:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 24:
    case 25:
    case 26:
    case 49:
    case 56:
    case 57:
    case 58:
    case 59:
        return static_cast<AssetType>(code);
    default:
        return AssetType::Unknown;
    }
}
std::int8_t asset_name_type(const std::optional<std::string> &name) {
    const auto &key = nonnull(name);
    for (const auto &[text, type] : asset_names)
        if (text == key)
            return type;
    return -1;
}
std::int8_t inventory_name_type(const std::optional<std::string> &name) {
    const auto &key = nonnull(name);
    for (const auto &[text, type] : inventory_names)
        if (text == key)
            return type;
    return -1;
}
std::string asset_content_type(int type) {
    const auto code = wrapped(type);
    for (const auto &row : mappings)
        if (row.asset == code)
            return std::string(row.content);
    return "application/octet-stream";
}
std::string inventory_content_type(int type) {
    const auto code = wrapped(type);
    for (const auto &row : mappings)
        if (row.inventory == code)
            return std::string(row.content);
    return "application/octet-stream";
}
std::string asset_extension(int type) {
    const auto code = wrapped(type);
    for (const auto &row : mappings)
        if (row.asset == code)
            return std::string(row.extension);
    return "bin";
}
std::int8_t content_asset_type(const std::optional<std::string> &content) {
    const auto &key = nonnull(content);
    for (const auto &row : mappings)
        if (row.content == key || (!row.alias.empty() && row.alias == key))
            return row.asset;
    return -1;
}
std::int8_t content_inventory_type(const std::optional<std::string> &content) {
    const auto &key = nonnull(content);
    for (const auto &row : mappings)
        if (row.content == key || (!row.alias.empty() && row.alias == key))
            return row.inventory;
    return -1;
}
}
