// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/network/osd_request.hpp"
#include <functional>
#include <stdexcept>
#include <exception>
#include <optional>
#include <atomic>
namespace opensim::network {
class OsdServiceError : public std::runtime_error {
public:
    OsdServiceError(std::string type, std::u16string text, std::optional<int> status = {});
    std::string source_type;
    std::u16string message;
    std::optional<int> status;
};
struct OsdServiceHttpRequest {
    using Text = std::optional<std::u16string>;
    Text method, url;
    std::optional<std::string> body;
    std::vector<std::pair<std::string, std::u16string>> headers;
    virtual ~OsdServiceHttpRequest() = default;
    virtual void close() = 0;
};
class OsdServiceStream {
public:
    virtual ~OsdServiceStream() = default;
    virtual void close() = 0;
};
class OsdServiceReader {
public:
    virtual ~OsdServiceReader() = default;
    virtual std::u16string read_to_end() = 0;
    virtual void close() = 0;
};
class OsdServiceResponse {
public:
    virtual ~OsdServiceResponse() = default;
    virtual void ensure_success() = 0;
    virtual std::shared_ptr<OsdServiceStream> stream() = 0;
    virtual std::optional<std::int64_t> content_length() = 0;
    virtual void close() = 0;
};
class OsdServiceHttpClient {
public:
    virtual ~OsdServiceHttpClient() = default;
    virtual std::shared_ptr<OsdServiceResponse> send(const std::shared_ptr<OsdServiceHttpRequest>&) = 0;
    virtual void close() = 0;
};
class OsdServiceCancellation {
public:
    virtual ~OsdServiceCancellation() = default;
    virtual void on_cancel(std::function<void()>) = 0;
    virtual void close() = 0;
};
struct OsdServiceException {
    std::u16string message;
    bool http_request = false;
    std::optional<int> status;
};
struct OsdServiceServices {
    using Text = std::u16string;
    using Nullable = std::optional<Text>;
    std::function<std::int32_t()> request_number, debug_level, tick_count;
    std::function<std::int32_t()> next_request_number;
    std::function<void(std::int32_t)> set_request_number;
    std::function<std::int32_t(std::int32_t)> elapsed;
    std::function<void(const Text&)> debug, info;
    std::function<void(const Nullable&, std::int32_t, const Text&)> outgoing_detail;
    std::function<void(std::int32_t, const Text&)> response_detail;
    // These adapters own the actual HttpMethod/Uri/request/client/stream/reader/
    // cancellation construction and disposal. No successful transport is invented.
    std::function<std::shared_ptr<OsdServiceHttpClient>(std::int32_t)> client;
    std::function<std::shared_ptr<OsdServiceHttpRequest>(const Nullable&, const Nullable&)> request;
    std::function<std::shared_ptr<OsdServiceReader>(const std::shared_ptr<OsdServiceStream>&)> reader;
    std::function<std::shared_ptr<OsdServiceCancellation>(std::int32_t)> cancellation;
    std::function<Text(const Value&)> serialize_text;
    std::function<std::string(const Value&)> serialize_bytes;
    std::function<std::string(const Text&)> utf8_bytes;
    std::function<std::string(std::string_view)> compress;
    std::function<Value(const Text&)> canonicalize;
    std::function<OsdServiceException(std::exception_ptr)> describe;
};
std::int32_t osd_service_receive_timeout(std::optional<std::int64_t>, bool keepalive);
Value canonical_osd_response_utf16(std::u16string_view);
class OsdServiceRequester {
    OsdServiceServices services_;

public:
    explicit OsdServiceRequester(OsdServiceServices);
    Value send(const OsdServiceServices::Nullable& url, const Value* data,
               const OsdServiceServices::Nullable& method, std::int32_t timeout, bool compressed, bool rpc,
               bool keepalive = false);
};
}
