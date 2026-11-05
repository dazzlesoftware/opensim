#include "opensim/services/login_agent_factory.hpp"
#include "opensim/services/agent_launch.hpp"
#include "opensim/network/circuit_transport.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Admission final : services::ISimulationLogin {
    framework::AgentCircuitManager& manager;
    int queries = 0, creates = 0;
    explicit Admission(framework::AgentCircuitManager& m) : manager(m) {
    }
    bool query_access(services::GridRegion&, const framework::Uuid&, const std::optional<std::string>&, bool,
                      const framework::Vector3&, std::vector<framework::Uuid>&,
                      services::LoginTransferContext&, std::string&) override {
        ++queries;
        return true;
    }
    bool create_agent(const services::GridRegion*, services::GridRegion&,
                      const std::shared_ptr<framework::AgentCircuitData>& circuit, std::uint32_t,
                      services::LoginTransferContext& context, std::string&) override {
        ++creates;
        auto decoded = std::make_shared<framework::AgentCircuitData>();
        decoded->unpack(network::parse_llsd_xml(network::llsd_xml(circuit->pack(&context))));
        manager.add_new_circuit(decoded->circuit_code, decoded);
        return true;
    }
};
int main() {
    try {
        services::UserAccount account;
        account.principal_id = framework::Uuid::random().str();
        account.first_name = "First";
        account.last_name = "Last";
        account.service_urls = std::make_shared<framework::AgentCircuitData::ServiceUrls>();
        int stores = 0;
        services::LoginAgentFactory factory({{"SRV_InventoryServerURI", "https://inventory"}}, std::nullopt,
                                            [&](auto&) {
                                                ++stores;
                                                return false;
                                            });
        services::LoginAgentDetails details;
        details.session = framework::Uuid::random();
        details.secure_session = framework::Uuid::random();
        details.position = {128, 128, 30};
        details.viewer = "1.0";
        details.channel = "Viewer";
        framework::AvatarAppearance avatar;
        avatar.serial = 17;
        auto baked = framework::Uuid::random();
        avatar.texture->get_face(44)->texture_id(baked);
        auto first = factory.make_agent(account, &avatar, details),
             second = factory.make_agent(account, nullptr, details);
        check(first->caps_path && second->caps_path && first->caps_path != second->caps_path,
              "Fresh caps paths on each login");
        framework::Uuid caps(*first->caps_path);
        check((caps.bytes()[6] >> 4) == 4 && (caps.bytes()[8] & 192) == 128, "Caps path is a random UUID");
        check(!first->child && first->children_cap_seeds && first->children_cap_seeds->empty(),
              "Root login circuit and fresh child seeds");
        check(first->appearance.get() != &avatar && first->appearance->serial == 17,
              "Source appearance cloned");
        check(second->appearance && second->appearance->serial == 0, "Missing appearance receives defaults");
        check(stores == 1, "Failed store return is ignored; unchanged URLs are not stored again");
        framework::AgentCircuitManager manager;
        Admission admission(manager);
        services::AgentLauncher launcher(nullptr, &admission, nullptr, nullptr, {}, [] { return 42u; });
        services::AgentLaunchRequest request;
        request.destination = std::make_shared<services::GridRegion>();
        request.where = "last";
        request.flags = 128;
        request.make_agent = [&](const auto&, std::uint32_t code) {
            details.circuit = code;
            return factory.make_agent(account, &avatar, details);
        };
        auto launched = launcher.launch(request);
        check(launched.circuit && admission.queries == 1 && admission.creates == 1,
              "Factory connected to native launch admission");
        bool admitted = false;
        network::CircuitTransport transport(
            manager, [](const auto&, auto) { return true; },
            [&](const auto& circuit, const auto&, const auto&) {
                check(circuit->appearance->texture->get_face(44)->texture_id() == baked,
                      "Baked appearance reaches transport after XML");
                check(std::any_cast<std::string>(circuit->service_urls->at("InventoryServerURI")) ==
                          "https://inventory/",
                      "Configured service URL reaches transport");
                admitted = true;
                return true;
            });
        auto use = network::lludp::make_packet("UseCircuitCode");
        use.sequence = 1;
        use.block("CircuitCode")["Code"] = std::uint64_t{42};
        use.block("CircuitCode")["ID"] = framework::Uuid(account.principal_id);
        use.block("CircuitCode")["SessionID"] = details.session;
        transport.receive({"127.0.0.1", 10000}, network::lludp::encode_datagram(use));
        check(admitted, "Factory to launch to XML to credential-gated transport");
        std::cout << "PASS: login agent factory defaults and native launch/transport handoff\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
