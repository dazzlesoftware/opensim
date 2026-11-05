// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/nwc_xml_rpc.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/text.hpp"
#include <iostream>
#include <sstream>
using namespace opensim;
using network::Value;
using Text = std::optional<std::u16string>;
std::string H(const Text& t) {
    if (!t)
        return "-";
    if (t->empty())
        return ".";
    std::string o;
    for (char16_t c : *t)
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
std::string Hex(std::string_view data) {
    std::string o;
    for (unsigned char c : data) {
        o += "0123456789abcdef"[c >> 4];
        o += "0123456789abcdef"[c & 15];
    }
    return o;
}
std::string Error() {
    try {
        throw;
    } catch (const network::NwcXmlRpcError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::XmlTextWriterError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::XmlEncoderError& e) {
        return std::string(e.source_type) + ":" + H(e.message);
    } catch (const framework::NiniStreamError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const std::exception& e) {
        return "System.Exception:" + H(framework::utf16_replacing(e.what()));
    }
}
Value TextValue(const Text& p) {
    if (!p)
        return Value::null_reference();
    Value v(framework::utf8(*p));
    v.sdk_string = std::make_shared<const std::u16string>(*p);
    return v;
}
Value Array(std::vector<Value> v) {
    Value a;
    a.kind = Value::Kind::Array;
    a.array = std::move(v);
    return a;
}
Value Map(std::initializer_list<std::pair<const char*, Value>> v) {
    Value a;
    a.kind = Value::Kind::Map;
    for (const auto& [k, x] : v)
        a.set(k, x);
    return a;
}
Value Atom(int kind, const Text& p) {
    auto s = framework::utf8(p.value_or(u""));
    Value v;
    switch (kind) {
    case 0:
        return Value::null_reference();
    case 1:
        return TextValue(p);
    case 2:
        return Value(std::int32_t(std::stol(s)));
    case 3:
        return Value(s == "True");
    case 4:
        v.kind = Value::Kind::Real;
        v.real = std::stod(s);
        return v;
    case 5:
        return Value::long_integer(std::stoll(s));
    case 6:
        return Array({TextValue(p), Value::null_reference(), Value(std::int32_t(7)), [] {
                          Value b;
                          b.kind = Value::Kind::Binary;
                          b.binary_data = std::make_shared<std::vector<std::uint8_t>>();
                          return b;
                      }()});
    case 7:
        return Map({{"z", TextValue(p)}, {"a", Value::null_reference()}, {"k", Value(std::int32_t(7))}});
    case 8:
        v.kind = Value::Kind::Binary;
        {
            auto data = std::make_shared<std::vector<std::uint8_t>>();
            for (std::size_t i = 0; i < s.size(); i += 2)
                data->push_back(std::uint8_t(std::stoi(s.substr(i, 2), nullptr, 16)));
            v.binary_data = data;
        }
        return v;
    case 9:
        v.kind = Value::Kind::Date;
        v.sdk_date = std::make_shared<framework::AssetDateTime>(std::stoll(s), 2);
        return v;
    default:
        return v;
    }
}
struct Buffer : framework::NiniByteStream {
    std::string data;
    bool can_read() override {
        return true;
    }
    bool can_write() override {
        return true;
    }
    bool can_seek() override {
        return true;
    }
    std::int64_t position() override {
        return data.size();
    }
    std::size_t read(std::span<std::uint8_t>, std::size_t, std::size_t) override {
        return 0;
    }
    void write(std::span<const std::uint8_t> b) override {
        data.append(reinterpret_cast<const char*>(b.data()), b.size());
    }
    void flush() override {
    }
    void close() override {
    }
};
template <class F> std::string Read(F f) {
    try {
        return H(f());
    } catch (...) {
        return "E:" + Error();
    }
}
std::string Model(const std::shared_ptr<Value>& value, const Text& payload) {
    network::NwcXmlRpcResponse r(-77, payload);
    auto state = [&] {
        return std::string(r.is_fault ? "True" : "False") + ":" + std::to_string(r.fault_code()) + ":" +
               H(r.fault_string());
    };
    auto text = state();
    r.set_value(value);
    text += ":" + state();
    r.set_fault(42, {});
    return text + ":" + state();
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        int mode, shape, flags, kind;
        std::string p, m;
        in >> mode >> shape >> flags >> kind >> p >> m;
        auto payload = D(p), method = D(m);
        auto atom = Atom(kind, payload);
        auto v = shape == 0   ? atom
                 : shape == 1 ? Array({atom, Value::null_reference(), atom})
                 : shape == 2
                     ? Map({{"z", atom}, {"a", Value::null_reference()}, {"last", atom}})
                     : Array({Map({{"a", atom}, {"b", Array({Value::null_reference(), atom})}}), atom});
        auto value = std::make_shared<Value>(v);
        auto request = flags & 1 ? nullptr : std::make_shared<network::NwcXmlRpcRequest>();
        if (request) {
            request->method_name = method;
            if (flags & 2)
                request->parameters.reset();
            else
                request->parameters->push_back(value);
        }
        auto response = flags & 1 ? nullptr : std::make_shared<network::NwcXmlRpcResponse>();
        if (response) {
            response->value = value;
            response->is_fault = (flags & 4) != 0;
        }
        auto names =
            Read([&]() -> Text {
                if (!request)
                    throw network::NwcXmlRpcError("System.NullReferenceException",
                                                  u"Object reference not set to an instance of an object.");
                return request->method_name_object();
            }) +
            ":" + Read([&]() -> Text {
                if (!request)
                    throw network::NwcXmlRpcError("System.NullReferenceException",
                                                  u"Object reference not set to an instance of an object.");
                return request->method_name_method();
            });
        auto buffer = std::make_shared<Buffer>();
        auto encoding = std::make_shared<framework::XmlStreamEncoding>(
            *framework::native_xml_intrinsic_encoding(framework::nini_writer_text(u"utf-8")));
        encoding->preamble = [] { return std::vector<std::uint8_t>{}; };
        framework::NativeXmlTextWriter writer(framework::native_xml_stream_io(buffer, encoding));
        if (flags & 16) {
            writer.formatting(1);
            writer.indentation(4);
        }
        std::string error = "-", close = "-";
        try {
            if (mode == 0)
                network::serialize_nwc_xml_rpc_request(writer, request);
            else if (mode == 1)
                network::serialize_nwc_xml_rpc_response(writer, response);
            else
                network::serialize_nwc_xml_rpc_object(writer, value);
            writer.flush();
        } catch (...) {
            error = Error();
        }
        try {
            writer.close();
        } catch (...) {
            close = Error();
        }
        std::string pretty = "-";
        try {
            if (mode == 0) {
                if (!request)
                    throw network::NwcXmlRpcError("System.NullReferenceException",
                                                  u"Object reference not set to an instance of an object.");
                pretty = Hex(network::nwc_xml_rpc_request_bytes(request, true));
            } else if (mode == 1) {
                if (!response)
                    throw network::NwcXmlRpcError("System.NullReferenceException",
                                                  u"Object reference not set to an instance of an object.");
                pretty = Hex(network::nwc_xml_rpc_response_bytes(response, true));
            }
        } catch (...) {
            pretty = "E:" + Error();
        }
        std::cout << Model(value, payload) << '|' << names << '|' << error << '|' << close << '|'
                  << Hex(buffer->data) << '|' << pretty << '\n';
    }
}
