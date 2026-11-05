// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/basic_profile.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <type_traits>
using namespace opensim;
using namespace framework;
using namespace scene;
std::string H(std::string_view s) {
    std::string r;
    for (unsigned char c : s) {
        r += "0123456789abcdef"[c >> 4];
        r += "0123456789abcdef"[c & 15];
    }
    return r;
}
std::string H(const ProfileText& s) {
    return s ? H(utf8(*s)) : "N";
}
std::string H(const std::vector<std::uint8_t>& data) {
    return H(std::string_view(reinterpret_cast<const char*>(data.data()), data.size()));
}
ProfileText Text(const std::string& value) {
    if (value == "-")
        return {};
    if (value == ".")
        return u"";
    std::u16string s;
    for (std::size_t i = 0; i < value.size(); i += 4) {
        auto low = std::stoi(value.substr(i, 2), nullptr, 16),
             high = std::stoi(value.substr(i + 2, 2), nullptr, 16);
        s += static_cast<char16_t>(low | (high << 8));
    }
    return s;
}
std::string trace;
int mode, flags;
std::int32_t created;
ProfileText first, last;
struct Accounts : BasicProfileAccounts {
    std::string name;
    explicit Accounts(std::string n) : name(std::move(n)) {
    }
    std::shared_ptr<BasicProfileAccount> get(Uuid scope, Uuid avatar) override {
        trace += "get" + name + ":" + scope.str() + ":" + avatar.str() + ";";
        if (mode == 2)
            throw std::runtime_error("account failed");
        if (mode == 0)
            return {};
        auto account = std::make_shared<BasicProfileAccount>();
        account->first = first;
        account->last = last;
        account->created = created;
        return account;
    }
};
struct Scene : BasicProfileScene {
    std::string name;
    Uuid id;
    std::vector<std::function<void(std::shared_ptr<BasicProfileClient>)>> handlers;
    explicit Scene(std::string n)
        : name(std::move(n)),
          id(name == "A" ? "00000000-0000-0000-0000-000000000001" : "00000000-0000-0000-0000-000000000002") {
    }
    std::shared_ptr<BasicProfileAccounts> accounts() override {
        if (name == "A" && (flags & 8))
            return {};
        return std::make_shared<Accounts>(name);
    }
    Uuid scope() override {
        if (name == "A" && (flags & 16))
            throw std::runtime_error("Object reference not set to an instance of an object.");
        return id;
    }
    void on_new_client(std::function<void(std::shared_ptr<BasicProfileClient>)> handler) override {
        trace += "subscribe" + name + ";";
        if (flags & 256)
            throw std::runtime_error("subscribe failed");
        handlers.push_back(std::move(handler));
    }
    void register_profile(std::shared_ptr<ProfileModule>) override {
        trace += "register" + name + ";";
        if (flags & 128)
            throw std::runtime_error("register failed");
    }
    void fire(std::shared_ptr<BasicProfileClient> client) {
        auto list = handlers;
        for (auto& handler : list)
            handler(client);
    }
};
struct Other : ProfileSceneBase {};
struct Client : BasicProfileClient, std::enable_shared_from_this<Client> {
    std::shared_ptr<Scene> concrete;
    std::vector<std::function<void(std::shared_ptr<BasicProfileClient>, Uuid)>> handlers;
    std::shared_ptr<ProfileSceneBase> scene() override {
        if (flags & 2)
            return std::make_shared<Other>();
        if (flags & 4)
            return {};
        return concrete;
    }
    void
    on_properties_request(std::function<void(std::shared_ptr<BasicProfileClient>, Uuid)> handler) override {
        trace += "request-sub;";
        handlers.push_back(std::move(handler));
    }
    void request(Uuid avatar) {
        auto list = handlers;
        for (auto& handler : list)
            handler(shared_from_this(), avatar);
    }
    void send_properties(const AvatarProfileProperties& p) override {
        trace += "P:" + p.avatar.str() + ":" + H(p.about) + ":" + H(p.born) + ":" + H(*p.membership) + ":" +
                 H(p.first_life_about) + ":" + std::to_string(p.flags) + ":" + p.first_life_image.str() +
                 ":" + p.image.str() + ":" + H(p.url) + ":" + p.partner.str() + ";";
        if (flags & 32)
            throw std::runtime_error("properties failed");
    }
    void send_interests(const AvatarProfileInterests& p) override {
        trace += "I:" + p.avatar.str() + ":" + std::to_string(p.wants) + ":" + H(p.want_text) + ":" +
                 std::to_string(p.skills) + ":" + H(p.skill_text) + ":" + H(p.languages) + ";";
        if (flags & 64)
            throw std::runtime_error("interests failed");
    }
};
namespace opensim::scene {
struct BasicProfileProbe {
    static std::string state(const BasicProfileModule& module, const std::shared_ptr<Scene>& a,
                             const std::shared_ptr<Scene>& b) {
        std::lock_guard lock(module.mutex_);
        std::string out = module.enabled_ ? "1" : "0";
        out += ':';
        for (auto& scene : module.scenes_)
            out += scene == a ? "A" : scene == b ? "B" : "N";
        return out;
    }
};
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string raw;
        in >> raw;
        if (raw == "B") {
            in >> raw;
            try {
                auto text = Text(raw);
                std::cout << H(profile_string_bytes(text ? std::u16string_view(*text)
                                                         : std::u16string_view{}))
                          << "\n";
            } catch (const std::exception& e) {
                std::cout << "EX:" << H(e.what()) << "\n";
            }
            continue;
        }
        mode = std::stoi(raw);
        std::string f, l;
        in >> flags >> created >> f >> l;
        first = Text(f);
        last = Text(l);
        trace.clear();
        auto module =
            std::make_shared<BasicProfileModule>([](auto text) { trace += "log:" + utf8(text) + ";"; });
        auto a = std::make_shared<Scene>("A"), b = std::make_shared<Scene>("B");
        auto client = std::make_shared<Client>();
        client->concrete = b;
        std::string output;
        Uuid avatar("00000000-0000-0000-0000-000000000003");
        for (std::string token; in >> token;) {
            if (!output.empty())
                output += '/';
            try {
                if (token == "I" || token == "J") {
                    auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
                    auto config = arena->create_ini_source();
                    if (token == "J")
                        config->add_config(nini_writer_text(u"UserProfiles"));
                    module->initialise(config);
                } else if (token == "Z")
                    module->initialise(nullptr);
                else if (token == "A")
                    module->add_region(a);
                else if (token == "B")
                    module->add_region(b);
                else if (token == "N")
                    module->add_region(nullptr);
                else if (token == "R")
                    module->remove_region(a);
                else if (token == "T")
                    module->remove_region(b);
                else if (token == "U")
                    module->remove_region(nullptr);
                else if (token == "C")
                    module->close();
                else if (token == "P")
                    module->post_initialise();
                else if (token == "L")
                    module->region_loaded(nullptr);
                else if (token == "E")
                    a->fire(flags & 1 ? nullptr : client);
                else if (token == "F")
                    b->fire(client);
                else if (token == "Q")
                    module->request_properties(flags & 1 ? nullptr : client, avatar);
                else if (token == "V")
                    client->request(avatar);
                output += "OK";
            } catch (const std::exception& e) {
                output += "EX:" + H(e.what());
            }
            output += ":" + BasicProfileProbe::state(*module, a, b);
        }
        std::cout << output << "|" << H(trace) << "|" << utf8(BasicProfileModule::name) << ":"
                  << (std::is_base_of_v<ProfileModule, BasicProfileModule> ? "I" : "X") << "\n";
        a->handlers.clear();
        b->handlers.clear();
        client->handlers.clear();
    }
}
