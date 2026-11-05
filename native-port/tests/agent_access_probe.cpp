#include "opensim/server/agent_access.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/vector_text.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <sstream>
using namespace opensim;
std::string hex(const std::string& s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string decode(const std::string& s) {
    std::string out;
    if (s == ".")
        return out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16));
    return out;
}
std::string bits(float f) {
    if (std::isnan(f))
        return "N";
    std::ostringstream out;
    out << std::hex << std::bit_cast<std::uint32_t>(f);
    return out.str();
}
framework::Uuid id(int n) {
    return framework::Uuid(n == 1   ? "00000000-0000-0000-0000-000000000001"
                           : n == 2 ? "00000000-0000-0000-0000-000000000002"
                                    : "00000000-0000-0000-0000-000000000009");
}
struct Service : services::ISimulationLogin {
    int profile;
    std::string trace;
    explicit Service(int p) : profile(p) {
    }
    bool query_access(services::GridRegion& d, const framework::Uuid& a, const std::optional<std::string>& h,
                      bool t, const framework::Vector3& p, std::vector<framework::Uuid>& f,
                      framework::EntityTransferContext& c, std::string& r) override {
        trace = d.id.str() + ":" + a.str() + ":" + (h ? hex(*h) : "-") + ":" + (t ? "1" : "0") + ":" +
                bits(p.x) + "," + bits(p.y) + "," + bits(p.z) + ":" + bits(c.inbound_version) + "," +
                bits(c.outbound_version) + "," + std::to_string(c.wearables_count) + ":";
        for (auto v : f)
            trace += v.str() + ",";
        if (profile & 2)
            throw std::runtime_error("service");
        if (profile & 4) {
            if (!f.empty())
                f.erase(f.begin());
            f.push_back(id(9));
            c.inbound_version = 12;
            c.outbound_version = 13;
            c.wearables_count = 99;
        }
        r = profile & 1 ? "denied" : "";
        return !(profile & 1);
    }
    bool create_agent(const services::GridRegion*, services::GridRegion&,
                      const std::shared_ptr<framework::AgentCircuitData>&, std::uint32_t,
                      framework::EntityTransferContext&, std::string&) override {
        throw std::runtime_error("unused");
    }
};
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string mode, text;
        int profile;
        in >> mode;
        if (mode == "P") {
            in >> text;
            server::AgentPathParameters path;
            bool ok = server::agent_path_parameters(decode(text), path);
            std::cout << ok << "|" << path.agent.str() << "|" << path.region.str() << "|" << hex(path.action)
                      << "\n";
            continue;
        }
        if (mode == "V") {
            in >> text;
            try {
                auto v = framework::parse_sdk_vector3(decode(text));
                std::cout << bits(v.x) << "," << bits(v.y) << "," << bits(v.z) << "\n";
            } catch (const std::exception&) {
                std::cout << "E\n";
            }
            continue;
        }
        in >> profile >> text;
        Service service(profile);
        server::AgentAccessHandler handler(profile & 8 ? nullptr : &service);
        network::HttpResponse response;
        response.status = 211;
        response.keep_alive = true;
        response.body = "before";
        bool failed = false;
        try {
            handler.query_access(network::parse_osd_json(decode(text)), response, id(1), id(2));
        } catch (const std::exception&) {
            failed = true;
        }
        std::cout << failed << "|" << response.status << "|" << response.keep_alive << "|"
                  << hex(service.trace) << "|" << hex(response.body) << "\n";
    }
}
