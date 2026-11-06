// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/nwc_http.hpp"
namespace opensim::network {
class XmlRpcRequesterClient {
  public:
    virtual ~XmlRpcRequesterClient() = default;
    virtual std::shared_ptr<NwcXmlRpcResponse> send(const std::shared_ptr<NwcXmlRpcRequest>&,
                                                    const std::optional<std::u16string>&) = 0;
    // Explicit disposal: source finally exceptions can replace the pending error.
    virtual void close() = 0;
};
struct XmlRpcRequesterServices {
    using Text = std::u16string;
    std::function<std::int32_t()> request_number, debug_level, tick_count;
    std::function<void(std::int32_t)> set_request_number;
    std::function<std::int32_t(std::int32_t)> elapsed;
    std::function<std::shared_ptr<XmlRpcRequesterClient>(std::int32_t)> client;
    std::function<void(const Text&)> debug, error, info;
    std::function<void(std::int32_t, const Text&)> outgoing_detail, response_detail;
    // Optional override for the consumed XElement diagnostic serialization.
    std::function<Text(const Text&)> compact_xml;
    std::function<Text(const std::shared_ptr<NwcXmlRpcRequest>&)> request_text;
    std::function<Text(const std::shared_ptr<NwcXmlRpcResponse>&)> response_text;
    std::function<Text(std::exception_ptr)> exception_message;
};
class XmlRpcRequester {
    XmlRpcRequesterServices services_;

  public:
    explicit XmlRpcRequester(XmlRpcRequesterServices);
    std::shared_ptr<Value> send_request(std::shared_ptr<Value>, const std::optional<std::u16string>& method,
                                        const std::optional<std::u16string>& url);
};
// Disposes a wrapper while preserving the shared transport's lifetime.
std::shared_ptr<XmlRpcRequesterClient> xml_rpc_http_client(std::shared_ptr<NwcXmlRpcHttpTransport>,
                                                           std::int32_t timeout);
// WebUtil's diagnostic conversion includes DEL (127), replacing all other
// nonprintable/non-ASCII UTF-16 code units independently with a full stop.
std::u16string xml_rpc_binary_to_ascii(std::u16string);
// Consumed XElement.Parse / DisableFormatting path for Nwc serializers' XML.
std::u16string compact_nwc_xml_rpc_diagnostic(const std::u16string&);
// WebUtil.LogOutgoingDetail and LogResponseDetail share this implementation.
void xml_rpc_log_detail(const std::function<std::int32_t()>& debug_level,
                        const std::function<void(const std::u16string&)>& debug, const std::u16string& context,
                        const std::u16string& output);
}
