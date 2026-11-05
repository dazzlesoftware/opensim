#include "opensim/server/json_rpc.hpp"
#include "opensim/network/osd_json.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using namespace server;
using namespace network;
std::string hex(std::string_view s) {
    std::string r;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        r += h[c >> 4];
        r += h[c & 15];
    }
    return r;
}
std::string unhex(std::string_view s) {
    std::string r;
    if (s == "-")
        return r;
    for (std::size_t i = 0; i < s.size(); i += 2)
        r += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return r;
}
int main() {
    std::string row;
    while (std::getline(std::cin, row)) {
        std::istringstream in(row);
        std::string op, raw;
        int profile, mode;
        in >> op >> profile >> mode >> raw;
        std::string log, disposed, output;
        try {
            if (op == "S") {
                JsonRpcResponse r;
                if (profile & 1)
                    r.id(u"id");
                if (profile & 2)
                    r.version(u"2.0");
                if (profile & 4)
                    r.result =
                        std::make_shared<Value>(Value::object({{"zero", Value(0)}, {"answer", Value(42)}}));
                if (profile & 8) {
                    r.error->code(-3);
                    r.error->message(u"bad");
                }
                r.error->data = std::make_shared<Value>("unused");
                output = hex(r.serialize());
                if (mode & 1)
                    r.result.reset();
                if (mode & 2)
                    r.error->code(0);
                if (mode & 4)
                    r.error = std::make_shared<JsonRpcError>();
                if (mode & 8)
                    r.reply = std::make_shared<Value>(Value::object({}));
                output += "/" + hex(r.serialize());
            } else {
                JsonRpcDispatcher::Decoder decoder;
                if (mode == 1)
                    decoder = [](std::string_view) -> Value {
                        throw JsonRpcParseError("injected parse failure");
                    };
                if (mode == 2)
                    decoder = [](std::string_view) -> Value {
                        throw std::runtime_error("controlled decoder failure");
                    };
                JsonRpcDispatcher d(
                    decoder, [&](std::string_view s) { log += std::string(s) + "\n"; },
                    [&] { disposed += "D"; });
                auto handler = [&](const Value&, std::shared_ptr<JsonRpcResponse>& r) {
                    switch (profile) {
                    case 1:
                        r->result = std::make_shared<Value>(Value::object({{"answer", Value(42)}}));
                        return true;
                    case 2:
                        return false;
                    case 3:
                        r->error->code(-3);
                        r->error->message(u"custom");
                        return false;
                    case 4:
                        throw std::runtime_error("fixture handler failure");
                    case 5:
                        r->error->code(-4);
                        r->error->data = std::make_shared<Value>("ignored");
                        return true;
                    case 6:
                        r = std::make_shared<JsonRpcResponse>();
                        r->id(u"replacement");
                        r->result = std::make_shared<Value>();
                        r->result->kind = Value::Kind::Array;
                        return true;
                    case 7:
                        r->error.reset();
                        return true;
                    case 8:
                        r.reset();
                        return false;
                    case 9:
                        r->result = std::make_shared<Value>(0);
                        return false;
                    default:
                        return true;
                    }
                };
                std::string registered;
                if (profile) {
                    registered += d.add(u"test", handler) ? "1" : "0";
                    registered += d.add(u"test", {}) ? "1" : "0";
                    d.add(u"removed", {});
                    d.remove(u"removed");
                    d.add(u"again", {});
                    for (auto& key : d.keys())
                        registered += ":" + framework::utf8(key);
                }
                HttpRequest request;
                request.body = unhex(raw);
                auto r = d.handle(request);
                output = std::to_string(r.status) + ":" + hex(r.body) + "|" + registered;
            }
        } catch (const std::exception&) {
            output = "EX";
        }
        std::cout << output << "|" << hex(log) << "|" << disposed << "\n";
    }
}
