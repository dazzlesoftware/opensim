#include "opensim/services/appearance_provisioner.hpp"
#include "opensim/services/account_provisioner.hpp"
#include "opensim/services/account_store.hpp"
#include "opensim/services/avatar_service.hpp"
#include "opensim/framework/filesystem.hpp"
#include "opensim/framework/identity.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <bit>
using namespace opensim;
void check(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
std::string path_text(const std::filesystem::path& path) {
    auto s = path.u8string();
    return {s.begin(), s.end()};
}
struct Files {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        framework::utf8_path("model-transfer-試験-" + framework::Uuid::random().str());
    std::string inventory, accounts, avatars;
    Files() {
        std::filesystem::create_directory(root);
        inventory = path_text(root / "inventory.db");
        accounts = path_text(root / "accounts.db");
        avatars = path_text(root / "avatars.db");
    }
    ~Files() {
        std::error_code e;
        for (auto file : {"inventory.db", "accounts.db", "avatars.db"})
            std::filesystem::remove(root / file, e);
        std::filesystem::remove(root, e);
    }
};
int main() {
    try {
        Files files;
        framework::Uuid model_id = framework::Uuid::random(), destination = framework::Uuid::random();
        std::vector<services::InventoryItem> persisted;
        std::shared_ptr<framework::AvatarAppearance> copied;
        {
            services::AccountStore accounts(files.accounts, files.accounts, true);
            services::InventoryFolderStore store(files.inventory, true);
            services::InventoryService inventory(store);
            services::SqliteAvatarStore avatar_store(files.avatars, true);
            services::AvatarService avatars(avatar_store);
            services::UserAccount model({}, model_id, "Template", "Avatar", "mail", [] { return 123; });
            check(accounts.store_account(model), "Model account stored");
            check(inventory.create_user_inventory(model_id), "Model inventory created");
            services::AppearanceProvisioning operations;
            operations.folder = [&](auto owner, int type) {
                auto f = inventory.for_type(owner, type);
                return f ? std::make_shared<services::InventoryFolder>(*f) : nullptr;
            };
            operations.root = [&](auto owner) {
                auto f = inventory.root(owner);
                return f ? std::make_shared<services::InventoryFolder>(*f) : nullptr;
            };
            operations.add_folder = [&](auto folder) { return inventory.add_folder(*folder); };
            operations.add_item = [&](auto item) { return inventory.add_item(*item); };
            operations.item = [&](auto owner, auto id) {
                auto item = inventory.item(owner, id);
                return item ? std::make_shared<services::InventoryItem>(*item) : nullptr;
            };
            operations.by_name = [&](auto scope, auto first, auto last) {
                auto a = accounts.find(scope.str(), first, last);
                return a ? std::make_shared<services::UserAccount>(*a) : nullptr;
            };
            operations.get_appearance = [&](auto owner) { return avatars.get_appearance(owner); };
            operations.set_appearance = [&](auto owner, auto appearance) {
                return avatars.set_appearance(owner, appearance.get());
            };
            services::AppearanceProvisioner appearance(std::move(operations));
            appearance.create_defaults(model_id);
            auto template_appearance = avatars.get_appearance(model_id);
            auto original_body = template_appearance->wearables->at(0)->at(0);
            auto outfit = inventory.for_type(model_id, 46);
            framework::Uuid body_link;
            for (const auto& item : inventory.folder_items(model_id, outfit->id))
                if (item.asset_id == original_body.item)
                    body_link = item.id;
            check(!body_link.is_zero(), "Default model body outfit link exists");
            template_appearance->set_wearable(
                0, std::make_shared<framework::AvatarWearable>(body_link, original_body.asset));
            services::InventoryItem object;
            object.id = framework::Uuid::random();
            object.owner = model_id;
            object.parent = inventory.for_type(model_id, 5)->id;
            object.asset_id = framework::Uuid::random();
            object.asset_type = 6;
            object.inventory_type = 6;
            object.name = "Attached object";
            object.description = "Copied metadata";
            object.creator_id = "creator;https://profile;Name";
            object.next_permissions = 32768;
            object.current_permissions = -1;
            object.base_permissions = 0x8e000;
            object.everyone_permissions = 0x8e000;
            object.group_permissions = 0x8e000;
            object.group_id = framework::Uuid::random();
            object.group_owned = 1;
            object.sale_price = 123;
            object.sale_type = 200;
            object.creation_date = -123;
            object.flags = std::bit_cast<std::int32_t>(0x80000003u);
            check(inventory.add_item(object), "Template attachment stored");
            template_appearance->set_attachment(5, object.id, object.asset_id);
            auto link = object;
            link.id = framework::Uuid::random();
            link.asset_type = 24;
            link.asset_id = object.id;
            link.name = "Attached inventory link";
            check(inventory.add_item(link), "Attachment link stored");
            template_appearance->set_attachment(6, link.id, link.asset_id);
            check(avatars.set_appearance(model_id, template_appearance.get()),
                  "Model appearance persisted before transfer");
            services::AccountProvisioning creation;
            creation.by_name = [&](auto scope, auto first, auto last) {
                auto a = accounts.find(scope.str(), first, last);
                return a ? std::make_shared<services::UserAccount>(*a) : nullptr;
            };
            creation.store = [&](auto& a) { return accounts.store_account(a); };
            creation.set_password = [&](auto id, auto password) {
                return accounts.set_password(id.str(), password);
            };
            creation.create_inventory = [&](auto id) { return inventory.create_user_inventory(id); };
            creation.default_appearance = [&](auto id) { appearance.create_defaults(id); };
            creation.model_appearance = [&](auto id, auto model) { appearance.establish(id, model); };
            services::AccountProvisioner provisioner(std::move(creation), true);
            auto user =
                provisioner.create({}, destination, "Copied", "Avatar", "secret", "mail", "Template Avatar");
            check(user != nullptr, "Full native provisioning copies a model");
            copied = avatars.get_appearance(destination);
            auto clothing = inventory.for_type(destination, 5);
            auto current = inventory.for_type(destination, 46);
            auto clothes = inventory.folder_items(destination, clothing->id);
            auto links = inventory.folder_items(destination, current->id);
            check(clothes.size() == 8 && links.size() == 8,
                  "Six wearables and two attachments copied with eight outfit links");
            check(inventory.folder_items(destination, inventory.for_type(destination, 13)->id).empty(),
                  "All model copies go into Clothing like the source");
            for (int i = 0; i < 6; ++i) {
                auto w = copied->wearables->at(i)->at(0);
                auto item = inventory.item(destination, w.item);
                check(item && item->owner == destination && item->parent == clothing->id &&
                          item->asset_id == w.asset,
                      "Destination wearable uses copied inventory and original wearable asset");
                check(w.item != template_appearance->wearables->at(i)->at(0).item,
                      "Wearable IDs regenerated");
            }
            auto attachments = copied->attachments();
            check(attachments.size() == 2, "Copied attachments retained");
            for (const auto& a : attachments) {
                auto item = inventory.item(destination, a.item);
                check(item && item->owner == destination && item->creation_date == -123 &&
                          item->creator_id == object.creator_id && item->group_id == object.group_id &&
                          item->group_owned == 1 && item->sale_type == 200 && item->sale_price == 123,
                      "Attachment metadata preserved");
                check(item->current_permissions == 32768 && item->base_permissions == 32768 &&
                          item->everyone_permissions == 32768 && item->group_permissions == 0x8e000,
                      "Next-owner mask affects exactly current/base/everyone permissions");
                if (a.point == 6)
                    check(item->asset_type == 24 && item->asset_id == object.id,
                          "Attachment link is copied without wearable-style resolution");
            }
            check(template_appearance->wearables->at(0)->at(0).item == body_link &&
                      template_appearance->attachments().front().item == object.id,
                  "Model service snapshot remains unchanged");
            for (const auto& item : clothes) {
                bool linked = false;
                for (const auto& l : links)
                    if (l.asset_id == item.id && l.asset_type == 24 && l.parent == current->id &&
                        l.current_permissions == 32768)
                        linked = true;
                check(linked, "Each copied item has a Current Outfit link");
            }
            check(static_cast<bool>(accounts.authenticate(destination.str(), framework::md5("secret"), 30)),
                  "Provisioned model user authenticates");
            persisted = store.items({{"avatarID", destination.str()}});
            auto unknown = framework::Uuid::random();
            check(avatars.get_avatar(unknown).avatar_type == 1 && avatars.get_appearance(unknown) != nullptr,
                  "Absent avatar records produce the source default appearance");
            services::AvatarData fields;
            fields.avatar_type = 2;
            fields.data = std::make_shared<services::AvatarData::Fields>();
            (*fields.data)["Serial"] = "7";
            (*fields.data)["AvatarHeight"] = "1,2";
            check(avatars.set_avatar(unknown, fields) &&
                      avatars.get_avatar(unknown).data->at("AvatarHeight") == "1.2",
                  "Avatar service repairs malformed comma heights");
            (*fields.data)["AvatarHeight"] = std::nullopt;
            bool failed = false;
            try {
                avatars.set_avatar(unknown, fields);
            } catch (const std::invalid_argument&) {
                failed = true;
            }
            auto partial = avatars.get_avatar(unknown);
            check(failed && partial.avatar_type == 2 && partial.data->size() == 1 &&
                      partial.data->at("Serial") == "7",
                  "Height exception preserves earlier metadata/serial writes without rollback");
            services::AvatarService::Strings names = {"custom", "AvatarType"}, values = {"text", "8"};
            check(avatars.set_items(unknown, &names, &values) && avatars.get_avatar(unknown).avatar_type == 8,
                  "Item updates retain original metadata type collision");
            names = {"custom", "missing", std::nullopt};
            check(avatars.remove_items(unknown, &names), "Removal ignores absent and null item names");
            check(avatars.reset(unknown) && !avatars.reset(unknown),
                  "Avatar reset preserves provider delete return values");
        }
        {
            services::InventoryFolderStore reopened(files.inventory);
            auto actual = reopened.items({{"avatarID", destination.str()}});
            check(actual.size() == 16, "Transferred inventory survives reopening");
            for (const auto& item : persisted) {
                auto rows = reopened.items({{"inventoryID", item.id.str()}});
                check(rows.size() == 1 && rows.front() == item,
                      "Every transferred metadata field survives SQLite reopening");
            }
            services::SqliteAvatarStore avatar_store(files.avatars);
            services::AvatarService avatars(avatar_store);
            auto loaded = avatars.get_appearance(destination);
            check(loaded && loaded->wearables->at(0)->at(0) == copied->wearables->at(0)->at(0) &&
                      loaded->attachments().size() == 2,
                  "Avatar appearance survives closing and reopening SQLite");
            check(avatars.get_avatar(destination).data->at("VisualParams")->size() > 255,
                  "SQLite avatar visual data is not truncated to the declared VARCHAR width");
        }
        std::cout
            << "PASS: model transfer, SQLite inventory, next-owner permissions and account provisioning\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
