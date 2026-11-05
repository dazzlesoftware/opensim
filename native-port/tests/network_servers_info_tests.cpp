#include "opensim/framework/network_servers_info.hpp"
#include "opensim/framework/configuration_loader.hpp"
#include "opensim/framework/nini_config_invariant.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
namespace {
using T = NiniWriterText;
T t(std::u16string s) {
    return nini_writer_text(std::move(s));
}
T dec(const std::string& s) {
    if (s == "null")
        return {};
    std::u16string x;
    if (s != "-")
        for (std::size_t i = 0; i < s.size(); i += 4)
            x += char16_t(std::stoul(s.substr(i, 4), nullptr, 16));
    return t(x);
}
std::string enc(T s) {
    if (!s)
        return "null";
    if (s->empty())
        return "-";
    std::string r;
    const char* h = "0123456789abcdef";
    for (auto c : *s)
        for (int n = 12; n >= 0; n -= 4)
            r += h[(c >> n) & 15];
    return r;
}
struct Row {
    std::shared_ptr<NetworkServersInfo> info;
    std::shared_ptr<NiniConfigArena> arena =
        std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
    std::shared_ptr<NiniConfigSourceBase> source;
    std::vector<std::string> trace;
    int fault = 0, mutation = 0, mutation_at = 0, calls = 0;
    Row(int seed) {
        info = seed < 0 ? std::make_shared<NetworkServersInfo>(0xffffffffu, 0)
                        : std::make_shared<NetworkServersInfo>();
        if (seed <= 0)
            return;
        auto& n = *info;
        n.HttpListenerPort = 0u - std::uint32_t(seed);
        n.secureInventoryServer = (seed & 1) != 0;
        n.isSandbox = (seed & 2) != 0;
        n.HttpUsesSSL = (seed & 4) != 0;
        n.HttpSSLCN = seed % 3 == 0 ? nullptr : t(u"old-cn");
        n.HttpSSLCertPath = seed % 2 == 0 ? nullptr : t(std::u16string(u"old-path") + char16_t(0xd800));
        n.HttpSSLCNCertPass = seed % 3 == 0 ? nullptr : t(u"old-pass\ufffe");
        n.httpSSLPort = std::uint32_t(seed) * 123457u;
        n.ssl_listener = (seed & 1) != 0;
        n.ssl_external = (seed & 2) != 0;
        n.https_port = 0xffffffffu - std::uint32_t(seed);
        n.cert_path = seed % 3 == 0 ? nullptr : t(u"cert-seed");
        n.cert_pass = seed % 2 == 0 ? nullptr : t(u"pass-seed");
    }
    void hit(const char* kind, T key, std::string fallback) {
        trace.push_back(std::string(kind) + ":" + enc(key) + ":" + fallback);
        ++calls;
        if (calls == mutation_at) {
            auto& n = *info;
            switch (mutation) {
            case 1:
                n.ssl_listener = true;
                break;
            case 2:
                n.ssl_listener = false;
                break;
            case 3:
                n.HttpSSLCertPath = t(u"mutated-path");
                n.HttpSSLCNCertPass = t(u"mutated-pass");
                n.cert_path = t(u"mutated-cert");
                n.cert_pass = t(u"mutated-certpass");
                break;
            case 4:
                n.HttpSSLCertPath = nullptr;
                n.HttpSSLCNCertPass = nullptr;
                n.cert_path = nullptr;
                n.cert_pass = nullptr;
                break;
            case 5:
                source->configs()->clear();
                source->add_config(t(u"Network"))->set_text(t(u"http_listener_sslport"), t(u"31337"));
                break;
            case 7:
                info->load_from_configuration(source);
                break;
            case 6:
                n.HttpListenerPort = 0xffffffffu;
                n.httpSSLPort = 0xffffffffu;
                n.HttpUsesSSL = true;
                n.ssl_external = true;
                n.https_port = 0xffffffffu;
                break;
            }
        }
        if (calls == fault)
            throw NiniConfigError(NiniConfigError::Kind::InvalidOperation, u"getter fault");
    }
    void load(int mode, T ini) {
        source = arena->create_ini_source(nini_string_reader(*ini));
        if (mode == 0) {
            info->load_from_configuration(NetworkServersConfigLookup([this](T name) {
                trace.push_back("L:" + enc(name));
                if (fault == -1)
                    throw NiniConfigError(NiniConfigError::Kind::InvalidOperation, u"lookup fault");
                auto c = source->configs()->at(name);
                if (!c)
                    return std::shared_ptr<NetworkServersConfigAccess>();
                auto a = std::make_shared<NetworkServersConfigAccess>();
                a->integer = [this, c](T k, int f) {
                    hit("I", k, std::to_string(f));
                    return c->get_int(k, f);
                };
                a->boolean = [this, c](T k, bool f) {
                    hit("B", k, f ? "1" : "0");
                    return c->get_boolean(k, f);
                };
                a->text = [this, c](T k, T f) {
                    hit("T", k, enc(f));
                    if (mutation == 8 && calls == mutation_at)
                        return T{};
                    return c->get_string(k, f);
                };
                return a;
            }));
        } else if (mode == 3)
            info->load_from_configuration(std::shared_ptr<NiniConfigSourceBase>());
        else if (mode == 2 || mode == 5) {
            RegionConfigurationBackend b;
            bind_network_servers_info(b);
            if (mode == 5) {
                trace.push_back("bind-load");
                b.load_network_info({}, source);
            } else {
                trace.push_back("bind-create");
                auto object = b.create_network_info();
                auto slot = std::static_pointer_cast<NetworkServersInfo>(object);
                *slot = *info;
                info = slot;
                trace.push_back("bind-load");
                b.load_network_info(object, source);
            }
        } else
            info->load_from_configuration(source);
    }
    std::string snapshot() {
        auto& n = *info;
        ConfigSettings c;
        std::ostringstream s;
        s << n.HttpListenerPort << ',' << n.secureInventoryServer << ',' << n.isSandbox << ','
          << n.HttpUsesSSL << ',' << enc(n.HttpSSLCN) << ',' << enc(n.HttpSSLCertPath) << ','
          << enc(n.HttpSSLCNCertPass) << ',' << n.httpSSLPort << ',' << n.ssl_listener << ','
          << n.ssl_external << ',' << n.https_port << ',' << enc(n.cert_path) << ',' << enc(n.cert_pass)
          << ',' << enc(c.PhysicsEngine) << ',' << enc(c.MeshEngineName) << ',' << enc(c.ClientstackDll)
          << ',' << enc(c.LibrariesXMLFile) << ',' << c.DefaultRegionHttpPort;
        return s.str();
    }
};
std::string error(const char* type, T msg, std::uint32_t hr, T param = {}) {
    return std::string(type) + ":" + enc(msg) + ":" + std::to_string(hr) + ":" + enc(param);
}
}
int main(int argc, char**) {
    if (argc == 1) {
        NetworkServersInfo a, b(0xffffffffu, 0xffffffffu);
        if (a.HttpListenerPort != 9000 || a.httpSSLPort != 9001 ||
            a.cert_path != network_servers_empty_text() || a.HttpSSLCN != network_servers_empty_text() ||
            a.cert_path != b.cert_path || a.HttpSSLCertPath != b.HttpSSLCertPath || b.https_port != 0)
            return 1;
        RegionConfigurationBackend backend;
        bind_network_servers_info(backend);
        auto object = backend.create_network_info();
        if (std::static_pointer_cast<NetworkServersInfo>(object)->HttpListenerPort != 9000)
            return 1;
        auto arena = std::make_shared<NiniConfigArena>(native_nini_invariant_config_backend());
        std::weak_ptr<NiniConfigArena> lifetime = arena;
        auto source =
            arena->create_ini_source(nini_string_reader(u"[Network]\nhttp_listener_cn=retained-value\n"));
        a.load_from_configuration(source);
        source.reset();
        arena.reset();
        if (!lifetime.expired() || !a.HttpSSLCN || *a.HttpSSLCN != u"retained-value")
            return 1;
        std::cout
            << "PASS: NetworkServersInfo constructors, literal identity, source/value ownership and loader callbacks\n";
        return 0;
    }
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, seed, fault, mutation, at;
        std::string doc;
        in >> mode >> seed >> fault >> mutation >> at >> doc;
        Row r(seed);
        r.fault = fault;
        r.mutation = mutation;
        r.mutation_at = at;
        std::string status = "ok";
        try {
            r.load(mode, dec(doc));
        } catch (const NiniConfigError& e) {
            status =
                error(e.source_type(), t(e.message), e.hresult(), e.parameter ? t(*e.parameter) : nullptr);
        } catch (const NiniReaderError& e) {
            status = error(e.source_type(), t(e.message_utf16()), e.hresult());
        } catch (const NiniDocumentError& e) {
            status =
                error(e.source_type(), t(e.message), e.hresult(), e.parameter ? t(*e.parameter) : nullptr);
        }
        std::cout << status << '/' << r.snapshot() << '/';
        for (std::size_t i = 0; i < r.trace.size(); ++i) {
            if (i)
                std::cout << ';';
            std::cout << r.trace[i];
        }
        std::cout << '\n';
    }
    return 0;
}
