// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/rpc.hpp"
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
namespace opensim::framework {
class NativeXmlTextWriter;
class NativeNiniXmlTextInput;
class NativeNiniXmlReader;
}
namespace opensim::network {
class NwcXmlRpcError : public std::runtime_error {
  public:
    NwcXmlRpcError(std::string type, std::u16string message);
    std::string source_type;
    std::u16string message;
};
struct NwcXmlRpcResponse {
    NwcXmlRpcResponse() = default;
    NwcXmlRpcResponse(std::int32_t code, const std::optional<std::u16string> &text) { set_fault(code, text); }
    std::shared_ptr<Value> value;
    bool is_fault = false;
    void set_fault(std::int32_t, const std::optional<std::u16string> &);
    void set_value(std::shared_ptr<Value> v) {
        value = std::move(v);
        is_fault = false;
    }
    std::int32_t fault_code() const;
    std::optional<std::u16string> fault_string() const;
};
struct NwcXmlRpcRequest {
    using Parameters = std::vector<std::shared_ptr<Value>>;
    std::optional<std::u16string> method_name;
    std::shared_ptr<Parameters> parameters = std::make_shared<Parameters>();
    NwcXmlRpcRequest() = default;
    NwcXmlRpcRequest(std::optional<std::u16string> method, std::shared_ptr<Parameters> args)
        : method_name(std::move(method)), parameters(std::move(args)) {}
    std::optional<std::u16string> method_name_object() const;
    std::optional<std::u16string> method_name_method() const;
};
// Native boxed wire values. Map order represents the caller's IDictionary.Keys
// enumeration, rather than changing protocol fields to lexical order.
void serialize_nwc_xml_rpc_object(framework::NativeXmlTextWriter &, const std::shared_ptr<Value> &);
void serialize_nwc_xml_rpc_request(framework::NativeXmlTextWriter &,
                                   const std::shared_ptr<NwcXmlRpcRequest> &);
void serialize_nwc_xml_rpc_response(framework::NativeXmlTextWriter &,
                                    const std::shared_ptr<NwcXmlRpcResponse> &);
std::string nwc_xml_rpc_request_bytes(const std::shared_ptr<NwcXmlRpcRequest> &, bool indented = false);
std::string nwc_xml_rpc_response_bytes(const std::shared_ptr<NwcXmlRpcResponse> &, bool indented = false);
class NwcXmlRpcResponseDeserializer {
  public:
    using Logger = std::function<void(std::u16string_view)>;
    explicit NwcXmlRpcResponseDeserializer(Logger = {});
    std::shared_ptr<NwcXmlRpcResponse> deserialize(const std::optional<std::u16string> &);
    // XmlTextReader disposes the supplied reader after releasing its monitor.
    std::shared_ptr<NwcXmlRpcResponse> deserialize_reader(std::shared_ptr<framework::NativeNiniXmlTextInput>);

  private:
    struct Context {
        std::shared_ptr<Value> container;
        std::optional<std::u16string> name;
    };
    std::recursive_mutex gate_;
    std::shared_ptr<Value> container_, value_;
    std::optional<std::u16string> text_, name_;
    std::vector<Context> stack_;
    Logger logger_;
    std::shared_ptr<NwcXmlRpcResponse> parse(std::shared_ptr<framework::NativeNiniXmlReader>);
    void element(std::u16string_view);
    void end_element(std::u16string_view);
};
}
