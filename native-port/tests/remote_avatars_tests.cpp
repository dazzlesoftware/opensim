// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/remote_avatars.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const auto owner = framework::Uuid::random();
        services::SqliteAvatarStore store(":memory:", true);
        services::AvatarService local(store);
        services::AvatarData local_value;
        local_value.avatar_type = 1;
        local_value.data = std::make_shared<services::AvatarData::Fields>();
        (*local_value.data)["Serial"] = "999";
        require(local.set_avatar(owner, local_value), "Local control avatar creation failed");
        server::IniConfig config;
        config.set("AvatarService", "AvatarServerURI", "http://avatar.invalid/base/");
        std::vector<network::Form> calls;
        std::string result = "Success";
        bool offline = false;
        auto remote = services::remote_avatar_service(store, config, [&](const auto &url, const auto &body) {
            require(url == "http://avatar.invalid/base//avatar", "Original avatar URI concatenation changed");
            if (offline)
                throw std::runtime_error("Controlled remote outage");
            auto fields = network::parse_service_form(body);
            calls.push_back(fields);
            require(std::get<std::string>(fields.at("VERSIONMIN")) == "1" &&
                        std::get<std::string>(fields.at("VERSIONMAX")) == "1" &&
                        std::get<std::string>(fields.at("UserID")) == owner.str(),
                    "Avatar request identity/version changed");
            if (std::get<std::string>(fields.at("METHOD")) == "getavatar")
                return std::string(
                    "<ServerResponse><result type=\"List\"><AvatarType>1</AvatarType>"
                    "<Serial>7</Serial><VisualParams>1,2,3</VisualParams><AvatarHeight>1.9</AvatarHeight>"
                    "</result></ServerResponse>");
            return network::service_xml(network::ServiceXmlValue::object({{"result", result}}));
        });
        auto avatar = remote->get_avatar(owner);
        require(avatar.avatar_type == 1 && avatar.data->at("Serial") == "7", "Remote avatar lookup failed");
        auto appearance = remote->get_appearance(owner);
        require(appearance && appearance->serial == 7, "Remote appearance conversion failed");
        (*avatar.data)["Serial"] = "8";
        require(remote->set_avatar(owner, avatar) && std::get<std::string>(calls.back().at("Serial")) == "8",
                "Remote avatar save failed");
        require(remote->set_appearance(owner, appearance.get()), "Remote appearance save failed");
        require(std::get<std::string>(calls.back().at("METHOD")) == "setavatar",
                "Appearance save did not use original setavatar operation");
        services::AvatarService::Strings names{"Name & one", "other"}, values{"Value = two", std::nullopt};
        require(remote->set_items(owner, &names, &values), "Remote item updates failed");
        require(std::get<std::vector<std::string>>(calls.back().at("Names")) ==
                        std::vector<std::string>{"Name & one", "other"} &&
                    std::get<std::vector<std::string>>(calls.back().at("Values")) ==
                        std::vector<std::string>{"Value = two", ""},
                "Avatar array encoding changed");
        require(remote->remove_items(owner, &names) && remote->reset(owner), "Remote remove/reset failed");
        result = "True";
        require(!remote->reset(owner), "Avatar connector accepted boolean instead of Success");
        result = " Success ";
        require(!remote->set_avatar(owner, avatar), "Avatar connector trimmed an original exact result");
        offline = true;
        require(!remote->get_avatar(owner).data && !remote->set_avatar(owner, avatar) && !remote->reset(owner),
                "Remote avatar failure used local fallback");
        require(local.get_avatar(owner).data->at("Serial") == "999", "Remote operation mutated local avatar rows");
        std::cout << "PASS: remote avatar lookup, appearance conversion, writes, arrays and no local fallback\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
