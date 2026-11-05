#include "opensim/services/appearance_provisioner.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <bit>
using namespace opensim;
using Id = framework::Uuid;
Id id(int n) {
    std::ostringstream s;
    s << "00000000-0000-0000-0000-" << std::hex << std::setw(12) << std::setfill('0') << n;
    return Id(s.str());
}
std::string hex(std::string_view s) {
    constexpr char d[] = "0123456789abcdef";
    std::string r;
    for (unsigned char c : s) {
        r += d[c >> 4];
        r += d[c & 15];
    }
    return r;
}
std::string item_text(const services::InventoryItem& a) {
    return a.id.str() + "," + a.owner.str() + "," + a.asset_id.str() + "," + std::to_string(a.asset_type) +
           "," + a.name + "," + a.description + "," + std::to_string(a.inventory_type) + "," + a.creator_id +
           "," + std::to_string(static_cast<std::uint32_t>(a.next_permissions)) + "," +
           std::to_string(static_cast<std::uint32_t>(a.current_permissions)) + "," +
           std::to_string(static_cast<std::uint32_t>(a.base_permissions)) + "," +
           std::to_string(static_cast<std::uint32_t>(a.everyone_permissions)) + "," +
           std::to_string(static_cast<std::uint32_t>(a.group_permissions)) + "," + a.group_id.str() + "," +
           std::to_string(a.group_owned) + "," + std::to_string(a.sale_price) + "," +
           std::to_string(a.sale_type) + "," + std::to_string(static_cast<std::uint32_t>(a.flags)) + "," +
           std::to_string(a.creation_date) + "," + a.parent.str();
}
std::string appearance_text(framework::AvatarAppearance& a) {
    std::string s = std::to_string(a.serial) + ":";
    for (auto b : *a.visual_params)
        s += std::to_string(b) + ",";
    s += ':';
    for (auto b : a.texture->bytes())
        s += std::to_string(b) + ",";
    s += ':';
    if (!a.wearables)
        s += "null";
    else
        for (auto w : *a.wearables) {
            if (!w) {
                s += "null;";
                continue;
            }
            s += '[';
            for (std::size_t j = 0; j < w->count(); ++j) {
                auto n = w->at(static_cast<int>(j));
                s += n.item.str() + "," + n.asset.str() + ";";
            }
            s += ']';
        }
    s += ':';
    for (auto attachment : a.attachments())
        s += std::to_string(attachment.point) + "," + attachment.item.str() + "," + attachment.asset.str() +
             ";";
    return s;
}
#include "appearance_transfer_fixture.hpp"
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int avatar, outcome, target, mutation, link;
        in >> avatar >> outcome >> target >> mutation >> link;
        std::string trace;
        int step = 0, random = 0, clock = 0, adds = 0;
        std::vector<std::shared_ptr<services::InventoryItem>> items;
        std::map<int, std::shared_ptr<services::InventoryFolder>> folders;
        for (int type : {13, 46, 5}) {
            auto folder = std::make_shared<services::InventoryFolder>();
            folder->id = id(100 + type);
            folders[type] = folder;
        }
        auto touch = [&] {
            ++step;
            if (outcome == 2 && step == target)
                throw std::runtime_error("provider");
            return !(outcome == 1 && step == target);
        };
        services::AppearanceProvisioning ops;
        ops.folder = [&](Id principal, int type) {
            trace += "folder:" + principal.str() + ":" + std::to_string(type) + ";";
            if (!touch())
                return std::shared_ptr<services::InventoryFolder>{};
            return folders.at(type);
        };
        ops.add_item = [&](auto item) {
            trace += "add:" + item_text(*item) + ";";
            bool result = touch();
            items.push_back(item);
            ++adds;
            if (mutation == 1 && adds == 1) {
                item->id = id(900);
                item->asset_id = id(901);
                item->name = "Changed";
                item->flags = 99;
            }
            if (mutation == 2 && adds == 1)
                folders.at(46)->id = id(902);
            if (mutation == 3 && adds == 11) {
                items.front()->id = id(903);
                items.front()->asset_id = id(904);
                items.front()->name = "LateChanged";
            }
            return result;
        };
        if (avatar)
            ops.set_appearance = [&](Id principal, auto a) {
                trace += "appearance:" + principal.str() + ":" + appearance_text(*a) + ";";
                return touch();
            };
        ops.by_name = [&](Id scope, auto first, auto last) {
            trace += "lookup:" + scope.str() + ":" + first + ":" + last + ";";
            bool ok = touch();
            return ok && mutation != 0 ? std::make_shared<services::UserAccount>(id(8)) : nullptr;
        };
        if (avatar)
            ops.get_appearance = [&](Id principal) {
                trace += "get:" + principal.str() + ";";
                bool ok = touch();
                return ok && mutation != 1 ? std::make_shared<framework::AvatarAppearance>() : nullptr;
            };
        ops.copy_model = [&](Id destination, Id source, auto a) {
            trace += "copy:" + destination.str() + ":" + source.str() + ";";
            touch();
            if (mutation == 3)
                throw std::runtime_error("copy");
            a->set_wearable(0, std::make_shared<framework::AvatarWearable>(id(555), id(556)));
        };
        std::shared_ptr<TransferFixture> transfer;
        if (link >= 100) {
            transfer = std::make_shared<TransferFixture>(link - 100);
            folders.at(5)->type = avatar ? 6 : 5;
            folders.at(46)->type = 46;
            ops.root = [&](Id principal) {
                trace += "root:" + principal.str() + ";";
                if (!touch())
                    return std::shared_ptr<services::InventoryFolder>{};
                auto root = std::make_shared<services::InventoryFolder>();
                root->id = id(88);
                return root;
            };
            ops.add_folder = [&](auto folder) {
                trace += "addfolder:" + folder->id.str() + ":" + folder->owner.str() + ":" + folder->name +
                         ":" + std::to_string(folder->type) + ":" + folder->parent.str() + ":" +
                         std::to_string(folder->version) + ";";
                bool result = touch();
                if (mutation == 9)
                    folder->id = id(944);
                return result;
            };
            ops.item = [&](Id principal, Id item) {
                trace += "item:" + principal.str() + ":" + item.str() + ";";
                bool result = touch();
                transfer->queried(item, mutation);
                auto i = transfer->source_items.find(item);
                return result && i != transfer->source_items.end() ? i->second : nullptr;
            };
        }
        services::AppearanceProvisioner service(
            std::move(ops),
            [&] {
                ++random;
                trace += "random:" + std::to_string(random) + ";";
                if (outcome == 3 && random == target)
                    throw std::runtime_error("random");
                if (transfer)
                    transfer->before_random(random, mutation);
                if (mutation == 4 && random == 2 && !items.empty()) {
                    items.front()->id = id(905);
                    items.front()->name = "RandomChanged";
                }
                return id(1000 + random);
            },
            [&] {
                ++clock;
                trace += "clock:" + std::to_string(clock) + ";";
                if (outcome == 4 && clock == target)
                    throw std::runtime_error("clock");
                if (transfer)
                    transfer->before_clock(clock, mutation);
                return 100 + clock;
            });
        const std::vector<std::string> models = {
            "First Last", " First Last", "First  Last",          "First",
            "",           " ",           "First\tLast",          "First\xc2\xa0Last",
            "試験 😀",    "First\nLast", "First\xe2\x80\x8bLast"};
        std::string status = "OK";
        try {
            if (transfer)
                service.copy_model(id(7), id(8), transfer->appearance);
            else if (link >= 3)
                service.establish(id(7), models.at(link - 3));
            else if (link)
                service.create_outfit_link(link == 1 ? 18 : -1, link == 1 ? 5 : 0xffffffffu, "Link 試験 <&>",
                                           id(7), id(8), id(9));
            else
                service.create_defaults(id(7));
        } catch (...) {
            status = "EX";
        }
        std::string final;
        for (auto& item : items)
            final += item_text(*item) + ";";
        std::cout << status << '|' << hex(trace) << '|' << hex(final);
        if (transfer)
            std::cout << '|' << hex(transfer->appearance ? appearance_text(*transfer->appearance) : "null");
        std::cout << '\n';
    }
}
