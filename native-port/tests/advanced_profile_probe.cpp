// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/advanced_profile.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace framework;
using namespace scene;
namespace {
int flags;
std::string trace;
void Fail(int flag, const char* message) {
    if (flags & flag)
        throw std::runtime_error(message);
}
std::string H(std::string_view s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::string T(const ProfileText& s) {
    if (!s)
        return "-";
    std::string out;
    for (auto c : *s) {
        out += static_cast<char>(c & 255);
        out += static_cast<char>(c >> 8);
    }
    return H(out);
}
std::string Decode(std::string s) {
    if (s == ".")
        return {};
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
struct Grid : ProfileGridInfo {
    std::string id;
    explicit Grid(std::string n) : id(n) {
    }
};
struct Groups : ProfileGroups {
    std::string id;
    explicit Groups(std::string n) : id(n) {
    }
};
struct Users : ProfileUsers {
    std::string id;
    explicit Users(std::string n) : id(n) {
    }
};
struct Environment : AdvancedProfileEnvironment {
    std::vector<std::function<void()>> handlers;
    ProfileUriResult resolve_uri(const ProfileText& s) override {
        trace += "uri:" + T(s) + ";";
        Fail(4, "uri");
        if (flags & 8388608)
            return AdvancedProfileEnvironment::resolve_uri(s);
        return {!(flags & 2), !(flags & 3), u"normalized/" + s.value_or(u"")};
    }
    void log(AdvancedProfileLog level, std::u16string_view s) override {
        trace += (level == AdvancedProfileLog::Debug ? "D:" : "E:") + utf8(s) + ";";
        Fail(level == AdvancedProfileLog::Debug ? 8 : 16,
             level == AdvancedProfileLog::Debug ? "debug" : "error");
    }
    void add_status_command(ProfileStatusCommand c) override {
        trace += "cmd:" + utf8(c.module) + ":" + (c.shared ? "1" : "0") + ":" + utf8(c.command) + ":" +
                 utf8(c.short_help) + ":" + utf8(c.long_help) + ";";
        Fail(32, "command");
        handlers.push_back(c.handler);
    }
    void output(const ProfileText& s) override {
        trace += "out:" + utf8(s.value_or(u"")) + ";";
        Fail(1048576, "output");
    }
};
struct Region : AdvancedProfileRegion {
    std::string id;
    std::shared_ptr<Grid> grid;
    std::shared_ptr<Groups> groups;
    std::shared_ptr<Users> users;
    std::vector<std::function<void(std::shared_ptr<AdvancedProfileClient>)>> fresh;
    std::vector<std::function<void(Uuid, std::shared_ptr<AdvancedProfileRegion>)>> closed;
    explicit Region(std::string n)
        : id(n), grid(std::make_shared<Grid>(n)), groups(std::make_shared<Groups>(n)),
          users(std::make_shared<Users>(n)) {
    }
    ProfileText name() override {
        trace += "name:" + id + ";";
        Fail(524288, "name");
        return flags & 262144 ? ProfileText{} : ProfileText(utf16(id));
    }
    std::shared_ptr<ProfileGridInfo> grid_info() override {
        trace += "grid:" + id + ";";
        Fail(128, "grid");
        return flags & 64 ? nullptr : grid;
    }
    void register_profile(std::shared_ptr<AdvancedProfileLifecycle>) override {
        trace += "register:" + id + ";";
        Fail(256, "register");
    }
    std::shared_ptr<ProfileGroups> groups_module() override {
        trace += "groups:" + id + ";";
        Fail(1024, "groups");
        return flags & 512 ? nullptr : groups;
    }
    std::shared_ptr<ProfileUsers> users_module() override {
        trace += "users:" + id + ";";
        Fail(4096, "users");
        return flags & 2048 ? nullptr : users;
    }
    void subscribe_new_client(std::function<void(std::shared_ptr<AdvancedProfileClient>)> fn) override {
        trace += "new-sub:" + id + ";";
        Fail(8192, "new-sub");
        fresh.push_back(fn);
    }
    void
    subscribe_client_closed(std::function<void(Uuid, std::shared_ptr<AdvancedProfileRegion>)> fn) override {
        trace += "closed-sub:" + id + ";";
        Fail(16384, "closed-sub");
        closed.push_back(fn);
    }
};
struct Module : AdvancedProfileLifecycle {
    int profiles = 7, classifieds = 8, interest = 9;
    using AdvancedProfileLifecycle::AdvancedProfileLifecycle;
    void new_client(std::shared_ptr<AdvancedProfileClient> client) override {
        trace += "new:" + std::string(client ? "1" : "0") + ";";
    }
    void client_closed(Uuid, std::shared_ptr<AdvancedProfileRegion> region) override {
        trace += "closed:" + (region ? std::static_pointer_cast<Region>(region)->id : "N") + ";";
    }
    void clear_profiles() override {
        trace += "clear:profiles;";
        Fail(32768, "profiles");
        profiles = 0;
    }
    void clear_classifieds() override {
        trace += "clear:classifieds;";
        Fail(65536, "classifieds");
        classifieds = 0;
    }
    void clear_classified_interest() override {
        trace += "clear:interest;";
        Fail(131072, "interest");
        interest = 0;
    }
    std::int32_t local_queue_count() const override {
        trace += "local;";
        Fail(2097152, "local");
        return 2;
    }
    std::int32_t hg_queue_count() const override {
        trace += "hg;";
        Fail(4194304, "hg");
        return 3;
    }
    std::string state() {
        return std::string(enabled() ? "1" : "0") + ":" + (allow_web_urls() ? "1" : "0") + ":" +
               T(profile_server_uri) + ":" + (scene() ? std::static_pointer_cast<Region>(scene())->id : "N") +
               ":" + (grid_info() ? std::static_pointer_cast<Grid>(grid_info())->id : "N") + ":" +
               (groups_module() ? std::static_pointer_cast<Groups>(groups_module())->id : "") + ":" +
               (users_module() ? std::static_pointer_cast<Users>(users_module())->id : "") + ":" +
               (config() ? "1" : "0") + ":" + std::to_string(profiles) + ":" + std::to_string(classifieds) +
               ":" + std::to_string(interest);
    }
};
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        in >> flags;
        trace.clear();
        auto environment = std::make_shared<Environment>();
        auto module = std::make_shared<Module>(environment);
        auto a = std::make_shared<Region>("A"), b = std::make_shared<Region>("B");
        std::string output;
        for (std::string token; in >> token;) {
            std::string result = "OK";
            try {
                if (token[0] == 'I') {
                    std::shared_ptr<NiniConfigSourceBase> config;
                    if (token.substr(1) != "-") {
                        auto arena =
                            std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
                        config = arena->create_ini_source(nini_string_reader(utf16(Decode(token.substr(1)))));
                    }
                    module->initialise(config);
                } else if (token == "W")
                    module->config(nullptr);
                else if (token == "U")
                    module->profile_server_uri = u"manual";
                else if (token == "A")
                    module->add_region(a);
                else if (token == "B")
                    module->add_region(b);
                else if (token == "N")
                    module->add_region(nullptr);
                else if (token == "L")
                    module->region_loaded(b);
                else if (token == "Z")
                    module->region_loaded(nullptr);
                else if (token == "R")
                    module->remove_region(nullptr);
                else if (token == "C")
                    module->close();
                else if (token == "S")
                    module->show_status();
                else if (token == "X") {
                    auto list = environment->handlers;
                    for (auto& fn : list)
                        fn();
                } else if (token == "E") {
                    auto list = a->fresh;
                    for (auto& fn : list)
                        fn(nullptr);
                } else if (token == "F") {
                    auto list = b->fresh;
                    for (auto& fn : list)
                        fn(std::make_shared<AdvancedProfileClient>());
                } else if (token == "D") {
                    auto list = a->closed;
                    for (auto& fn : list)
                        fn({}, b);
                } else if (token == "T") {
                    auto list = b->closed;
                    for (auto& fn : list)
                        fn({}, nullptr);
                }
            } catch (const std::exception& e) {
                result = "EX:" + H(e.what());
            }
            if (!output.empty())
                output += '/';
            output += result + ":" + module->state();
        }
        std::cout << output << "|" << H(trace) << "\n";
        environment->handlers.clear();
        a->fresh.clear();
        a->closed.clear();
        b->fresh.clear();
        b->closed.clear();
    }
}
