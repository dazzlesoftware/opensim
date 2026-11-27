#include "opensim/script/ossl_aes.hpp"
#include "opensim/network/osd_json.hpp"
#include <iostream>

int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        const auto frame = opensim::network::parse_osd_json(line);
        opensim::network::Value result;
        try {
            const auto value = frame.get("op").text == "encrypt"
                                   ? opensim::script::ossl_aes_encrypt(frame.get("secret").text, frame.get("text").text,
                                                                       frame.get("iv").text)
                                   : opensim::script::ossl_aes_decrypt(frame.get("secret").text, frame.get("text").text,
                                                                       frame.get("iv").text);
            result = opensim::network::Value::object({{"outcome", "return"}, {"result", value}});
        } catch (const std::exception &) {
            result = opensim::network::Value::object({{"outcome", "throw"}});
        }
        std::cout << opensim::network::osd_json(result, true) << '\n';
    }
}
