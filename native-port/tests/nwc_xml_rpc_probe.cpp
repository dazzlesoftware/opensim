// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/nwc_xml_rpc.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/asset_base.hpp"
#include <bit>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <unordered_map>
#include <algorithm>
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
std::unordered_map<const void*, int> ids;
std::vector<std::shared_ptr<const void>> retained;
std::string Dump(const std::shared_ptr<Value>& input) {
    if (!input || input->kind == Value::Kind::NullReference)
        return "N";
    const auto& v = input->view();
    using K = Value::Kind;
    switch (v.kind) {
    case K::String:
        return "S:" + H(v.sdk_string ? Text(*v.sdk_string) : Text(framework::utf16_replacing(v.text)));
    case K::Integer:
        return "I:" + std::to_string(v.integer);
    case K::Boolean:
        return v.boolean ? "T" : "F";
    case K::Real: {
        std::ostringstream s;
        s << std::hex << std::setfill('0') << std::setw(16) << std::bit_cast<std::uint64_t>(v.real);
        return "R:" + s.str();
    }
    case K::Date:
        return "D:" + std::to_string(v.sdk_date->ticks()) + ":" + std::to_string(v.sdk_date->kind());
    default:
        break;
    }
    std::shared_ptr<const void> object = input;
    if (v.kind == K::Binary)
        object = network::osd_binary(v);
    auto found = ids.find(object.get());
    if (found != ids.end())
        return "@" + std::to_string(found->second);
    int id = int(ids.size()) + 1;
    ids[object.get()] = id;
    retained.push_back(object);
    std::string out;
    auto dump = [](const Value& x) {
        return Dump(x.kind == K::NullReference ? nullptr
                    : x.reference              ? x.reference
                                               : std::make_shared<Value>(x));
    };
    if (v.kind == K::Binary) {
        out = "B:" + std::to_string(id) + ":";
        for (auto c : *network::osd_binary(v)) {
            out += "0123456789abcdef"[c >> 4];
            out += "0123456789abcdef"[c & 15];
        }
        return out;
    }
    if (v.kind == K::Array) {
        out = "A:" + std::to_string(id) + "[";
        for (const auto& x : v.array) {
            if (out.back() != '[')
                out += ',';
            out += dump(x);
        }
        return out + "]";
    }
    if (v.kind == K::Map) {
        out = "M:" + std::to_string(id) + "{";
        auto keys = v.ordered_keys();
        std::sort(keys.begin(), keys.end(),
                  [](auto& a, auto& b) { return framework::utf16(a) < framework::utf16(b); });
        for (const auto& k : keys) {
            if (out.back() != '{')
                out += ',';
            out += H(framework::utf16(k)) + "=" + dump(v.map.at(k));
        }
        return out + "}";
    }
    return "X";
}
std::string Run(network::NwcXmlRpcResponseDeserializer& parser, const Text& xml, int action) {
    std::string result;
    try {
        auto r = parser.deserialize(xml);
        result = "OK:" + std::to_string(r->is_fault ? 1 : 0) + ":" + Dump(r->value);
        if (action & 1)
            result += ";C:" + std::to_string(r->fault_code());
        if (action & 2)
            result += ";S:" + H(r->fault_string());
    } catch (const network::NwcXmlRpcError& e) {
        result += ";E:" + e.source_type + ":" + H(e.message);
    } catch (const framework::NiniXmlProviderError& e) {
        result += ";E:" + e.source_type + ":" + H(e.message);
    } catch (const framework::InvariantNumberError& e) {
        result += ";E:" +
                  std::string(e.kind == framework::InvariantNumberError::Kind::Overflow
                                  ? "System.OverflowException"
                                  : "System.FormatException") +
                  ":" + H(e.message);
    } catch (const std::exception& e) {
        result += ";E:System.Exception:" + H(framework::utf16_replacing(e.what()));
    }
    return result;
}

