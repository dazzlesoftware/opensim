#include "opensim/network/web_http_clients.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <map>
#include <algorithm>
using namespace opensim::network;
namespace f = opensim::framework;
struct Proxy : f::ConfigXmlProxy {
    int id;
    explicit Proxy(int value) : id(value) {
    }
};
std::string hex(std::u16string_view text) {
    if (text.empty())
        return "-";
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : text)
        out << std::setw(4) << unsigned(c);
    return out.str();
}
int main() {
    WebHttpClients clients(native_web_http_handler_factory({}));
    std::map<int, std::shared_ptr<Proxy>> proxies;
    std::vector<std::shared_ptr<WebHttpHandler>> identities;
    std::map<int, std::shared_ptr<WebHttpClient>> wrappers;
    int next_client = 0;
    auto id = [&](std::shared_ptr<WebHttpHandler> handler) {
        if (!handler)
            return 0;
        auto at = std::find(identities.begin(), identities.end(), handler);
        if (at == identities.end()) {
            identities.push_back(handler);
            return int(identities.size());
        }
        return int(at - identities.begin() + 1);
    };
    auto snapshot = [&](std::shared_ptr<WebHttpHandler> handler) {
        if (!handler) {
            std::cout << "-";
            return;
        }
        auto& s = handler->settings();
        std::cout << id(handler) << '/' << s.allow_auto_redirect << '/' << s.max_automatic_redirections << '/'
                  << s.automatic_decompression << '/' << s.pool.connect_timeout.count() << '/'
                  << s.pre_authenticate << '/' << s.use_cookies << '/' << s.pool.max_connections_per_server
                  << '/' << s.pool.idle_timeout.count() << '/' << s.pool.lifetime.count() << '/'
                  << s.enabled_ssl_protocols << '/' << s.certificate_revocation_mode << '/' << s.use_proxy
                  << '/' << (s.proxy ? std::dynamic_pointer_cast<Proxy>(s.proxy)->id : 0);
    };
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream input(line);
        char op;
        input >> op;
        try {
            if (op == 'S') {
                int flags, proxy, maximum;
                input >> flags >> proxy >> maximum;
                if (proxy && !proxies.contains(proxy))
                    proxies[proxy] = std::make_shared<Proxy>(proxy);
                clients.setup(flags & 1, flags & 2, proxy ? proxies[proxy] : nullptr, maximum);
                std::cout << "OK";
            } else if (op == 'G') {
                int kind, timeout, maximum;
                input >> kind >> timeout >> maximum;
                auto client = kind ? clients.get_no_redirect_client(timeout, maximum)
                                   : clients.get_new_global_client(timeout, maximum);
                wrappers[++next_client] = client;
                std::cout << "G:" << next_client << ':' << id(client->handler()) << ':'
                          << client->timeout_milliseconds() << ':' << client->maximum_response_buffer_size()
                          << ':' << client->expect_continue();
            } else if (op == 'D') {
                int client;
                input >> client;
                wrappers.at(client)->close();
                std::cout << "D:" << client;
            } else if (op == 'U') {
                int client;
                input >> client;
                auto uris = f::native_config_uri_object_backend();
                auto uri = uris->create(std::make_shared<const std::u16string>(u"http://127.0.0.1:9/"),
                                        f::ConfigUriObjectKind::Absolute);
                wrappers.at(client)->send_headers(std::move(uri), {});
                std::cout << "UNEXPECTED_SEND";
            } else if (op == 'P') {
                std::int32_t flags;
                input >> flags;
                auto handler = clients.shared_handler.load(), no = clients.shared_no_redirect_handler.load();
                std::cout << "P:"
                          << (handler ? (handler->settings().accepts_certificate_errors(flags) ? "1" : "0")
                                      : "-")
                          << ':'
                          << (no ? (no->settings().accepts_certificate_errors(flags) ? "1" : "0") : "-");
            }
        } catch (const f::ConfigXmlHttpError& e) {
            std::cout << "E:" << e.source_type << ':' << std::hex << e.hresult << std::dec << ':'
                      << hex(e.message);
        } catch (const std::exception& e) {
            std::cerr << line << ":" << e.what() << '\n';
            return 1;
        }
        std::cout << "|";
        snapshot(clients.shared_no_redirect_handler.load());
        std::cout << "|";
        snapshot(clients.shared_handler.load());
        std::cout << '\n';
    }
}
