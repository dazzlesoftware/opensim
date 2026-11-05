// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/nwc_xml_rpc.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/asset_base.hpp"
#include "opensim/framework/nini_streams.hpp"
#include "opensim/framework/text.hpp"
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>
namespace opensim::network {
namespace {
using Text = std::optional<std::u16string>;
using Writer = framework::NativeXmlTextWriter;
using Kind = Value::Kind;
[[noreturn]] void null_reference() {
    throw NwcXmlRpcError("System.NullReferenceException",
                         u"Object reference not set to an instance of an object.");
}
std::shared_ptr<Value> boxed_text(const Text& text) {
    if (!text)
        return {};
    auto v = std::make_shared<Value>(framework::utf8(*text));
    v->sdk_string = std::make_shared<const std::u16string>(*text);
    return v;
}
Text string_value(const Value& value) {
    return value.sdk_string ? Text(*value.sdk_string) : Text(framework::utf16_replacing(value.text));
}
void start(Writer& w, const char16_t* name) {
    w.write_start_element({}, Text(name), {});
}
void element(Writer& w, const char16_t* name, const Text& text) {
    start(w, name);
    w.write_string(text);
    w.write_end_element();
}
std::shared_ptr<Value> child(const Value& value) {
    if (value.kind == Kind::NullReference)
        return {};
    return value.reference ? value.reference : std::make_shared<Value>(value);
}
std::u16string date_text(const Value& v) {
    if (!v.sdk_date)
        throw std::invalid_argument("Nwc DateTime box requires ticks");
    constexpr std::int64_t day_ticks = 864000000000ll;
    auto ticks = v.sdk_date->ticks();
    auto date =
        std::chrono::year_month_day(std::chrono::sys_days(std::chrono::days(ticks / day_ticks - 719162)));
    auto time = ticks % day_ticks;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setfill('0') << std::setw(4) << int(date.year()) << std::setw(2) << unsigned(date.month())
        << std::setw(2) << unsigned(date.day()) << 'T' << std::setw(2) << time / 36000000000ll << ':'
        << std::setw(2) << time / 600000000ll % 60 << ':' << std::setw(2) << time / 10000000ll % 60;
    return framework::utf16(out.str());
}
std::u16string real_text(const Value& v) {
    if (std::isinf(v.real))
        return std::signbit(v.real) ? u"-Infinity" : u"Infinity";
    auto text = osd_string(v);
    if (auto e = text.find('e'); e != std::string::npos)
        text[e] = 'E';
    return framework::utf16_replacing(text);
}
class Buffer final : public framework::NiniByteStream {
  public:
    std::string bytes;
    bool closed = false;
    bool can_read() override {
        return true;
    }
    bool can_write() override {
        return !closed;
    }
    bool can_seek() override {
        return true;
    }
    std::int64_t position() override {
        return static_cast<std::int64_t>(bytes.size());
    }
    std::size_t read(std::span<std::uint8_t>, std::size_t, std::size_t) override {
        throw std::logic_error("Nwc serializer buffer read is unused");
    }
    void write(std::span<const std::uint8_t> b) override {
        bytes.append(reinterpret_cast<const char*>(b.data()), b.size());
    }
    void flush() override {
    }
    void close() override {
        closed = true;
    }
};
std::shared_ptr<framework::XmlStreamEncoding> encoding() {
    static auto e = [] {
        auto value = std::make_shared<framework::XmlStreamEncoding>(
            *framework::native_xml_intrinsic_encoding(framework::nini_writer_text(u"utf-8")));
        value->preamble = [] { return std::vector<std::uint8_t>{}; };
        return value;
    }();
    return e;
}
template <class T, class F> std::string bytes(const std::shared_ptr<T>& value, bool indented, F write) {
    auto buffer = std::make_shared<Buffer>();
    auto io = framework::native_xml_stream_io(buffer, encoding());
    Writer writer(std::move(io));
    if (indented) {
        writer.formatting(1);
        writer.indentation(4);
    }
    try {
        write(writer, value);
        writer.flush();
        auto result = buffer->bytes;
        writer.close();
        return result;
    } catch (...) {
        auto error = std::current_exception();
        writer.close();
        std::rethrow_exception(error);
    }
}
}
void NwcXmlRpcResponse::set_fault(std::int32_t code, const Text& text) {
    auto v = std::make_shared<Value>();
    v->kind = Kind::Map;
    v->set("faultCode", Value(code));
    auto t = boxed_text(text);
    v->set("faultString", t ? Value::alias(t) : Value::null_reference());
    set_value(std::move(v));
    is_fault = true;
}
Text NwcXmlRpcRequest::method_name_object() const {
    if (!method_name) {
        null_reference();
    }
    auto dot = method_name->find(u'.');
    return dot == std::u16string::npos ? method_name : Text(method_name->substr(0, dot));
}
Text NwcXmlRpcRequest::method_name_method() const {
    if (!method_name) {
        null_reference();
    }
    auto dot = method_name->find(u'.');
    return dot == std::u16string::npos ? method_name : Text(method_name->substr(dot + 1));
}
void serialize_nwc_xml_rpc_object(Writer& writer, const std::shared_ptr<Value>& input) {
    if (!input || input->kind == Kind::NullReference) {
        return;
    }
    const auto& v = input->view();
    switch (v.kind) {
    case Kind::Binary: {
        start(writer, u"base64");
        auto data = osd_binary(v);
        writer.write_base64(std::span<const std::uint8_t>(*data), 0, static_cast<std::int32_t>(data->size()));
        writer.write_end_element();
        break;
    }
    case Kind::String:
        element(writer, u"string", string_value(v));
        break;
    case Kind::Integer:
        element(writer, u"i4", framework::utf16(std::to_string(v.integer)));
        break;
    case Kind::Date:
        element(writer, u"dateTime.iso8601", date_text(v));
        break;
    case Kind::Real:
        element(writer, u"double", real_text(v));
        break;
    case Kind::Boolean:
        element(writer, u"boolean", v.boolean ? u"1" : u"0");
        break;
    case Kind::Array:
        start(writer, u"array");
        start(writer, u"data");
        for (const auto& x : v.array) {
            start(writer, u"value");
            serialize_nwc_xml_rpc_object(writer, child(x));
            writer.write_end_element();
        }
        writer.write_end_element();
        writer.write_end_element();
        break;
    case Kind::Map:
        start(writer, u"struct");
        for (const auto& key : v.ordered_keys()) {
            start(writer, u"member");
            element(writer, u"name", framework::utf16_replacing(key));
            start(writer, u"value");
            serialize_nwc_xml_rpc_object(writer, child(v.map.at(key)));
            writer.write_end_element();
            writer.write_end_element();
        }
        writer.write_end_element();
        break;
    default:
        break;
    }
}
void serialize_nwc_xml_rpc_request(Writer& w, const std::shared_ptr<NwcXmlRpcRequest>& request) {
    w.write_start_document();
    start(w, u"methodCall");
    if (!request)
        null_reference();
    element(w, u"methodName", request->method_name);
    start(w, u"params");
    auto args = request->parameters;
    if (!args)
        null_reference();
    for (const auto& value : *args) {
        start(w, u"param");
        start(w, u"value");
        serialize_nwc_xml_rpc_object(w, value);
        w.write_end_element();
        w.write_end_element();
    }
    w.write_end_element();
    w.write_end_element();
}
void serialize_nwc_xml_rpc_response(Writer& w, const std::shared_ptr<NwcXmlRpcResponse>& response) {
    w.write_start_document();
    start(w, u"methodResponse");
    if (!response)
        null_reference();
    if (response->is_fault)
        start(w, u"fault");
    else {
        start(w, u"params");
        start(w, u"param");
    }
    start(w, u"value");
    serialize_nwc_xml_rpc_object(w, response->value);
    w.write_end_element();
    w.write_end_element();
    if (!response->is_fault)
        w.write_end_element();
    w.write_end_element();
}
std::string nwc_xml_rpc_request_bytes(const std::shared_ptr<NwcXmlRpcRequest>& r, bool indented) {
    return bytes(r, indented, serialize_nwc_xml_rpc_request);
}
std::string nwc_xml_rpc_response_bytes(const std::shared_ptr<NwcXmlRpcResponse>& r, bool indented) {
    return bytes(r, indented, serialize_nwc_xml_rpc_response);
}
}
