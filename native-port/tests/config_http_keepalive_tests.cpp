#include "opensim/framework/config_http_headers.hpp"
#include <iostream>
#include <sstream>
using namespace opensim::framework;
int main(int argc, char**) {
    if (argc == 1) {
        ConfigHttpKeepAliveState state;
        config_http_apply_keep_alive(state, u"timeout=20, max=10");
        if (state.close || state.timeout_seconds != 19)
            return 1;
        config_http_apply_keep_alive(state, u"max=0");
        return !state.close;
    }
    for (std::string line; std::getline(std::cin, line);) {
        ConfigHttpKeepAliveState state;
        std::istringstream fields(line);
        std::string field;
        while (std::getline(fields, field, '|')) {
            std::u16string value;
            if (field != "-")
                for (std::size_t i = 0; i < field.size(); i += 4)
                    value += static_cast<char16_t>(std::stoul(field.substr(i, 4), nullptr, 16));
            config_http_apply_keep_alive(state, value);
        }
        std::cout << (state.close ? 1 : 0) << ':' << state.timeout_seconds << '\n';
    }
}
