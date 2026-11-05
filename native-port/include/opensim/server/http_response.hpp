// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/server/http_cookies.hpp"
#include <mutex>
#include <span>
namespace opensim::server {
using HttpBytes = std::shared_ptr<std::vector<std::uint8_t>>;
enum class HttpConnectionType : std::int32_t { Close, KeepAlive };
struct HttpEncoding {
    std::string name;
};
enum class HttpSeekOrigin : std::int32_t { Begin, Current, End };
std::shared_ptr<HttpEncoding> http_utf8_encoding();
class HttpBodyStream {
  public:
    virtual ~HttpBodyStream() = default;
    virtual bool can_read() const = 0;
    virtual std::int64_t length() const = 0;
    virtual void dispose() = 0;
    virtual bool can_seek() const {
        return false;
    }
    virtual bool can_write() const {
        return false;
    }
    virtual std::size_t read(std::span<std::uint8_t> bytes);
    virtual void write(std::span<const std::uint8_t> bytes);
    virtual std::int64_t position() const;
    virtual void position(std::int64_t value);
    virtual std::int64_t seek(std::int64_t offset, HttpSeekOrigin origin);
    virtual void set_length(std::int64_t value);
    virtual void flush() {}
};
class HttpMemoryStream : public HttpBodyStream, public std::enable_shared_from_this<HttpMemoryStream> {
  public:
    bool can_read() const override;
    std::int64_t length() const override;
    void dispose() override;
    bool can_seek() const override {
        return can_read();
    }
    bool can_write() const override {
        return can_read();
    }
    std::size_t read(std::span<std::uint8_t> bytes) override;
    std::int32_t read_byte();
    HttpBytes get_buffer() const;
    std::int64_t position() const override;
    void position(std::int64_t value) override;
    std::int64_t seek(std::int64_t offset, HttpSeekOrigin origin) override;
    void set_length(std::int64_t value) override;
    void write(std::span<const std::uint8_t> bytes) override;
    std::vector<std::uint8_t> to_array() const;
    void write_to(std::shared_ptr<HttpBodyStream> target);

  private:
    void require_open() const;
    void capacity(std::size_t minimum);
    mutable std::recursive_mutex mutex_;
    HttpBytes buffer_ = std::make_shared<std::vector<std::uint8_t>>();
    std::int32_t length_ = 0, position_ = 0;
    bool open_ = true;
};
class HttpResponseState;
class HttpResponseContext {
  public:
    virtual ~HttpResponseContext() = default;
    virtual bool is_closing() const = 0;
    virtual std::int32_t max_requests() const = 0;
    virtual std::int32_t timeout_keep_alive() const = 0;
    virtual void timeout_keep_alive(std::int32_t value) = 0;
    virtual void start_send_response(std::shared_ptr<HttpResponseState> response) = 0;
    virtual bool send_async_start(HttpBytes buffer, std::int32_t start, std::int32_t length) = 0;
    virtual void end_send_response(std::uint32_t request_id, HttpConnectionType connection) = 0;
    virtual void continue_send_response() = 0;
};
class HttpResponseRequest {
  public:
    virtual ~HttpResponseRequest() = default;
    virtual HttpName http_version() const = 0;
    virtual std::shared_ptr<HttpResponseContext> context() const = 0;
    virtual HttpConnectionType connection() const = 0;
    virtual std::uint32_t id() const = 0;
    virtual double arrival_timestamp() const = 0;
};
class HttpResponseState : public std::enable_shared_from_this<HttpResponseState> {
  public:
    explicit HttpResponseState(std::shared_ptr<HttpResponseRequest> request, HttpClock& clock = http_clock());
    HttpResponseState(std::shared_ptr<HttpResponseContext> context, HttpName version,
                      HttpConnectionType connection, HttpClock& clock = http_clock());
#define OPENSIM_HTTP_PROPERTY(Type, Name)                                                                    \
    Type Name() const {                                                                                      \
        std::lock_guard lock(mutex_);                                                                        \
        return Name##_;                                                                                      \
    }                                                                                                        \
    void Name(Type value) {                                                                                  \
        std::lock_guard lock(mutex_);                                                                        \
        Name##_ = std::move(value);                                                                          \
    }
    OPENSIM_HTTP_PROPERTY(HttpConnectionType, connection)
    OPENSIM_HTTP_PROPERTY(bool, chunked)
    OPENSIM_HTTP_PROPERTY(HttpName, protocol_version)
    OPENSIM_HTTP_PROPERTY(std::shared_ptr<HttpEncoding>, encoding)
    OPENSIM_HTTP_PROPERTY(std::int32_t, status)
    OPENSIM_HTTP_PROPERTY(HttpName, reason)
    OPENSIM_HTTP_PROPERTY(std::int64_t, content_length)
    OPENSIM_HTTP_PROPERTY(HttpName, content_type)
    OPENSIM_HTTP_PROPERTY(HttpBytes, raw_buffer)
    OPENSIM_HTTP_PROPERTY(std::int32_t, raw_start)
    OPENSIM_HTTP_PROPERTY(std::int32_t, raw_length)
#undef OPENSIM_HTTP_PROPERTY
    std::int32_t priority() const;
    void priority(std::int32_t value);
    std::int32_t keep_alive() const;
    void keep_alive(std::int32_t value);
    bool headers_sent() const;
    bool sent() const;
    std::uint32_t request_id() const {
        return request_id_;
    }
    double request_timestamp() const {
        return request_timestamp_;
    }
    std::shared_ptr<HttpBodyStream> body();
    std::shared_ptr<ResponseCookies> cookies() const {
        return cookies_;
    }
    HttpBytes header_bytes() const;
    void redirect(HttpName url, std::int32_t status = 302);
    void add_header(HttpName name, HttpName value);
    void get_headers();
    void send();
    bool send_next_async(std::int32_t bytes_limit);
    void check_send_next_async_continue();
    void clear();

  private:
    friend struct HttpResponseProbe;
    void set_header(HttpName name, HttpName value);
    mutable std::recursive_mutex mutex_;
    std::shared_ptr<HttpResponseContext> context_;
    std::shared_ptr<ResponseCookies> cookies_ = std::make_shared<ResponseCookies>();
    std::vector<std::pair<HttpName, HttpName>> headers_;
    std::shared_ptr<HttpBodyStream> body_;
    HttpClock& clock_;
    HttpConnectionType connection_ = HttpConnectionType::Close;
    std::int32_t priority_ = 0, keep_alive_ = 60, status_ = 200, raw_start_ = 0, raw_length_ = 0;
    std::int64_t content_length_ = 0;
    HttpName protocol_version_, content_type_, reason_;
    std::shared_ptr<HttpEncoding> encoding_ = http_utf8_encoding();
    bool chunked_ = false, headers_sent_ = false, sent_ = false;
    std::uint32_t request_id_ = 0;
    double request_timestamp_ = 0;
    HttpBytes raw_buffer_, header_bytes_;
};
}
