#include "opensim/services/account_service_urls.hpp"
#include "opensim/network/service_protocol.hpp"
#include "osd_fixture_value.hpp"
#include <iostream>
using namespace opensim;
using Urls = framework::AgentCircuitData::ServiceUrls;
std::string hx(const std::string& s) {
    constexpr char a[] = "0123456789abcdef";
    std::string out;
    for (unsigned char c : s) {
        out += a[c >> 4];
        out += a[c & 15];
    }
    return out;
}
std::string snapshot(const std::shared_ptr<Urls>& urls) {
    if (!urls)
        return "null";
    std::string s;
    for (auto& [key, value] : *urls) {
        s += hx(key) + ":";
        if (auto p = std::any_cast<std::string>(&value))
            s += "s" + hx(*p);
        else if (!value.has_value())
            s += "null";
        else
            s += "object";
        s += ';';
    }
    return s;
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op, arg;
        in >> op >> arg;
        std::string status = "OK", value = "-";
        std::shared_ptr<Urls> urls;
        try {
            if (op == "codec") {
                auto raw = arg == "." ? "" : fixture::unhex(arg);
                value = hx(network::service_url_encode(raw)) + "/" + hx(network::service_url_decode(raw));
            } else if (op == "decode") {
                urls = services::decode_account_service_urls(arg == "." ? "" : fixture::unhex(arg));
                value = hx(services::encode_account_service_urls(urls));
            } else if (op == "objects") {
                int mode = std::stoi(arg);
                if (mode != 0)
                    urls = std::make_shared<Urls>();
                if (mode == 1)
                    (*urls)["null"] = std::any{};
                if (mode == 2) {
                    (*urls)["z &=試験"] = std::string("https://host/a?x=1&y=two words");
                    (*urls)[""] = std::string{};
                }
                if (mode == 3) {
                    (*urls)["integer"] = 7;
                    (*urls)["bool"] = true;
                    (*urls)["double"] = 1e22;
                    auto count = std::make_shared<int>(0);
                    (*urls)["counter"] =
                        framework::ServiceUrlStringifiable{[count] { return std::to_string(++*count); }};
                }
                if (mode == 4) {
                    (*urls)["before"] = std::string("first");
                    std::weak_ptr<Urls> weak = urls;
                    (*urls)["remove"] = framework::ServiceUrlStringifiable{[weak] {
                        weak.lock()->erase("before");
                        return "middle";
                    }};
                    (*urls)["after"] = std::string("last");
                }
                if (mode == 5) {
                    std::weak_ptr<Urls> weak = urls;
                    (*urls)["add"] = framework::ServiceUrlStringifiable{[weak] {
                        (*weak.lock())["added"] = std::string("value");
                        return "value";
                    }};
                }
                if (mode == 6) {
                    (*urls)["change"] = framework::ServiceUrlStringifiable{[&urls] {
                        urls = std::make_shared<Urls>();
                        (*urls)["new"] = std::string("value");
                        return "value";
                    }};
                    (*urls)["after"] = std::string("value");
                }
                value = hx(services::encode_account_service_urls(urls));
            }
        } catch (const std::exception&) {
            status = "EX";
        }
        std::cout << status << '|' << value << '|' << snapshot(urls) << '\n';
    }
}
