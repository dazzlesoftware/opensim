/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#pragma once
#include "opensim/services/inventory_store.hpp"
#include <functional>
#include <mutex>
#include <unordered_map>
namespace opensim::services {
class InventoryFolderTree : public InventoryFolder, public std::enable_shared_from_this<InventoryFolderTree> {
  public:
    InventoryFolderTree() = default;
    explicit InventoryFolderTree(const InventoryFolder& folder) : InventoryFolder(folder) {}
    std::shared_ptr<InventoryFolderTree> create_child(const framework::Uuid& id, const std::string& name,
                                                      std::uint16_t type);
    void add_child(const std::shared_ptr<InventoryFolderTree>& folder);
    bool contains_child(const framework::Uuid& id) const;
    std::shared_ptr<InventoryFolderTree> child(const framework::Uuid& id) const;
    std::shared_ptr<InventoryFolderTree> remove_child(const framework::Uuid& id);
    void add_item(const std::shared_ptr<InventoryItem>& item);
    void purge();
    std::shared_ptr<InventoryItem> find_item(const framework::Uuid& id) const;
    std::shared_ptr<InventoryItem> find_asset(const framework::Uuid& id) const;
    bool delete_item(const framework::Uuid& id);
    std::shared_ptr<InventoryFolderTree> find_folder(const framework::Uuid& id);
    std::shared_ptr<InventoryFolderTree> find_type(int type) const;
    std::shared_ptr<InventoryFolderTree> find_folder_path(std::string path);
    std::shared_ptr<InventoryItem> find_item_path(const std::string& path) const;
    std::vector<std::shared_ptr<InventoryItem>> items() const;
    std::vector<std::shared_ptr<InventoryFolderTree>> children() const;
    int total_count() const;

  private:
    mutable std::recursive_mutex mutex_;
    std::vector<std::shared_ptr<InventoryFolderTree>> children_;
    std::vector<std::shared_ptr<InventoryItem>> items_;
};
class LibraryService {
  public:
    using Log = std::function<void(const std::string&)>;
    explicit LibraryService(const std::string& control = "inventory/Libraries.xml",
                            const std::string& name = "OpenSim Library", Log log = {});
    std::shared_ptr<InventoryFolderTree> root() const;
    std::unordered_map<framework::Uuid, std::shared_ptr<InventoryFolderTree>, framework::UuidHash>
    all_folders() const;
    std::vector<std::shared_ptr<InventoryFolderTree>> ordered_folders() const;
    std::shared_ptr<InventoryItem> item(const framework::Uuid& id) const;
    std::optional<std::vector<std::shared_ptr<InventoryItem>>>
    multiple_items(const std::vector<framework::Uuid>& ids) const;
    std::shared_ptr<InventoryItem> create_item(const framework::Uuid& id, const framework::Uuid& asset,
                                               const std::string& name, const std::string& description,
                                               int asset_type, int inventory_type,
                                               const framework::Uuid& parent) const;

  private:
    struct State;
    std::shared_ptr<State> state_;
};
}
