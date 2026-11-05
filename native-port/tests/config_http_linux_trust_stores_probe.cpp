#include "opensim/framework/config_http_linux_trust_stores.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <cstdlib>
using namespace opensim::framework;
void text(std::u16string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    if (value.empty())
        std::cout << '_';
    for (auto c : value)
        for (int shift = 12; shift >= 0; shift -= 4)
            std::cout << hex[(c >> shift) & 15];
}
void error(const ConfigHttpLinuxStoreError& value) {
    std::cout << "E " << value.source_type << ' ' << value.hresult << ' ';
    text(value.message);
    if (value.inner) {
        std::cout << " | ";
        error(*value.inner);
    }
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--paths") {
            auto paths = []() {
                auto home = config_http_linux_user_home_directory();
                const auto& root = config_http_linux_user_store_root();
                std::cout << "H ";
                text(utf16_replacing(home));
                std::cout << " R ";
                text(utf16_replacing(root.native()));
                std::cout << '\n';
            };
            paths();
            if (argc > 2) {
                setenv("HOME", argv[2], 1);
                paths();
            }
        } else {
            config_http_linux_check_disallowed_store(config_http_linux_user_store_root());
            std::cout << "OK\n";
        }
    } catch (const ConfigHttpLinuxStoreError& e) {
        error(e);
        std::cout << '\n';
    }
}
