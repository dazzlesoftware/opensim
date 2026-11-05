#include "opensim/framework/config_http_linux_user_store.hpp"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    opensim::framework::ConfigHttpLinuxUserStore store(argv[1]);
    constexpr char hex[] = "0123456789abcdef";
    for (const auto& der : *store.collection()) {
        for (auto c : der)
            std::cout << hex[c >> 4] << hex[c & 15];
        std::cout << '\n';
    }
}
