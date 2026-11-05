// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/task_inventory_registry.hpp"
#include <map>
#include <mutex>
#include <stdexcept>
namespace opensim::scene {
struct TaskInventoryRegistry::Impl {
    using Item = TaskInventoryRegistry::Item;
    struct Slots {
        struct Entry {
            framework::Uuid key;
            Item value;
        };
        std::map<framework::Uuid, std::size_t> index;
        std::vector<Entry> entries;
        std::vector<std::size_t> free;
        Item find(framework::Uuid key) const {
            auto i = index.find(key);
            return i == index.end() ? Item{} : entries[i->second].value;
        }
        bool set(framework::Uuid key, Item value, bool overwrite) {
            auto i = index.find(key);
            if (i != index.end()) {
                if (overwrite)
                    entries[i->second].value = std::move(value);
                return false;
            }
            std::size_t slot;
            if (free.empty()) {
                slot = entries.size();
                entries.push_back({key, std::move(value)});
            } else {
                slot = free.back();
                free.pop_back();
                entries[slot] = {key, std::move(value)};
            }
            index.emplace(key, slot);
            return true;
        }
        Item erase(framework::Uuid key) {
            auto i = index.find(key);
            if (i == index.end())
                return {};
            auto slot = i->second;
            auto value = std::move(entries[slot].value);
            index.erase(i);
            free.push_back(slot);
            return value;
        }
        std::vector<Item> values() const {
            std::vector<Item> out;
            out.reserve(index.size());
            for (const auto &entry : entries)
                if (entry.value)
                    out.push_back(entry.value);
            return out;
        }
    } items;
    std::optional<Slots> scripts;
    TaskInventoryRegistryHooks hooks;
    mutable std::recursive_mutex mutex;
    std::uint32_t serial = 0;
    bool changed = false;
    bool contains_name(const std::optional<std::string> &name) const {
        for (const auto &[key, slot] : items.index)
            if (items.entries[slot].value->name == name)
                return true;
        return false;
    }
    std::optional<std::string> available_name(const std::optional<std::string> &name) const {
        if (!contains_name(name))
            return name;
        for (int n = 1; n < 256; ++n) {
            auto candidate = name.value_or("") + " " + std::to_string(n);
            if (!contains_name(candidate))
                return candidate;
        }
        return std::string{};
    }
};
TaskInventoryRegistry::TaskInventoryRegistry(TaskInventoryRegistryHooks hooks)
    : impl_(std::make_unique<Impl>()) {
    if (!hooks.part || !hooks.changed || !hooks.aggregate_permissions || !hooks.deep_permissions ||
        !hooks.clear_scripted || !hooks.schedule_full || !hooks.group_changed || !hooks.remove_script)
        throw std::invalid_argument("Incomplete task inventory registry hooks");
    impl_->hooks = std::move(hooks);
}
TaskInventoryRegistry::~TaskInventoryRegistry() = default;
bool TaskInventoryRegistry::add(Item item, bool drop) {
    auto &s = *impl_;
    std::lock_guard lock(s.mutex);
    if (!item)
        throw std::invalid_argument("Null task inventory item");
    auto name = s.available_name(item->name);
    if (!name)
        throw std::invalid_argument("Null inventory name");
    if (name->empty())
        return false;
    auto part = s.hooks.part();
    item->parent_id = part.id;
    item->parent_part_id = part.id;
    item->name = *name;
    item->group_id = part.group;
    if (!s.items.set(item->item_id, item, false))
        return false;
    if (item->inventory_type == 10) {
        if (!s.scripts)
            s.scripts.emplace();
        s.scripts->set(item->item_id, item, true);
    }
    ++s.serial;
    s.changed = true;
    s.hooks.changed(drop ? 64u : 1u, drop ? std::optional(item->item_id) : std::nullopt);
    s.hooks.aggregate_permissions();
    s.hooks.group_changed();
    return true;
}
void TaskInventoryRegistry::restore(const std::vector<Item> &items) {
    auto &s = *impl_;
    bool restored = false;
    {
        std::lock_guard lock(s.mutex);
        for (const auto &item : items) {
            if (!item || !s.items.set(item->item_id, item, false))
                continue;
            if (item->inventory_type == 10) {
                if (!s.scripts)
                    s.scripts.emplace();
                s.scripts->set(item->item_id, item, true);
            }
            restored = true;
        }
        if (restored)
            ++s.serial;
    }
    if (restored)
        s.hooks.aggregate_permissions();
}
bool TaskInventoryRegistry::update(Item item, bool fire, bool consider) {
    auto &s = *impl_;
    std::lock_guard lock(s.mutex);
    if (!item)
        throw std::invalid_argument("Null task inventory item");
    auto old = s.items.find(item->item_id);
    if (!old) {
        if (s.hooks.log)
            s.hooks.log("Task inventory item missing for update");
        return false;
    }
    auto part = s.hooks.part();
    item->parent_id = part.id;
    item->parent_part_id = part.id;
    if (item->group_permissions)
        item->group_id = part.group;
    if (item->owner_id.is_zero())
        item->owner_id = item->group_id;
    if (item->asset_id.is_zero())
        item->asset_id = old->asset_id;
    s.items.set(item->item_id, item, true);
    if (item->inventory_type == 10) {
        if (!s.scripts)
            s.scripts.emplace();
        s.scripts->set(item->item_id, item, true);
    }
    ++s.serial;
    if (fire)
        s.hooks.changed(1, {});
    if (consider) {
        s.hooks.deep_permissions();
        s.changed = true;
        s.hooks.group_changed();
    }
    return true;
}
int TaskInventoryRegistry::remove(framework::Uuid key) {
    auto &s = *impl_;
    std::lock_guard lock(s.mutex);
    auto old = s.items.erase(key);
    if (!old) {
        if (s.hooks.log)
            s.hooks.log("Task inventory item missing for removal");
        return -1;
    }
    int type = old->inventory_type;
    if (s.scripts) {
        s.scripts->erase(key);
        if (s.scripts->index.empty())
            s.scripts.reset();
    }
    if (!s.scripts)
        s.hooks.clear_scripted();
    s.hooks.deep_permissions();
    ++s.serial;
    s.changed = true;
    s.hooks.group_changed();
    s.hooks.schedule_full();
    auto part = s.hooks.part();
    if (type == 10)
        s.hooks.remove_script(part.local_id, key);
    if (s.scripts)
        s.hooks.changed(1, {});
    return type;
}
TaskInventoryRegistry::Item TaskInventoryRegistry::find(framework::Uuid key) const {
    std::lock_guard lock(impl_->mutex);
    return impl_->items.find(key);
}
std::vector<TaskInventoryRegistry::Item> TaskInventoryRegistry::items() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->items.values();
}
std::vector<TaskInventoryRegistry::Item> TaskInventoryRegistry::scripts() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->scripts ? impl_->scripts->values() : std::vector<Item>{};
}
bool TaskInventoryRegistry::has_script_dictionary() const {
    std::lock_guard lock(impl_->mutex);
    return bool(impl_->scripts);
}
std::uint32_t TaskInventoryRegistry::serial() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->serial;
}
void TaskInventoryRegistry::serial(std::uint32_t v) {
    std::lock_guard lock(impl_->mutex);
    impl_->serial = v;
}
bool TaskInventoryRegistry::inventory_changed() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->changed;
}
void TaskInventoryRegistry::inventory_changed(bool v) {
    std::lock_guard lock(impl_->mutex);
    impl_->changed = v;
}
}
