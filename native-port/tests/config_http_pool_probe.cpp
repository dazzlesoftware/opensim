#include "opensim/framework/config_xml_transport_http.hpp"
#include "opensim/framework/config_xml_typed_http_services.hpp"
#include "opensim/framework/config_native_uri_object.hpp"
#include "opensim/framework/config_http_cookies_native.hpp"
#include "opensim/framework/config_http_headers.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
#include <future>
#include <map>
#include <mutex>
#include <thread>
using namespace opensim::framework;
std::string hex(std::string_view bytes) {
    std::string result;
    constexpr char h[] = "0123456789abcdef";
    for (unsigned char c : bytes) {
        result += h[c >> 4];
        result += h[c & 15];
    }
    return result.empty() ? "-" : result;
}
int main(int argc, char** argv) {
    try {
        if (argc != 5)
            return 2;
        ConfigXmlNativeHttpServices policy;
        ConfigXmlHttpPoolSettings settings;
        settings.max_connections_per_server = std::stoi(argv[2]);
        settings.idle_timeout = std::chrono::milliseconds(std::stoll(argv[3]));
        settings.lifetime = std::chrono::milliseconds(std::stoll(argv[4]));
        policy.connection_pool = settings;
        policy.proxy = [](const auto&, const auto&) -> std::optional<ConfigXmlHttpRoute> { return {}; };
        policy.error = [](std::string phase, int) {
            return std::make_exception_ptr(
                ConfigXmlHttpError(phase == "timeout" ? "System.Threading.Tasks.TaskCanceledException"
                                                      : "System.Net.Http.HttpRequestException",
                                   phase, 0));
        };
        auto uris = native_config_uri_object_backend();
        ConfigXmlUriObjects objects;
        auto bound = config_xml_bind_typed_http(
            policy, objects, *uris, [] { return native_invariant_config_http_cookies_backend(); });
        auto send = config_xml_native_http_requester(std::move(bound), false);
        std::map<std::string, std::shared_ptr<ConfigXmlStream>> streams;
        std::mutex mutex;
        std::map<std::string, std::future<std::string>> jobs;
        std::string base = argv[1];
        auto open = [&](std::string id, std::string path, std::string method, int timeout,
                        std::string connection) {
            ConfigXmlHttpOperation operation;
            operation.method = method;
            operation.timeout_milliseconds = timeout;
            operation.ensure_success = false;
            operation.redirect_limit = 10;
            if (connection == "c")
                operation.headers.emplace_back("Connection", nini_writer_text(u"close"));
            int status = 0;
            std::u16string cid;
            operation.response_headers = [&](auto& head) {
                status = head.status;
                for (auto& h : head.headers)
                    if (h.name == "X-Connection-Id")
                        cid = h.value;
            };
            try {
                auto url = path.starts_with("http://") ? path : base + path;
                auto stream =
                    send(objects.wrap(uris->create(std::make_shared<const std::u16string>(utf16(url)),
                                                   ConfigUriObjectKind::Absolute)),
                         operation);
                {
                    std::lock_guard lock(mutex);
                    streams[id] = std::move(stream);
                }
                return "O " + id + " " + std::to_string(status) + " " + utf8(cid);
            } catch (const ConfigXmlHttpError& e) {
                return "E " + id + " " + e.source_type;
            } catch (const ConfigHttpHeaderError& e) {
                return "E " + id + " " + e.source_type;
            }
        };
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream input(line);
            char op;
            std::string id;
            input >> op >> id;
            try {
                if (op == 'O' || op == 'A') {
                    std::string path, method, connection;
                    int timeout;
                    input >> path >> method >> timeout >> connection;
                    if (op == 'O')
                        std::cout << open(id, path, method, timeout, connection) << '\n';
                    else
                        jobs[id] =
                            std::async(std::launch::async, open, id, path, method, timeout, connection);
                } else if (op == 'J') {
                    std::cout << jobs.at(id).get() << '\n';
                    jobs.erase(id);
                } else if (op == 'S') {
                    std::this_thread::sleep_for(std::chrono::milliseconds(std::stoll(id)));
                } else if (op == 'D') {
                    std::shared_ptr<ConfigXmlStream> stream;
                    {
                        std::lock_guard lock(mutex);
                        stream = streams.at(id);
                    }
                    stream->close();
                    std::cout << "D " << id << '\n';
                } else if (op == 'C') {
                    std::shared_ptr<ConfigXmlStream> stream;
                    {
                        std::lock_guard lock(mutex);
                        stream = std::move(streams.at(id));
                        streams.erase(id);
                    }
                    stream->close();
                    std::cout << "C " << id << '\n';
                } else if (op == 'R') {
                    int count;
                    input >> count;
                    std::shared_ptr<ConfigXmlStream> stream;
                    {
                        std::lock_guard lock(mutex);
                        stream = streams.at(id);
                    }
                    std::string result;
                    std::uint8_t buffer[4096];
                    try {
                        while (count < 0 || result.size() < static_cast<std::size_t>(count)) {
                            auto size = count < 0 ? sizeof(buffer)
                                                  : std::min(sizeof(buffer),
                                                             static_cast<std::size_t>(count) - result.size());
                            auto n = stream->read({buffer, size});
                            if (!n)
                                break;
                            result.append(reinterpret_cast<char*>(buffer), n);
                        }
                        std::cout << "R " << id << " " << hex(result) << '\n';
                    } catch (const ConfigHttpHeaderError& e) {
                        std::cout << "E " << id << " " << e.source_type << '\n';
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << line << ": " << e.what() << '\n';
                return 3;
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
