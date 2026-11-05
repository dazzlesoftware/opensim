// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/asset_base.hpp"
#include <stdexcept>
namespace opensim::framework {
AssetDateTime::AssetDateTime(std::int64_t ticks, int kind)
    : ticks_(ticks), kind_(kind), kind_bits_(static_cast<std::uint8_t>(kind)) {
    if (ticks < 0 || ticks > 3155378975999999999ll || kind < 0 || kind > 2)
        throw std::out_of_range("Invalid asset DateTime");
}
AssetDateTime AssetDateTime::from_serialized(std::uint64_t bits) {
    auto kind = static_cast<int>(bits >> 62);
    AssetDateTime result(static_cast<std::int64_t>(bits & 0x3fffffffffffffffull), kind == 3 ? 2 : kind);
    result.kind_bits_ = static_cast<std::uint8_t>(kind);
    return result;
}
std::string AssetMetadata::id() const {
    if (!id_ || id_->empty())
        id_ = full_id_.str();
    return *id_;
}
void AssetMetadata::id(const std::optional<std::string> &value) {
    Uuid parsed;
    if (value && try_parse_sdk_uuid(*value, parsed)) {
        full_id_ = parsed;
        id_ = parsed.str();
    } else
        id_ = value;
}
std::string AssetMetadata::content_type() const {
    return content_type_ && !content_type_->empty() ? *content_type_ : asset_content_type(type);
}
void AssetMetadata::content_type(const std::optional<std::string> &value) {
    content_type_ = value;
    const auto mapped = content_asset_type(value);
    if (mapped != -1)
        type = mapped;
}
AssetBase::AssetBase() : metadata_(std::make_shared<AssetMetadata>()) {
    metadata_->full_id({});
    metadata_->id(Uuid{}.str());
    metadata_->type = -1;
    metadata_->creator_id = std::string{};
}
AssetBase::AssetBase(const Uuid &id, const std::optional<std::string> &name, std::int8_t type,
                     const std::optional<std::string> &creator)
    : metadata_(std::make_shared<AssetMetadata>()) {
    metadata_->full_id(id);
    metadata_->name = name;
    metadata_->type = type;
    metadata_->creator_id = creator;
}
AssetBase::AssetBase(const std::optional<std::string> &id, const std::optional<std::string> &name,
                     std::int8_t type, const std::optional<std::string> &creator)
    : metadata_(std::make_shared<AssetMetadata>()) {
    metadata_->id(id);
    metadata_->name = name;
    metadata_->type = type;
    metadata_->creator_id = creator;
}
AssetMetadata &AssetBase::required() const {
    if (!metadata_)
        throw std::invalid_argument("Null asset metadata");
    return *metadata_;
}
bool AssetBase::is_binary() const {
    switch (type()) {
    case -1:
    case 0:
    case 1:
    case 6:
    case 8:
    case 11:
    case 12:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 49:
        return true;
    default:
        return false;
    }
}
bool AssetBase::contains_references() const {
    if (!is_textual())
        return false;
    const auto value = type();
    return value != 7 && value != 2 && value != 10 && value != 3;
}
}
