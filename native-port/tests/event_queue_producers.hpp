#pragma once
#include <bit>
void produce(EventQueueModule& m, framework::Uuid a, char op, int p) {
    using framework::Uuid;
    using framework::Vector3;
    using S = std::optional<std::string>;
    Uuid id = p & 1 ? Uuid{} : Uuid("01234567-89ab-cdef-0123-456789abcdef");
    S text;
    switch (p % 6) {
    case 0:
        break;
    case 1:
        text = "";
        break;
    case 2:
        text = "<&\"'>";
        break;
    case 3:
        text = "世界😀";
        break;
    case 4: {
        std::string t;
        for (int i = 0; i < 300; ++i)
            t += "é";
        text = t;
        break;
    }
    default:
        text = std::string("a\0b", 3);
    }
    auto endpoint = std::make_shared<framework::IpEndpoint>();
    endpoint->address = std::make_shared<framework::IpAddress>(
        *framework::IpAddress::parse(p & 1 ? "fe80::1%12" : "192.0.2.37"));
    endpoint->port = p & 2 ? 65535 : 0;
    if (p & 16)
        endpoint.reset();
    int32_t n = p & 2 ? int32_t(-1) : 0;
    uint32_t u = uint32_t(n);
    uint64_t handle = p & 2 ? UINT64_MAX : 0;
    Vector3 v{float(p) / 7, -0.0f, p & 4 ? 1e-20f : -123.25f};
    if (p >= 32) {
        constexpr uint32_t bits[] = {0x7f800000, 0xff800000, 0x7fc00000, 0x80000000,
                                     1,          0x7f7fffff, 0x3eaaaaab, 0x00800000};
        v.x = std::bit_cast<float>(bits[p % 8]);
    }
    auto bucket =
        p & 4 ? server::PollBytes{}
              : std::make_shared<std::vector<uint8_t>>(std::initializer_list<uint8_t>{0, 128, 255, '<', '&'});
    switch (op) {
    case 'e':
        m.enable_simulator(handle, endpoint, a, n, 256);
        break;
    case 'f':
        m.teleport_finish(handle, uint8_t(u), endpoint, u, u, text, a, n, 256);
        break;
    case 'c':
        m.cross_region(handle, v, v, endpoint, text, a, id, n, 256);
        break;
    case 'a':
        m.establish_communication(a, endpoint, text, handle, n, 256);
        break;
    case 'i':
        m.chatterbox_invitation(id, text, id, text, a, text, uint8_t(u), u, p & 1, n, v, u, id, p & 2,
                                bucket);
        break;
    case 'u': {
        std::vector<EventGroupMember> list;
        for (int i = 0; i < p % 3; ++i) {
            switch ((p / 4) % 4) {
            case 0:
                list.emplace_back(id);
                break;
            case 1:
                list.emplace_back(id, bool(p & 8));
                break;
            case 2:
                list.emplace_back(id, bool(p & 1), bool(p & 2), bool(p & 4), bool(p & 8));
                break;
            default:
                list.emplace_back();
                break;
            }
        }
        m.chatterbox_updates(id, a, p == 31 ? nullptr : &list);
        break;
    }
    case 'r':
        m.chatterbox_start(id, text, n, p & 1, p & 2, id, p & 4, text, a);
        break;
    case 'q':
        m.chatterbox_close(a, id, text);
        break;
    case 'g': {
        std::vector<std::shared_ptr<EventGroupMembership>> list;
        for (int i = 0; i < p % 3; ++i) {
            auto g = std::make_shared<EventGroupMembership>();
            g->group = id;
            g->picture = id;
            g->name = text;
            g->powers = handle;
            g->contribution = n;
            g->notices = p & 1;
            g->profile = p & 2;
            list.push_back(p & 16 ? nullptr : g);
        }
        m.group_membership(a, p == 31 ? nullptr : &list);
        break;
    }
    case 'p': {
        std::vector<std::shared_ptr<EventPlace>> list;
        for (int i = 0; i < p % 3; ++i) {
            auto g = std::make_shared<EventPlace>();
            g->owner = id;
            g->snapshot = id;
            g->name = text;
            g->description = text;
            g->sim_name = text;
            g->x = u;
            g->y = u;
            g->z = u;
            g->dwell = u;
            g->actual_area = n;
            g->billable_area = n;
            g->price = n;
            g->flags = uint8_t(u);
            list.push_back(p & 16 ? nullptr : g);
        }
        m.places_reply(a, id, id, p == 31 ? nullptr : &list);
        break;
    }
    case 'j':
        m.script_running(id, id, p & 1, a);
        break;
    case 'h':
        m.physics_properties(u, uint8_t(u), v.x, v.y, v.z, float(n), a);
        break;
    case 'w':
        m.windlight_refresh(p - 16, a);
        break;
    case 'l': {
        std::vector<S> list{text, S("next")};
        m.large_message(a, p & 1 ? std::optional(id) : std::nullopt, p & 2 ? std::optional(id) : std::nullopt,
                        text, id, p == 31 ? nullptr : &list);
        break;
    }
    case 't': {
        std::vector<server::PollBytes> list{bucket, bytes("next")};
        m.large_message_bytes(a, p & 1 ? std::optional(id) : std::nullopt,
                              p & 2 ? std::optional(id) : std::nullopt, text, id, p == 31 ? nullptr : &list);
        break;
    }
    case 'v': {
        InventoryItem item;
        item.id = id;
        item.asset_id = id;
        item.parent = id;
        item.owner = id;
        item.group_id = id;
        item.creator_id = id.str();
        item.name = text.value_or("");
        item.description = item.name;
        item.asset_type = n;
        item.inventory_type = n;
        item.creation_date = n;
        item.flags = n;
        item.current_permissions = n;
        item.next_permissions = n;
        item.base_permissions = n;
        item.everyone_permissions = n;
        item.group_permissions = n;
        item.sale_price = n;
        item.sale_type = p;
        item.group_owned = p & 1;
        m.bulk_inventory(p == 31 ? nullptr : &item, a, p & 1 ? std::optional(id) : std::nullopt);
        break;
    }
    case 'b': {
        auto value =
            p == 31 ? std::shared_ptr<network::Value>{} : std::make_shared<network::Value>(text.value_or(""));
        auto data = m.build_event(text, value);
        std::cout << probe_hex(std::string(data->begin(), data->end()));
        break;
    }
    case 'k':
        std::cout << probe_hex(m.keep_alive_event());
        break;
    case 's': {
        auto buffer = m.start_event(text, p - 16);
        auto data = m.end_event(buffer);
        std::cout << probe_hex(std::string(data->begin(), data->end()));
        break;
    }
    }
}
