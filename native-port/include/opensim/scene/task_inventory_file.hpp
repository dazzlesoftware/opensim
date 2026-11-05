// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/framework/uuid.hpp"
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <vector>
namespace opensim::scene {
struct TaskInventoryFileItem {
    framework::Uuid item, asset, creator, last_owner, owner, group;
    std::uint32_t creation_date = 0, flags = 0;
    std::uint32_t base_permissions = 0x7fffffffu, current_permissions = 0x7fffffffu,
                  group_permissions = 0x7fffffffu, everyone_permissions = 0x7fffffffu,
                  next_permissions = 0x7fffffffu;
    int type = 0, inventory_type = 0;
    std::string name, description;
};
struct TaskInventoryFileBackend {
    std::function<bool(framework::Uuid, framework::Uuid)> can_edit_inventory;
    std::function<framework::Uuid()> random_uuid = [] { return framework::Uuid::random(); };
    std::function<bool(const std::string&, std::shared_ptr<const std::vector<std::uint8_t>>)> add_file;
    std::function<void(framework::Uuid, std::int16_t, std::vector<std::uint8_t>)> reply;
};
// One per actual object part. Caller supplies a stable inventory snapshot in
// source dictionary order and its real mutation serial. Bytes are registered
// with the real Xfer backend; no physical .tmp file is written by the source.
class TaskInventoryFile {
public:
    TaskInventoryFile(framework::Uuid part, TaskInventoryFileBackend);
    void request(framework::Uuid agent, std::uint32_t serial, std::span<const TaskInventoryFileItem>,
                 bool viewer_ui_god);

private:
    framework::Uuid part_;
    TaskInventoryFileBackend backend_;
    std::recursive_mutex mutex_;
    std::shared_ptr<const std::vector<std::uint8_t>> data_ =
        std::make_shared<const std::vector<std::uint8_t>>();
    std::string filename_;
    std::vector<std::uint8_t> filename_bytes_;
    std::uint32_t filename_serial_ = 0;
    int privileged_ = 0;
};
}
