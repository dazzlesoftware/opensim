// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#define PROFILES_PROVIDER_ONLY
#include "profiles_service_probe.cpp"
#undef PROFILES_PROVIDER_ONLY
#include "opensim/services/local_profiles_connector.hpp"
namespace opensim::services {
struct LocalProfileConnectorProbe {
    static std::string regions(const LocalProfilesConnector& module,
                               const std::shared_ptr<LocalProfileScene>& a,
                               const std::shared_ptr<LocalProfileScene>& b) {
        std::lock_guard lock(module.mutex_);
        std::string ids;
        for (auto& entry : module.regions_)
            ids += entry.first.str() + ":" + (entry.second == a ? "a" : entry.second == b ? "b" : "d") + ";";
        return ids;
    }
};
}
std::string U(std::string_view s) {
    if (s == ".")
        return {};
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
std::string T(const UserProfilesService::Text& value) {
    return value ? H(utf8(*value)) : "N";
}
struct Scene : LocalProfileScene {
    Uuid id;
    bool info = true;
    Uuid region_id() const override {
        if (!info)
            throw std::runtime_error("Object reference not set to an instance of an object.");
        return id;
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, flags;
        in >> mode >> flags;
        std::string trace, log, output;
        auto server = std::make_shared<server::JsonRpcDispatcher>();
        if (flags & 4)
            server->add(u"classified_update", [](auto&, auto&) { return true; });
        auto service = std::make_shared<ProfilesService>(std::make_shared<Store>(0));
        LocalProfilesConnector module(
            [&]() -> std::shared_ptr<server::JsonRpcDispatcher> {
                trace += "S;";
                if (flags & 2)
                    throw std::runtime_error("server getter failed");
                return flags & 1 ? nullptr : server;
            },
            [&](auto dll, auto, auto name) -> std::shared_ptr<UserProfilesService> {
                trace += "F:" + T(dll) + ":" + T(name) + ";";
                if (mode == 1)
                    return {};
                if (mode == 2)
                    throw std::runtime_error("factory failed");
                return service;
            },
            [&](auto level, auto text) {
                log += (level == LocalProfileLogLevel::Info ? "INFO:" : "") + utf8(text) + "\n";
            });
        auto a = std::make_shared<Scene>();
        a->id = Uuid("00000000-0000-0000-0000-000000000001");
        auto b = std::make_shared<Scene>();
        b->id = Uuid("00000000-0000-0000-0000-000000000002");
        for (std::string token; in >> token;) {
            if (!output.empty())
                output += '/';
            try {
                if (token[0] == 'I' || token[0] == 'S') {
                    std::shared_ptr<NiniConfigSourceBase> source;
                    if (token.substr(1) != "-") {
                        auto arena =
                            std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
                        source = arena->create_ini_source(nini_string_reader(utf16(U(token.substr(1)))));
                    }
                    if (token[0] == 'I')
                        module.initialise(source);
                    else
                        module.initialise_service(source);
                } else if (token == "A")
                    module.add_region(a);
                else if (token == "B")
                    module.add_region(b);
                else if (token == "D") {
                    auto d = std::make_shared<Scene>();
                    d->id = a->id;
                    module.add_region(d);
                } else if (token == "N")
                    module.add_region(nullptr);
                else if (token == "Z") {
                    auto z = std::make_shared<Scene>();
                    z->info = false;
                    module.add_region(z);
                } else if (token == "R")
                    module.remove_region(a);
                else if (token == "T")
                    module.remove_region(b);
                else if (token == "U")
                    module.remove_region(nullptr);
                else if (token == "C")
                    module.close();
                else if (token == "P")
                    module.post_initialise();
                else if (token == "L")
                    module.region_loaded(nullptr);
                output += "OK";
            } catch (const std::exception& e) {
                output += "EX:" + H(e.what());
            }
            output += ":" + std::string(module.enabled ? "1" : "0") + ":" +
                      (module.service_module ? "1" : "0") + ":" + T(module.config_name) + ":" +
                      LocalProfileConnectorProbe::regions(module, a, b);
        }
        std::string keys;
        for (auto key : server->keys())
            keys += utf8(key) + ";";
        std::cout << output << "|" << H(trace) << "|" << H(log) << "|" << H(keys) << "|"
                  << H(utf8(LocalProfilesConnector::name)) << ":N\n";
    }
}
