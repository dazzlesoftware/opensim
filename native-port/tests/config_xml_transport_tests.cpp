#include "opensim/framework/config_xml_transport.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
namespace {
int mask;
std::string trace;
void step(int bit, std::string name) {
    if (!trace.empty())
        trace += ',';
    trace += name;
    if (mask & (1 << bit))
        throw std::runtime_error(name);
}
struct Body : ConfigXmlStream {
    std::int32_t read(std::span<std::uint8_t>) override {
        return 0;
    }
    std::vector<std::uint8_t> copy_to_memory() override {
        step(5, "copy");
        return {0, 255, 13, 10, 65};
    }
    void close() override {
        step(6, "closestream");
    }
};
struct Handler : ConfigXmlHttpHandler {
    Handler() {
        step(0, "handler");
    }
    void credentials(std::shared_ptr<ConfigXmlCredentials>) override {
        step(2, "credentials");
    }
    void proxy(std::shared_ptr<ConfigXmlProxy>) override {
        step(3, "proxy");
    }
};
struct Client : ConfigXmlHttpClient {
    Client() {
        step(1, "client");
    }
    std::shared_ptr<ConfigXmlStream> get_stream(std::shared_ptr<ConfigXmlUri>) override {
        step(4, "get");
        return std::make_shared<Body>();
    }
    void close() override {
        step(7, "closeclient");
    }
};
struct Credentials : ConfigXmlCredentials {};
struct Proxy : ConfigXmlProxy {};
}
int main(int argc, char**) {
    if (argc == 1) {
        auto stream = config_xml_memory_stream({0, 255});
        std::uint8_t bytes[3];
        if (stream->read(bytes) != 2 || bytes[0] != 0 || bytes[1] != 255 || stream->read(bytes) != 0)
            return 1;
        return 0;
    }
    int c, p;
    while (std::cin >> mask >> c >> p) {
        trace.clear();
        ConfigXmlDownloadBackend backend;
        backend.new_handler = []() { return std::make_shared<Handler>(); };
        backend.new_client = [](auto) { return std::make_shared<Client>(); };
        auto uri = std::make_shared<ConfigXmlUri>();
        uri->scheme = []() { return nini_writer_text(u"http"); };
        std::string output;
        try {
            auto stream = config_xml_download(uri, backend, c ? std::make_shared<Credentials>() : nullptr,
                                              p ? std::make_shared<Proxy>() : nullptr);
            auto bytes = stream->copy_to_memory();
            static constexpr char h[] = "0123456789abcdef";
            for (auto b : bytes) {
                output += h[b >> 4];
                output += h[b & 15];
            }
        } catch (const std::exception& e) {
            output = "System.InvalidOperationException:" + std::string(e.what());
        }
        std::cout << output << '/' << trace << '\n';
    }
}
