// Shared default TimeZoneInfo cache bridge: actual BCL oracle in companion reference.
#include "opensim/framework/invariant_datetime_context.hpp"
#include "opensim/framework/time_zone.hpp"
#include "opensim/framework/runtime_environment.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <algorithm>
#include <map>
#include <sstream>
using namespace opensim::framework;
namespace {
RuntimeEnvironmentText dec(const std::string& x) {
    if (x == "-")
        return {};
    std::u16string v;
    if (x != ".")
        for (std::size_t i = 0; i < x.size(); i += 4)
            v += static_cast<char16_t>(std::stoul(x.substr(i, 2), nullptr, 16) |
                                       (std::stoul(x.substr(i + 2, 2), nullptr, 16) << 8));
    return std::make_shared<const std::u16string>(std::move(v));
}
std::vector<std::string> split(std::string_view s, char c) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        auto i = s.find(c, at);
        out.emplace_back(s.substr(at, i == s.npos ? s.npos : i - at));
        if (i == s.npos)
            return out;
        at = i + 1;
    }
}
struct Slot {
    std::shared_ptr<TimeZone> zone;
    std::shared_ptr<const InvariantTimeZoneRules> rules;
};
std::string query(const std::shared_ptr<const InvariantTimeZoneRules>& rules, std::int64_t ticks) {
    auto o = rules->utc_offset(ticks);
    auto d = rules->utc_is_daylight(ticks);
    return "Q:" + std::to_string(o.ticks) + ":" + (d ? "1" : "0") + ":" + (o.ambiguous_daylight ? "1" : "0") +
           ":" + (d ? "1" : "0");
}
std::string run(std::string row) {
    std::map<int, std::shared_ptr<NativeInvariantDateTimeContext>> owners;
    std::map<int, std::unique_ptr<TimeZoneDatabase>> dbs;
    std::map<int, Slot> zones;
    std::vector<std::shared_ptr<TimeZone>> identities;
    auto owner = [&](int n) { return n < 0 ? shared_native_invariant_datetime_context() : owners.at(n); };
    auto db = [&](int n) -> TimeZoneDatabase& {
        if (n < 0)
            return time_zones();
        if (!dbs.contains(n))
            dbs[n] = std::make_unique<TimeZoneDatabase>(owner(n));
        return *dbs.at(n);
    };
    auto identify = [&](auto z) {
        auto i = std::find(identities.begin(), identities.end(), z);
        if (i == identities.end()) {
            identities.push_back(z);
            return identities.size() - 1;
        }
        return static_cast<std::size_t>(i - identities.begin());
    };
    std::string out;
    for (auto& token : split(row, ',')) {
        auto a = split(token, ':');
        auto result = a[0];
        try {
            if (a[0] == "E")
                runtime_environment_set(dec(a.at(1)), dec(a.at(2)));
            else if (a[0] == "C")
                shared_native_invariant_datetime_context()->clear_cached_time_zone_data();
            else if (a[0] == "N")
                owners[std::stoi(a.at(1))] = std::make_shared<NativeInvariantDateTimeContext>();
            else if (a[0] == "PC")
                owner(std::stoi(a.at(1)))->clear_cached_time_zone_data();
            else if (a[0] == "L") {
                int n = a.size() > 2 ? std::stoi(a[2]) : -1;
                auto z = db(n).local();
                zones[std::stoi(a.at(1))] = {z, owner(n)->local_rule_snapshot()};
                result = "Z:" + std::to_string(identify(z));
            } else if (a[0] == "F") {
                auto z = time_zones().find(utf8(*dec(a.at(2))));
                zones[std::stoi(a.at(1))] = {z, {}};
                result = "Z:" + std::to_string(identify(z));
            } else if (a[0] == "I") {
                auto value = utf16_replacing(zones.at(std::stoi(a.at(1))).zone->id());
                std::string h;
                for (auto c : value)
                    for (unsigned char b :
                         {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
                        h += "0123456789abcdef"[b >> 4];
                        h += "0123456789abcdef"[b & 15];
                    }
                result = "I:" + (h.empty() ? std::string(".") : h);
            } else if (a[0] == "R")
                result = std::string("R:") +
                         (zones.at(std::stoi(a.at(1))).zone == zones.at(std::stoi(a.at(2))).zone ? "1" : "0");
            else if (a[0] == "Q")
                result = query(zones.at(std::stoi(a.at(1))).rules, std::stoll(a.at(2)));
            else if (a[0] == "U")
                result = query(owner(std::stoi(a.at(1)))->local_rule_snapshot(), std::stoll(a.at(2)));
            else if (a[0] == "D")
                result = std::string("D:") +
                         (zones.at(std::stoi(a.at(1))).zone->daylight_at(std::stoll(a.at(2))) ? "1" : "0");
            else
                throw std::runtime_error("operation");
        } catch (const std::exception&) {
            result = "EX";
        }
        if (!out.empty())
            out += '|';
        out += result;
    }
    return out;
}
}
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
void units() {
    auto process = shared_native_invariant_datetime_context();
    auto baseline = process->local_rule_snapshot();
    auto other = std::make_shared<NativeInvariantDateTimeContext>();
    auto old = other->local_rule_snapshot();
    other->clear_cached_time_zone_data();
    require(old != other->local_rule_snapshot(), "Independent epoch replacement");
    require(baseline == process->local_rule_snapshot(), "Independent clear must not reset default runtime");
    auto db = std::make_unique<TimeZoneDatabase>(other);
    auto first = db->local();
    auto before = first->daylight_at(1719792000);
    other->clear_cached_time_zone_data();
    auto after = db->local();
    require(first != after, "Context clear must invalidate associated legacy local object");
    require(first->daylight_at(1719792000) == before, "Old returned local snapshot must remain valid");
    auto utc = db->find(std::string("utc"));
    db->clear();
    require(utc == db->find(std::string("UTC")), "UTC intrinsic identity survives epochs");
    std::weak_ptr<NativeInvariantDateTimeContext> weak = other;
    other.reset();
    require(!weak.expired(), "Database retains exact supplied owner");
    db.reset();
    require(weak.expired(), "Retained immutable TimeZone must not keep owner cycle alive");
    require(first->daylight_at(1719792000) == before, "Snapshot remains usable after owner destruction");
#ifdef _WIN32
    int calls = 0;
    std::shared_ptr<NativeInvariantDateTimeContext> owner;
    auto identity = std::make_shared<int>(17);
    std::weak_ptr<int> identity_weak = identity;
    NativeInvariantDateTimeContextOptions options;
    options.iana_to_windows = [&, identity](std::u16string_view input) {
        require(input == u"America/New_York", "Exact provider input");
        ++calls;
        if (calls == 1)
            owner->clear_cached_time_zone_data();
        return std::optional<std::u16string>(u"UTC");
    };
    owner = std::make_shared<NativeInvariantDateTimeContext>(std::move(options));
    identity.reset();
    TimeZoneDatabase reentrant(owner);
    auto a = reentrant.find(std::string("America/New_York"));
    auto b = reentrant.find(std::string("America/New_York"));
    auto c = reentrant.find(std::string("America/New_York"));
    require(calls == 2 && a != b && b == c,
            "Clear inside converter must keep old lease and use new epoch next call");
    require(!identity_weak.expired(), "Provider closure identity survives Clear");
    require(!a->daylight_at(1719792000), "Use caller-provided converter instead of default converter");
#endif
}
int main(int argc, char** argv) {
    try {
        if (argc == 1) {
            units();
            std::cout << "PASS shared timezone cache owner/snapshot/provider/reentry\n";
            return 0;
        }
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << run(row) << '\n';
        } else
            std::cout << run(argv[1]) << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
