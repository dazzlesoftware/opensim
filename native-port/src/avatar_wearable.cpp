// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/avatar_wearable.hpp"
#include <algorithm>
namespace opensim::framework {
std::optional<std::size_t> AvatarWearable::slot(const Uuid& item) const {
    for (std::size_t i = 0; i < slots_.size(); ++i)
        if (slots_[i] && slots_[i]->item == item)
            return i;
    return std::nullopt;
}
void AvatarWearable::add(const Uuid& item, const Uuid& asset) {
    if (item.is_zero())
        return;
    if (const auto index = slot(item)) {
        slots_[*index]->asset = asset;
        return;
    }
    if (ids_.size() >= 5)
        return;
    ids_.push_back(item);
    if (free_.empty())
        slots_.push_back(WearableItem{item, asset});
    else {
        const auto index = free_.back();
        free_.pop_back();
        slots_[index] = WearableItem{item, asset};
    }
}
void AvatarWearable::clear() {
    slots_.clear();
    free_.clear();
    ids_.clear();
}
void AvatarWearable::remove_item(const Uuid& item) {
    std::erase(ids_, item);
    if (const auto index = slot(item)) {
        slots_[*index].reset();
        free_.push_back(*index);
    }
}
void AvatarWearable::remove_asset(const Uuid& asset) {
    for (const auto& value : slots_)
        if (value && value->asset == asset) {
            const auto item = value->item;
            remove_item(item);
            return;
        }
}
WearableItem AvatarWearable::at(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= ids_.size())
        return {};
    const auto& item = ids_[static_cast<std::size_t>(index)];
    return {item, asset(item)};
}
Uuid AvatarWearable::asset(const Uuid& item) const {
    const auto index = slot(item);
    return index ? slots_[*index]->asset : Uuid{};
}
std::vector<std::shared_ptr<AvatarWearable>> AvatarWearable::defaults() {
    std::vector<std::shared_ptr<AvatarWearable>> result;
    result.reserve(LegacyVersionMaxWearables);
    for (int i = 0; i < LegacyVersionMaxWearables; ++i)
        result.push_back(std::make_shared<AvatarWearable>());
    result[Body]->add(default_body_item, default_body_asset);
    result[Hair]->add(default_hair_item, default_hair_asset);
    result[Skin]->add(default_skin_item, default_skin_asset);
    result[Eyes]->add(default_eyes_item, default_eyes_asset);
    result[Shirt]->add(default_shirt_item, default_shirt_asset);
    result[Pants]->add(default_pants_item, default_pants_asset);
    return result;
}
}
