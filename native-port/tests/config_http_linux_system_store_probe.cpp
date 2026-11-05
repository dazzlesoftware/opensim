#include "opensim/framework/config_http_linux_system_store.hpp"
#include <iostream>
int main(int argc, char**) {
    if (argc > 1) {
        auto now = std::chrono::steady_clock::time_point{};
        opensim::framework::ConfigHttpLinuxSystemStore store([&] { return now; });
        opensim::framework::ConfigHttpLinuxSystemStore::Snapshot previous;
        long long milliseconds;
        constexpr char hex[] = "0123456789abcdef";
        while (std::cin >> milliseconds) {
            now = std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(milliseconds);
            auto snapshot = store.collection();
            std::cout << (snapshot == previous ? 'S' : 'N');
            for (int root = 1; root >= 0; --root)
                for (const auto& der : root ? snapshot->roots : snapshot->intermediates) {
                    std::cout << ' ' << (root ? 'r' : 'i');
                    for (auto c : der)
                        std::cout << hex[c >> 4] << hex[c & 15];
                }
            std::cout << std::endl;
            previous = std::move(snapshot);
        }
        return 0;
    }
    opensim::framework::ConfigHttpLinuxSystemStore store;
    constexpr char hex[] = "0123456789abcdef";
    auto snapshot = store.collection();
    for (int root = 1; root >= 0; --root)
        for (const auto& der : root ? snapshot->roots : snapshot->intermediates) {
            std::cout << (root ? 'r' : 'i') << ' ';
            for (auto c : der)
                std::cout << hex[c >> 4] << hex[c & 15];
            std::cout << '\n';
        }
}
