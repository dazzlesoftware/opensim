#include "opensim/network/web_http_clients.hpp"
#include "opensim/network/osd_service_http.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_http_environment_proxy.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/config_http_linux_certificate_name.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include <iomanip>
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#endif
using namespace opensim::network;
namespace f = opensim::framework;
class FixedProxy final : public f::ConfigHttpProxy {
  public:
    f::ConfigUriObjectPtr address;
    f::ConfigUriObjectPtr get_proxy(f::ConfigUriObjectPtr) const override {
        return address;
    }
    bool is_bypassed(f::ConfigUriObjectPtr) const override {
        return false;
    }
    std::shared_ptr<f::ConfigXmlCredentials> credentials() const override {
        return {};
    }
    void credentials(std::shared_ptr<f::ConfigXmlCredentials>) override {}
};
std::string hex(std::string_view bytes) {
    std::string result;
    constexpr char digits[] = "0123456789abcdef";
    for (unsigned char c : bytes) {
        result += digits[c >> 4];
        result += digits[c & 15];
    }
    return result.empty() ? "-" : result;
}
std::string message(std::u16string_view text) {
    return hex(f::utf8(text));
}
std::u16string host_decode(const std::string& value) {
    std::u16string result;
    if (value == "_")
        return result;
    for (std::size_t i = 0; i < value.size(); i += 4)
        result += static_cast<char16_t>(std::stoul(value.substr(i, 4), nullptr, 16));
    return result;
}
std::string host_encode(std::u16string_view value) {
    if (value.empty())
        return "_";
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto c : value)
        out << std::setw(4) << static_cast<unsigned>(c);
    return out.str();
}
void host_probe() {
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::istringstream in(line);
            char op;
            std::string value;
            in >> op >> value;
            if (op == 'I') {
                auto result = f::config_http_certificate_idn_ascii(host_decode(value));
                std::cout << "I " << host_encode(result) << '\n';
            } else if (op == 'N') {
                auto text = value == "-" ? std::optional<std::u16string>{}
                                         : std::optional<std::u16string>{host_decode(value)};
                auto result = f::config_http_normalize_tls_host(
                    text ? std::optional<std::u16string_view>{*text} : std::nullopt);
                std::cout << "N " << host_encode(result) << '\n';
            } else {
                std::vector<std::u16string> values{host_decode(value)};
                while (in >> value)
                    values.push_back(host_decode(value));
                auto host = f::config_http_request_host(values);
                std::cout << "H " << (host ? host_encode(*host) : "-") << ' '
                          << (host ? host_encode(f::config_http_tls_host_key(*host)) : "-") << '\n';
            }
        } catch (const f::ConfigUriComponentsError& e) {
            std::cout << "E " << e.source_type << ' ' << std::hex << e.hresult << std::dec << ' '
                      << host_encode(e.message) << '\n';
        }
    }
}
#if defined(_WIN32) || defined(__linux__) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
void certificate_probe() {
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream in(line);
        std::string der, host;
        in >> der >> host;
        std::vector<std::uint8_t> bytes;
        for (std::size_t i = 0; i < der.size(); i += 2)
            bytes.push_back(static_cast<std::uint8_t>(std::stoul(der.substr(i, 2), nullptr, 16)));
        try {
            auto normalized = f::config_http_normalize_tls_host(host_decode(host));
#ifdef _WIN32
            auto accepted = f::config_http_windows_certificate_name(bytes, normalized);
#else
            auto accepted = f::config_http_linux_certificate_name(bytes, normalized);
#endif
            std::cout << "C " << (accepted ? 1 : 0) << '\n';
        } catch (...) {
            std::cout << "C E\n";
        }
    }
}
#endif
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
void hello_probe() {
    for (std::string line; std::getline(std::cin, line);) {
        try {
            std::optional<std::u16string> host;
            if (line != "-")
                host = f::config_http_request_host({host_decode(line)});
            auto names =
                f::config_http_tls_target_names(host ? f::config_http_tls_host_key(*host) : u"127.0.0.1");
            std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_client_method()),
                                                                      SSL_CTX_free);
            if (!context)
                throw std::runtime_error("TLS context");
            SSL_CTX_set_min_proto_version(context.get(), TLS1_VERSION);
            SSL_CTX_set_max_proto_version(context.get(), TLS1_3_VERSION);
            SSL_CTX_set_verify(context.get(), SSL_VERIFY_NONE, nullptr);
            std::unique_ptr<SSL, decltype(&SSL_free)> session(SSL_new(context.get()), SSL_free);
            if (!session)
                throw std::runtime_error("TLS session");
            auto input = BIO_new(BIO_s_mem());
            auto output = BIO_new(BIO_s_mem());
            if (!input || !output) {
                BIO_free(input);
                BIO_free(output);
                throw std::runtime_error("TLS BIO");
            }
            SSL_set_bio(session.get(), input, output);
            SSL_set_connect_state(session.get());
            if (!names.sni_host.empty() &&
                !httplib::tls::set_sni(session.get(), names.sni_host.c_str(), false)) {
                if (!names.ignore_sni_error)
                    throw std::runtime_error("TLS SNI");
                ERR_clear_error();
            }
            SSL_do_handshake(session.get());
            char* data = nullptr;
            auto size = BIO_get_mem_data(output, &data);
            std::cout << "Y " << (size > 0 ? hex({data, static_cast<std::size_t>(size)}) : "-") << '\n';
            ERR_clear_error();
        } catch (...) {
            ERR_clear_error();
            std::cout << "Y -\n";
        }
    }
}
#endif
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--host-probe") {
            host_probe();
            return 0;
        }
