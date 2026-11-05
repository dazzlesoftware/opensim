// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/xml_rpc_requester.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/nini_xml_text_writer.hpp"
#include <sstream>
#include <iostream>
using namespace opensim;
using namespace network;
using Text = std::optional<std::u16string>;
std::string H(const std::u16string& t) {
    if (t.empty())
        return ".";
    std::string o;
    for (char16_t c : t)
        for (unsigned b : {unsigned(c) & 255, unsigned(c) >> 8}) {
            o += "0123456789abcdef"[b >> 4];
            o += "0123456789abcdef"[b & 15];
        }
    return o;
}
Text D(const std::string& h) {
    if (h == "-")
        return {};
    if (h == ".")
        return u"";
    std::u16string o;
    for (std::size_t i = 0; i < h.size(); i += 4)
        o += char16_t(std::stoi(h.substr(i, 2), nullptr, 16) |
                      (std::stoi(h.substr(i + 2, 2), nullptr, 16) << 8));
    return o;
}
std::string Error() {
    try {
        throw;
    } catch (const NwcXmlRpcError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::NiniXmlProviderError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::XmlTextWriterError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::XmlEncoderError& e) {
        return std::string(e.source_type) + ":" + H(e.message);
    } catch (const std::exception& e) {
        return "UNEXPECTED:" + std::string(e.what());
    }
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, level, id;
        std::string hex;
        in >> mode >> level >> id >> hex;
        Text text = D(hex);
        std::string result;
        try {
            if (mode == 2) {
                if (!text)
                    throw NwcXmlRpcError("System.NullReferenceException",
                                         u"Object reference not set to an instance of an object.");
                std::u16string log;
                xml_rpc_log_detail([&] { return level; }, [&](const auto& v) { log += v; },
                                   (id == 0 ? u"SEND " : u"RESPONSE ") +
                                       framework::utf16(std::to_string(id)) + u": ",
                                   *text);
                result = "L:" + H(log);
            } else {
                if (mode == 1 || mode == 3 || mode == 4 || mode == 5) {
                    std::shared_ptr<Value> v;
                    if (text) {
                        v = std::make_shared<Value>(framework::utf8(*text));
                        v->sdk_string = std::make_shared<const std::u16string>(*text);
                    }
                    if (mode == 1 || mode == 5) {
                        auto req = std::make_shared<NwcXmlRpcRequest>();
                        req->method_name = mode == 1 ? Text(u"method") : text;
                        req->parameters->push_back(mode == 1 ? v : std::make_shared<Value>(std::int32_t(42)));
                        text = framework::utf16_replacing(nwc_xml_rpc_request_bytes(req, true));
                    } else {
                        auto r = std::make_shared<NwcXmlRpcResponse>();
                        if (mode == 4) {
                            auto values = std::make_shared<Value>();
                            values->kind = Value::Kind::Array;
                            Value inner;
                            inner.kind = Value::Kind::Array;
                            Value real;
                            real.kind = Value::Kind::Real;
                            real.real = 3.5;
                            Value date;
                            date.kind = Value::Kind::Date;
                            date.sdk_date =
                                std::make_shared<framework::AssetDateTime>(638448068960000000LL, 0);
                            inner.array = {Value(true), Value(std::int32_t(47)), real, date,
                                           osd_from_binary(std::make_shared<std::vector<std::uint8_t>>(
                                               std::initializer_list<std::uint8_t>{0, 255}))};
                            values->array = {
                                v ? Value::alias(v) : Value::null_reference(), inner,
                                Value::object({{"name", v ? Value::alias(v) : Value::null_reference()}})};
                            v = values;
                        }
                        r->set_value(v);
                        text = framework::utf16_replacing(nwc_xml_rpc_response_bytes(r, true));
                    }
                }
                if (!text)
                    throw NwcXmlRpcError("System.ArgumentNullException",
                                         u"Value cannot be null. (Parameter 's')");
                result = "X:" + H(compact_nwc_xml_rpc_diagnostic(*text));
            }
        } catch (...) {
            result = "E:" + Error();
        }
        std::cout << result << '\n';
    }
}
