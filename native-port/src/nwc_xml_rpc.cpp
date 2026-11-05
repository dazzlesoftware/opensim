// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/nwc_xml_rpc.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/osd_value.hpp"
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/invariant_number_parse.hpp"
#include "opensim/framework/asset_base.hpp"
#include <chrono>
namespace opensim::network {
using Text = std::optional<std::u16string>;
using Kind = Value::Kind;
NwcXmlRpcError::NwcXmlRpcError(std::string type, std::u16string text)
    : runtime_error(framework::utf8(text)), source_type(std::move(type)), message(std::move(text)) {
}
namespace {
[[noreturn]] void null_reference() {
    throw NwcXmlRpcError("System.NullReferenceException",
                         u"Object reference not set to an instance of an object.");
}
[[noreturn]] void null_string() {
    throw NwcXmlRpcError("System.ArgumentNullException", u"Value cannot be null. (Parameter 's')");
}
std::shared_ptr<Value> string(Text text) {
    if (!text)
        return {};
    auto p = std::make_shared<Value>(framework::utf8(*text));
    p->sdk_string = std::make_shared<const std::u16string>(*text);
    return p;
}
Text string_value(const Value& v) {
    return v.sdk_string ? Text(*v.sdk_string) : Text(framework::utf16_replacing(v.text));
}
std::string type(const Value& v) {
    switch (v.kind) {
    case Kind::String:
        return "System.String";
    case Kind::Integer:
        return "System.Int32";
    case Kind::Long:
        return "System.Int64";
    case Kind::Boolean:
        return "System.Boolean";
    case Kind::Real:
        return "System.Double";
    case Kind::Array:
        return "System.Collections.ArrayList";
    case Kind::Map:
        return "System.Collections.Hashtable";
    case Kind::Binary:
        return "System.Byte[]";
    case Kind::Date:
        return "System.DateTime";
    default:
        return "System.Object";
    }
}
const Value& cast(const std::shared_ptr<Value>& value, Kind k, const char* target) {
    if (!value) {
        null_reference();
    }
    auto& v = value->view();
    if (v.kind != k)
        throw NwcXmlRpcError("System.InvalidCastException", u"Unable to cast object of type '" +
                                                                framework::utf16(type(v)) + u"' to type '" +
                                                                framework::utf16(target) + u"'.");
    return v;
}
std::shared_ptr<Value> member(const Value& map, const char* name) {
    auto it = map.map.find(name);
    if (it == map.map.end() || it->second.kind == Kind::NullReference)
        return {};
    if (it->second.reference)
        return it->second.reference;
    return std::make_shared<Value>(it->second);
}
std::int32_t integer(const Text& text, bool short_value = false) {
    if (!text) {
        null_string();
    }
    std::int32_t value;
    try {
        value = framework::invariant_convert_to_int32(text);
    } catch (const framework::InvariantNumberError& e) {
        if (short_value && e.kind == framework::InvariantNumberError::Kind::Overflow)
            throw NwcXmlRpcError("System.OverflowException",
                                 u"Value was either too large or too small for an Int16.");
        throw;
    }
    if (short_value && (value < -32768 || value > 32767)) {
        throw NwcXmlRpcError("System.OverflowException",
                             u"Value was either too large or too small for an Int16.");
    }
    return value;
}
std::shared_ptr<Value> date(const Text& text) {
    if (!text) {
        null_string();
    }
    auto fail = [&]() -> void {
        throw NwcXmlRpcError("System.FormatException",
                             text->empty()
                                 ? u"String was not recognized as a valid DateTime."
                                 : u"String '" + *text + u"' was not recognized as a valid DateTime.");
    };
    if (text->size() != 17) {
        fail();
    }
    auto digit = [&](std::size_t at, std::size_t n) {
        int x = 0;
        while (n--) {
            auto c = (*text)[at++];
            if (c < u'0' || c > u'9') {
                fail();
            }
            x = x * 10 + c - u'0';
        }
        return x;
    };
    int y = digit(0, 4), m = digit(4, 2), d = digit(6, 2), h = digit(9, 2), minute = digit(12, 2),
        second = digit(15, 2);
    std::chrono::year_month_day calendar{std::chrono::year(y), std::chrono::month(unsigned(m)),
                                         std::chrono::day(unsigned(d))};
    if ((*text)[8] != u'T' || (*text)[11] != u':' || (*text)[14] != u':') {
        fail();
    }
    if (y < 1 || !calendar.ok() || h > 23 || minute > 59 || second > 59)
        throw NwcXmlRpcError("System.FormatException",
                             u"The DateTime represented by the string '" + *text +
                                 u"' is not supported in calendar 'System.Globalization.GregorianCalendar'.");
    auto days = std::chrono::sys_days(calendar).time_since_epoch().count() + 719162;
    auto ticks = days * 864000000000ll + (h * 3600ll + minute * 60ll + second) * 10000000ll;
    auto p = std::make_shared<Value>();
    p->kind = Kind::Date;
    p->sdk_date = std::make_shared<framework::AssetDateTime>(ticks, 0);
    return p;
}
}
std::int32_t NwcXmlRpcResponse::fault_code() const {
    if (!is_fault)
        return 0;
    const auto& map = cast(value, Kind::Map, "System.Collections.Hashtable");
    auto p = member(map, "faultCode");
    return cast(p, Kind::Integer, "System.Int32").integer;
}
Text NwcXmlRpcResponse::fault_string() const {
    if (!is_fault)
        return u"";
    const auto& map = cast(value, Kind::Map, "System.Collections.Hashtable");
    auto p = member(map, "faultString");
    if (!p)
        return {};
    return string_value(cast(p, Kind::String, "System.String"));
}
NwcXmlRpcResponseDeserializer::NwcXmlRpcResponseDeserializer(Logger logger) : logger_(std::move(logger)) {
}
void NwcXmlRpcResponseDeserializer::element(std::u16string_view name) {
    if (logger_)
        logger_(u"START " + std::u16string(name));
    if (name == u"value") {
        value_.reset();
        text_.reset();
    } else if (name == u"struct" || name == u"array") {
        stack_.push_back({container_, name_});
        container_ = std::make_shared<Value>();
        container_->kind = name == u"struct" ? Kind::Map : Kind::Array;
    }
}
void NwcXmlRpcResponseDeserializer::end_element(std::u16string_view name) {
    if (logger_)
        logger_(u"END " + std::u16string(name));
    if (name == u"base64") {
        auto p = std::make_shared<Value>();
        p->kind = Kind::Binary;
        p->text = framework::utf8(text_.value_or(u""));
        try {
            auto bytes = osd_binary(*p);
            if (bytes->empty())
                p->binary_data = osd_binary(osd_from_binary({}));
        } catch (const std::invalid_argument&) {
            throw NwcXmlRpcError(
                "System.FormatException",
                u"The input is not a valid Base-64 string as it contains a non-base 64 character, more than two padding characters, or an illegal character among the padding characters.");
        }
        value_ = std::move(p);
    } else if (name == u"string")
        value_ = string(text_);
    else if (name == u"double") {
        if (!text_) {
            null_string();
        }
        auto p = std::make_shared<Value>();
        p->kind = Kind::Real;
        p->real = framework::invariant_convert_to_double(text_);
        value_ = std::move(p);
    } else if (name == u"member" && container_ && container_->kind == Kind::Map) {
        if (!name_) {
            throw NwcXmlRpcError("System.ArgumentNullException", u"Value cannot be null. (Parameter 'key')");
        }
        auto key = framework::utf8(*name_);
        if (container_->contains(key)) {
            throw NwcXmlRpcError("System.ArgumentException",
                                 u"Item has already been added. Key in dictionary: '" + *name_ +
                                     u"'  Key being added: '" + *name_ + u"'");
        }
        container_->set(key, value_ ? Value::alias(value_) : Value::null_reference());
    } else if (name == u"value") {
        if (!value_)
            value_ = string(text_);
        if (container_ && container_->kind == Kind::Array)
            container_->array.push_back(value_ ? Value::alias(value_) : Value::null_reference());
    } else if (name == u"struct" || name == u"array") {
        value_ = container_;
        if (stack_.empty())
            throw NwcXmlRpcError("System.InvalidOperationException", u"Stack empty.");
        auto c = std::move(stack_.back());
        stack_.pop_back();
        container_ = std::move(c.container);
        name_ = std::move(c.name);
    } else if (name == u"boolean") {
        auto i = integer(text_, true);
        if (i == 0 || i == 1)
            value_ = std::make_shared<Value>(i == 1);
    } else if (name == u"i4" || name == u"int")
        value_ = std::make_shared<Value>(integer(text_));
    else if (name == u"dateTime.iso8601")
        value_ = date(text_);
    else if (name == u"name")
        name_ = text_;
}
namespace {
framework::NiniXmlReaderSettings reader_settings() {
    auto s = framework::NiniXmlReaderSettings::legacy_document();
    s.dtd_processing = framework::NiniXmlDtdProcessing::Ignore;
    s.validating_reader_compatibility = false;
    return s;
}
}
std::shared_ptr<NwcXmlRpcResponse> NwcXmlRpcResponseDeserializer::deserialize(const Text& xml) {
    if (!xml)
        throw NwcXmlRpcError("System.ArgumentNullException", u"Value cannot be null. (Parameter 's')");
    return parse(
        framework::NativeNiniXmlReader::from_text(framework::nini_writer_text(*xml), reader_settings()));
}
std::shared_ptr<NwcXmlRpcResponse>
NwcXmlRpcResponseDeserializer::deserialize_reader(std::shared_ptr<framework::NativeNiniXmlTextInput> input) {
    // The legacy XmlTextReader(TextReader) null input behaves as empty input;
    // the separate Deserialize(string) overload still rejects a null string.
    if (!input)
        return parse(
            framework::NativeNiniXmlReader::from_text(framework::nini_writer_text(u""), reader_settings()));
    return parse(framework::NativeNiniXmlReader::from_text_input(std::move(input), reader_settings()));
}
std::shared_ptr<NwcXmlRpcResponse>
NwcXmlRpcResponseDeserializer::parse(std::shared_ptr<framework::NativeNiniXmlReader> reader) {
    auto response = std::make_shared<NwcXmlRpcResponse>();
    std::exception_ptr error;
    try {
        std::lock_guard lock(gate_);
        text_.reset();
        value_.reset();
        name_.reset();
        container_.reset();
        stack_.clear();
        while (reader->read()) {
            const auto token = reader->token();
            if (token.node_type == 1)
                element(token.name);
            else if (token.node_type == 15)
                end_element(token.name);
            else if (token.node_type == 3) {
                if (logger_)
                    logger_(u"Text " + token.value);
                text_ = token.value;
            }
            if (token.node_type == 15) {
                if (token.name == u"param") {
                    response->set_value(value_);
                    value_.reset();
                    text_.reset();
                } else if (token.name == u"fault") {
                    response->set_value(value_);
                    response->is_fault = true;
                }
            }
        }
    } catch (...) {
        error = std::current_exception();
    }
    // The source Monitor exits before the XmlTextReader using-scope closes input.
    reader->close();
    if (error)
        std::rethrow_exception(error);
    return response;
}
}