#if defined(_WIN32) || defined(__linux__) && defined(CPPHTTPLIB_OPENSSL_SUPPORT)
        if (argc == 2 && std::string_view(argv[1]) == "--certificate-probe") {
            certificate_probe();
            return 0;
        }
#endif
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        if (argc == 2 && std::string_view(argv[1]) == "--hello-probe") {
            hello_probe();
            return 0;
        }
#endif
        if (argc != 3)
            return 2;
        std::string base = argv[1], root = argv[2];
        f::ConfigXmlNativeHttpServices policy;
        policy.default_proxy = []() -> std::shared_ptr<f::ConfigXmlProxy> {
            throw std::runtime_error("Unexpected default proxy lookup");
        };
        policy.error = [](std::string phase, int code) {
            if (phase == "tls")
                return std::make_exception_ptr(f::ConfigXmlHttpError(
                    "System.Net.Http.HttpRequestException",
                    "The SSL connection could not be established, see inner exception.", 0x80131500));
            return std::make_exception_ptr(
                std::runtime_error("UNMAPPED " + phase + ":" + std::to_string(code)));
        };
        if (root != "-")
            policy.tls_ca_file = [root](const auto&) {
                return root;
            };
        auto globals = std::make_shared<WebHttpClients>(native_web_http_handler_factory(policy));
        auto uris = f::native_config_uri_object_backend();
        std::shared_ptr<FixedProxy> proxy;
        std::map<std::string, std::shared_ptr<WebHttpClient>> clients;
        std::map<std::string, std::shared_ptr<f::ConfigXmlStream>> streams;
        std::map<std::string, std::future<std::string>> jobs;
        std::mutex mutex;
        auto open = [&](std::string id, std::shared_ptr<WebHttpClient> client, std::string path,
                        std::string method, std::string agent, std::string host) {
            try {
                f::ConfigXmlHttpOperation operation;
                operation.method = method;
                if (!agent.empty() && agent != "-")
                    operation.headers.emplace_back("User-Agent", f::nini_writer_text(f::utf16(agent)));
                if (!host.empty())
                    operation.headers.emplace_back("Host", f::nini_writer_text(host_decode(host)));
                int status = 0;
                std::string cid;
                operation.response_headers = [&](const auto& h) {
                    status = h.status;
                    for (auto& header : h.headers)
                        if (header.name == "X-Connection-Id")
                            cid = f::utf8(header.value);
                };
                auto uri = uris->create(std::make_shared<const std::u16string>(
                                            f::utf16(path.starts_with("http") ? path : base + path)),
                                        f::ConfigUriObjectKind::Absolute);
                auto stream = client->send_headers(std::move(uri), operation);
                {
                    std::lock_guard lock(mutex);
                    streams[id] = std::move(stream);
                }
                return "O " + id + " " + std::to_string(status) + " " + cid;
            } catch (const f::ConfigXmlHttpError& e) {
                return "E " + id + " " + e.source_type + " " + message(e.message);
            } catch (const f::ConfigHttpHeaderError& e) {
                return "E " + id + " " + e.source_type + " " + message(e.message);
            }
        };
        for (std::string line; std::getline(std::cin, line);) {
            std::istringstream input(line);
            char op;
            std::string id;
            input >> op >> id;
            if (op == 'P') {
                if (!proxy)
                    proxy = std::make_shared<FixedProxy>();
                proxy->address = uris->create(std::make_shared<const std::u16string>(f::utf16(id)),
                                              f::ConfigUriObjectKind::Absolute);
                std::cout << "P\n";
            } else if (op == 'S') {
                int maximum;
                input >> maximum;
                auto flags = std::stoi(id);
                globals->setup(flags & 1, flags & 2, proxy, maximum);
                std::cout << "S\n";
            } else if (op == 'G') {
                int mode, timeout, buffer;
                input >> mode >> timeout >> buffer;
                clients[id] = mode ? globals->get_no_redirect_client(timeout, buffer)
                                   : globals->get_new_global_client(timeout, buffer);
                std::cout << "G " << id << '\n';
            } else if (op == 'D') {
                clients.at(id)->close();
                std::cout << "D " << id << '\n';
            } else if (op == 'W')
                std::this_thread::sleep_for(std::chrono::milliseconds(std::stoll(id)));
            else if (op == 'O' || op == 'A') {
                std::string client, path, method, agent, host;
                input >> client >> path >> method >> agent >> host;
                if (op == 'O')
                    std::cout << open(id, clients.at(client), path, method, agent, host) << '\n';
                else
                    jobs[id] = std::async(std::launch::async, open, id, clients.at(client), path, method,
                                          agent, host);
            } else if (op == 'J') {
                std::cout << jobs.at(id).get() << '\n';
                jobs.erase(id);
            } else if (op == 'R') {
                int count;
                input >> count;
                std::shared_ptr<f::ConfigXmlStream> stream;
                {
                    std::lock_guard lock(mutex);
                    stream = streams.at(id);
                }
                std::string result;
                std::uint8_t buffer[4096];
                while (count < 0 || result.size() < static_cast<std::size_t>(count)) {
                    auto size =
                        count < 0 ? sizeof(buffer)
                                  : std::min(sizeof(buffer), static_cast<std::size_t>(count) - result.size());
                    auto n = stream->read({buffer, size});
                    if (!n)
                        break;
                    result.append(reinterpret_cast<char*>(buffer), n);
                }
                std::cout << "R " << id << " " << hex(result) << '\n';
            } else if (op == 'C') {
                std::shared_ptr<f::ConfigXmlStream> stream;
                {
                    std::lock_guard lock(mutex);
                    stream = std::move(streams.at(id));
                    streams.erase(id);
                }
                stream->close();
                std::cout << "C " << id << '\n';
            }
        }
        for (auto& [id, stream] : streams)
            stream->close();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
