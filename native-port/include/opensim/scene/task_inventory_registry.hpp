// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/task_inventory_item.hpp"
#include <memory>
#include <vector>
namespace opensim::scene {
struct TaskInventoryPart {
    framework::Uuid id, group;
    std::uint32_t local_id = 0;
};
struct TaskInventoryRegistryHooks {
    std::function<TaskInventoryPart()> part;
    std::function<void(std::uint32_t, std::optional<framework::Uuid>)> changed;
    std::function<void()> aggregate_permissions, deep_permissions, clear_scripted, schedule_full;
    std::function<void()> group_changed;
    std::function<void(std::uint32_t, framework::Uuid)> remove_script;
    std::function<void(std::string)> log;
};
class TaskInventoryRegistry {
  public:
    using Item = std::shared_ptr<framework::TaskInventoryItem>;
    explicit TaskInventoryRegistry(TaskInventoryRegistryHooks);
    ~TaskInventoryRegistry();
    TaskInventoryRegistry(const TaskInventoryRegistry&) = delete;
    TaskInventoryRegistry& operator=(const TaskInventoryRegistry&) = delete;
    bool add(Item, bool allowed_drop = false);
    void restore(const std::vector<Item>& items);
    bool update(Item, bool fire_script_events = true, bool consider_changed = true);
    int remove(framework::Uuid);
    Item find(framework::Uuid) const;
    // Dictionary entry-slot order, including LIFO free-slot reuse. Values are the
    // original live objects. Caller synchronizes any direct item-field mutation.
    std::vector<Item> items() const;
    std::vector<Item> scripts() const;
    bool has_script_dictionary() const;
    std::uint32_t serial() const;
    void serial(std::uint32_t);
    bool inventory_changed() const;
    void inventory_changed(bool);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
