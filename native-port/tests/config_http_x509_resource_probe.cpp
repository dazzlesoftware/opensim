#include "opensim/framework/config_http_x509_resource_client.hpp"
#include <iostream>
#include <sstream>
#include <thread>
using namespace opensim::framework;
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    ConfigXmlNativeHttpServices policy;
    policy.error = [](std::string, int) {
        return std::make_exception_ptr(std::runtime_error("Opaque downloader transport failure"));
    };
    ConfigHttpX509ResourceClient client(policy, std::string_view(argv[1]) == "default" ? 104857600
                                                                                       : std::stoll(argv[1]));
    std::string line;
    constexpr char hex[] = "0123456789abcdef";
    const auto limit = std::string_view(argv[1]) == "default" ? 104857600 : std::stoll(argv[1]);
    int index = 0;
    while (std::getline(std::cin, line)) {
        std::istringstream input(line);
        std::int64_t ticks;
        std::string encoded;
        int delay = 0;
        input >> ticks >> encoded;
        input >> delay;
        if (delay > 0 && limit > 0 && limit <= 2147483647)
            std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        std::u16string uri;
        if (encoded != "_" && encoded != "-")
            for (std::size_t i = 0; i < encoded.size(); i += 4)
                uri += static_cast<char16_t>(std::stoul(encoded.substr(i, 4), nullptr, 16));
        auto start = std::chrono::steady_clock::now();
        auto result = client.download_asset(
            encoded == "_" ? std::nullopt : std::optional<std::u16string_view>{uri}, ticks);
        std::cerr << index++ << ' '
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                           start)
                         .count()
                  << '\n';
        if (!result)
            std::cout << '_';
        else if (result->empty())
            std::cout << '-';
        else
            for (auto b : *result)
                std::cout << hex[b >> 4] << hex[b & 15];
        std::cout << '\n';
    }
}