struct TextInput : framework::NativeNiniXmlTextInput {
    std::u16string data;
    std::size_t position = 0;
    int chunk, fail, reads = 0;
    bool close_fail;
    std::vector<std::string>& calls;
    TextInput(std::u16string d, int c, int f, bool x, std::vector<std::string>& log)
        : data(std::move(d)), chunk(c), fail(f), close_fail(x), calls(log) {
    }
    std::int32_t read(std::span<char16_t> destination) override {
        calls.push_back("R:" + std::to_string(destination.size()));
        if (++reads == fail)
            throw std::runtime_error("read");
        auto n = std::min({destination.size(), static_cast<std::size_t>(chunk), data.size() - position});
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(position), n, destination.begin());
        position += n;
        return static_cast<std::int32_t>(n);
    }
    void close() override {
        calls.push_back("X");
        if (close_fail)
            throw std::runtime_error("close");
    }
};
struct BytesInput : framework::NiniByteStream {
    std::vector<std::uint8_t> data;
    std::size_t at = 0;
    int chunk, fail, reads = 0;
    bool close_fail, readable;
    std::vector<std::string>& calls;
    BytesInput(std::vector<std::uint8_t> d, int c, int f, bool x, bool readable,
               std::vector<std::string>& log)
        : data(std::move(d)), chunk(c), fail(f), close_fail(x), readable(readable), calls(log) {
    }
    bool can_read() override {
        calls.push_back("C");
        return readable;
    }
    bool can_seek() override {
        return false;
    }
    bool can_write() override {
        return false;
    }
    std::int64_t position() override {
        return at;
    }
    std::size_t read(std::span<std::uint8_t> buffer, std::size_t offset, std::size_t count) override {
        calls.push_back("R:" + std::to_string(count));
        if (++reads == fail)
            throw std::runtime_error("read");
        auto n = std::min({count, static_cast<std::size_t>(chunk), data.size() - at});
        std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(at), n,
                    buffer.begin() + static_cast<std::ptrdiff_t>(offset));
        at += n;
        return n;
    }
    void write(std::span<const std::uint8_t>) override {
        throw std::logic_error("unused");
    }
    void flush() override {
        throw std::logic_error("unused");
    }
    void close() override {
        calls.push_back("X");
        if (close_fail)
            throw std::runtime_error("close");
    }
};
std::string StreamError() {
    try {
        throw;
    } catch (const network::NwcXmlRpcError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::NiniXmlProviderError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const framework::InvariantNumberError& e) {
        return std::string(e.kind == framework::InvariantNumberError::Kind::Overflow
                               ? "System.OverflowException"
                               : "System.FormatException") +
               ":" + H(e.message);
    } catch (const framework::NiniStreamError& e) {
        return e.source_type + ":" + H(e.message);
    } catch (const std::exception& e) {
        return "System.Exception:" + H(framework::utf16_replacing(e.what()));
    }
}
void StreamCase(const std::string& line) {
    std::istringstream row(line);
    std::string prefix, h;
    int mode, chunk, fail, close_fail, flags;
    row >> prefix >> mode >> chunk >> fail >> close_fail >> flags >> h;
    ids.clear();
    retained.clear();
    std::vector<std::string> calls;
    network::NwcXmlRpcResponseDeserializer::Logger log;
    if (flags & 8)
        log = [&](std::u16string_view s) { calls.push_back("L:" + H(Text(s))); };
    network::NwcXmlRpcResponseDeserializer parser(log);
    std::shared_ptr<framework::NativeNiniXmlTextInput> input;
    std::string result, extra = "-";
    try {
        if (!(flags & 1)) {
            if (mode == 0)
                input = std::make_shared<TextInput>(D(h).value_or(u""), chunk, fail, close_fail != 0, calls);
            else {
                std::vector<std::uint8_t> bytes;
                if (h != ".")
                    for (std::size_t i = 0; i < h.size(); i += 2)
                        bytes.push_back(std::uint8_t(std::stoi(h.substr(i, 2), nullptr, 16)));
                input = framework::native_xml_stream_reader_input(
                    flags & 2 ? nullptr
                              : std::make_shared<BytesInput>(bytes, chunk, fail, close_fail != 0,
                                                             !(flags & 16), calls));
            }
        }
        auto r = parser.deserialize_reader(input);
        result = "OK:" + std::to_string(r->is_fault ? 1 : 0) + ":" + Dump(r->value);
        result += ";C:" + std::to_string(r->fault_code());
        result += ";S:" + H(r->fault_string());
    } catch (...) {
        result += ";E:" + StreamError();
    }
    if ((flags & 4) && input) {
        try {
            input->close();
        } catch (...) {
            extra = "E:" + StreamError();
        }
    }
    auto second = Run(parser, u"<root/>", 0);
    std::cout << result << '|' << extra << '|' << second << '|';
    for (std::size_t i = 0; i < calls.size(); ++i) {
        if (i)
            std::cout << ';';
        std::cout << calls[i];
    }
    std::cout << '\n';
}
int main() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.starts_with("S ")) {
            StreamCase(line);
            continue;
        }
        std::istringstream in(line);
        int logMode, action;
        std::string a, b;
        in >> logMode >> action >> a >> b;
        std::string trace;
        int events = 0;
        bool nested = false;
        ids.clear();
        retained.clear();
        network::NwcXmlRpcResponseDeserializer* current = nullptr;
        network::NwcXmlRpcResponseDeserializer::Logger log;
        if (logMode)
            log = [&](std::u16string_view message) {
                trace += H(Text(message)) + ";";
                ++events;
                if (logMode > 0 && events == logMode)
                    throw std::runtime_error("logger");
                if (logMode <= -2 && !nested && (logMode == -2 || events == -logMode)) {
                    nested = true;
                    auto captured = trace;
                    auto result = Run(
                        *current,
                        u"<methodResponse><params><param><value><int>7</int></value></param></params></methodResponse>",
                        action);
                    trace = captured + "nested:" + result + ";";
                }
            };
        network::NwcXmlRpcResponseDeserializer parser(log);
        current = &parser;
        auto first = Run(parser, D(a), action), second = Run(parser, D(b), action);
        std::cout << first << '|' << second << '|' << trace << '\n';
    }
}
