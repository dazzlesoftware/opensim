#include "opensim/services/friends_service.hpp"
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace opensim;
using namespace services;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
int main() {
    try {
        SqliteFriendsStore store(":memory:", true);
        FriendsService service(store);
        auto a = framework::Uuid::random(), b = framework::Uuid::random();
        check(service.store(a.str(), b.str(), 7), "store one-way friend");
        auto rows = service.get(a);
        check(rows.size() == 1 && rows[0].principal == a && rows[0].friend_id == b.str() &&
                  rows[0].my_flags == 7 && rows[0].their_flags == -1,
              "one-way friend has source missing reverse permission -1");
        check(service.get(std::optional<std::string>(a.str())).empty(),
              "source string lookup skips newly constructed null friend");
        check(!friend_is_confirmed(service, a, b, false), "pending friendship is unconfirmed");
        check(service.store(b.str(), a.str(), 5), "store reverse relationship");
        rows = service.get(a);
        check(rows[0].their_flags == 5 && service.get(b)[0].their_flags == 7,
              "self-join exposes independent directional permissions");
        check(friend_can_modify_objects(service, a, b, false), "owner grants object editing to friend");
        check(service.store(b.str(), a.str(), 1), "revoke object editing");
        check(friend_is_confirmed(service, a, b, false), "confirmed friendship needs no object edit permission");
        check(!friend_can_modify_objects(service, a, b, false) &&
                  friend_can_modify_objects(service, b, a, false),
              "object edit grant is directional and revocation takes effect");
        check(service.store(b.str(), a.str(), 5), "restore object editing");
        auto raw = std::make_shared<FriendsData>();
        raw->principal = a.str();
        raw->friend_id = b.str();
        raw->data = std::make_shared<FriendsData::Fields>();
        (*raw->data)["Flags"] = "3";
        (*raw->data)["Offered"] = "123";
        check(store.store(raw) && store.get(a)[0]->data->at("Offered") == std::optional<std::string>("123"),
              "generic friends data includes Offered metadata");
        service.store(a.str(), b.str(), -2147483647);
        check(store.get(a)[0]->data->at("Offered") == std::optional<std::string>("0"),
              "REPLACE through service resets Offered to schema default");
        std::atomic<bool> okay = true;
        std::vector<std::thread> threads;
        for (int n = 0; n < 8; ++n)
            threads.emplace_back([&] {
                for (int i = 0; i < 200; ++i) {
                    auto value = service.get(a);
                    if (value.size() != 1 || value[0].my_flags != -2147483647 || value[0].their_flags != 5)
                        okay = false;
                }
            });
        for (auto& thread : threads)
            thread.join();
        check(okay, "concurrent friends reads retain paired permissions");
        check(service.erase(b, a.str()) && service.get(a)[0].their_flags == -1,
              "delete reverse relationship updates forward permissions");
        check(service.erase(b, a.str()), "deleting absent friends preserves source true result");
        check(!friend_can_modify_objects(service, a, b, false),
              "pending friendship grants no object editing");
        FriendInfo empty;
        check(!empty.friend_id && empty.principal.is_zero() && empty.my_flags == 0 &&
                  empty.to_key_value_pairs().size() == 4,
              "default friend DTO preserves null friend and zero flags");
        (*raw->data)["Flags"] = "bad";
        store.store(raw);
        bool threw = false;
        try {
            service.get(a);
        } catch (const std::exception&) {
            threw = true;
        }
        check(threw && service.get(std::optional<std::string>(a.str())).empty(),
              "UUID path propagates corrupt permissions while string path skips conversion");
        std::cout << "PASS: native friends storage, directional permissions and concurrency\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what();
        return 1;
    }
}
