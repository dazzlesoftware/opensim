// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/server/http_response.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>
#include <cstring>
namespace opensim::server {
namespace {
template <class T> T& require(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::invalid_argument("Null HTTP response dependency");
    return *value;
}
template <class T> std::shared_ptr<T> held(std::shared_ptr<T> value) {
    if (!value)
        throw std::invalid_argument("Null HTTP response dependency");
    return value;
}
std::int32_t add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
}
std::int32_t subtract(std::int32_t a, std::int32_t b) {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
}
void ascii(std::string& out, const std::string& value) {
    for (char16_t c : framework::utf16_replacing(value))
        out += static_cast<char>(c & 255);
}
void utf8(std::string& out, const std::string& value) {
    out += framework::utf8(framework::utf16_replacing(value));
}
std::string status_text(int32_t code) {
    const std::pair<int32_t, const char*> names[] = {
#include "../protocol/http_status_names.inc"
    };
    for (auto [value, name] : names)
        if (code == value)
            return name;
    return std::to_string(code);
}
bool equal(const HttpName& left, const HttpName& right) {
    if (!left || !right)
        return left == right;
    return framework::invariant_equals_ignore_case(*left, *right);
}
void append_bytes(const HttpBytes& target, const HttpBytes& source, int32_t start, int32_t length) {
    if (!source || start < 0 || length < 0 || static_cast<size_t>(start) > source->size() ||
        static_cast<size_t>(length) > source->size() - static_cast<size_t>(start))
        throw std::out_of_range("HTTP byte range");
    target->insert(target->end(), source->begin() + start, source->begin() + start + length);
}
}
std::shared_ptr<HttpEncoding> http_utf8_encoding() {
    static auto value = std::make_shared<HttpEncoding>(HttpEncoding{"utf-8"});
    return value;
}
std::size_t HttpBodyStream::read(std::span<uint8_t>) {
    throw std::logic_error("Stream cannot read");
}
void HttpBodyStream::write(std::span<const uint8_t>) {
    throw std::logic_error("Stream cannot write");
}
int64_t HttpBodyStream::position() const {
    throw std::logic_error("Stream cannot seek");
}
void HttpBodyStream::position(int64_t) {
    throw std::logic_error("Stream cannot seek");
}
int64_t HttpBodyStream::seek(int64_t, HttpSeekOrigin) {
    throw std::logic_error("Stream cannot seek");
}
void HttpBodyStream::set_length(int64_t) {
    throw std::logic_error("Stream cannot resize");
}
void HttpMemoryStream::require_open() const {
    if (!open_)
        throw std::logic_error("Memory stream is closed");
}
bool HttpMemoryStream::can_read() const {
    std::lock_guard lock(mutex_);
    return open_;
}
int64_t HttpMemoryStream::length() const {
    std::lock_guard lock(mutex_);
    require_open();
    return length_;
}
void HttpMemoryStream::dispose() {
    std::lock_guard lock(mutex_);
    open_ = false;
}
std::size_t HttpMemoryStream::read(std::span<uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    require_open();
    if (position_ >= length_)
        return 0;
    const auto count = std::min(bytes.size(), static_cast<size_t>(length_ - position_));
    if (count)
        std::memmove(bytes.data(), buffer_->data() + position_, count);
    position_ += static_cast<int32_t>(count);
    return count;
}
int32_t HttpMemoryStream::read_byte() {
    std::lock_guard lock(mutex_);
    require_open();
    if (position_ >= length_)
        return -1;
    return (*buffer_)[position_++];
}
HttpBytes HttpMemoryStream::get_buffer() const {
    std::lock_guard lock(mutex_);
    return buffer_;
}
int64_t HttpMemoryStream::position() const {
    std::lock_guard lock(mutex_);
    require_open();
    return position_;
}
void HttpMemoryStream::position(int64_t value) {
    std::lock_guard lock(mutex_);
    if (value < 0)
        throw std::out_of_range("Negative stream position");
    require_open();
    if (value > INT32_MAX)
        throw std::out_of_range("Stream position overflow");
    position_ = static_cast<int32_t>(value);
}
int64_t HttpMemoryStream::seek(int64_t offset, HttpSeekOrigin origin) {
    std::lock_guard lock(mutex_);
    require_open();
    if (offset > INT32_MAX)
        throw std::out_of_range("Stream seek overflow");
    const int64_t base = origin == HttpSeekOrigin::Begin     ? 0
                         : origin == HttpSeekOrigin::Current ? position_
                         : origin == HttpSeekOrigin::End     ? length_
                                                             : -1;
    if (base < 0)
        throw std::invalid_argument("Invalid stream origin");
    if (offset < -base)
        throw std::out_of_range("Seek before stream origin");
    const auto result = base + offset;
    if (result > INT32_MAX)
        throw std::out_of_range("Stream seek overflow");
    position_ = static_cast<int32_t>(result);
    return result;
}
void HttpMemoryStream::capacity(size_t minimum) {
    if (minimum <= buffer_->size())
        return;
    const auto target = std::max<size_t>({minimum, 256, std::min<size_t>(INT32_MAX, buffer_->size() * 2)});
    auto next = std::make_shared<std::vector<uint8_t>>(target);
    std::copy_n(buffer_->begin(), length_, next->begin());
    buffer_ = std::move(next);
}
void HttpMemoryStream::set_length(int64_t value) {
    std::lock_guard lock(mutex_);
    if (value < 0 || value > INT32_MAX)
        throw std::out_of_range("Stream length");
    require_open();
    capacity(static_cast<size_t>(value));
    if (value > length_)
        std::fill(buffer_->begin() + length_, buffer_->begin() + value, 0);
    length_ = static_cast<int32_t>(value);
    if (position_ > length_)
        position_ = length_;
}
void HttpMemoryStream::write(std::span<const uint8_t> bytes) {
    std::lock_guard lock(mutex_);
    require_open();
    if (bytes.size() > static_cast<size_t>(INT32_MAX - position_))
        throw std::out_of_range("Stream write overflow");
    const auto end = position_ + static_cast<int32_t>(bytes.size());
    auto previous = buffer_;
    capacity(static_cast<size_t>(end));
    if (position_ > length_)
        std::fill(buffer_->begin() + length_, buffer_->begin() + position_, 0);
    if (!bytes.empty())
        std::memmove(buffer_->data() + position_, bytes.data(), bytes.size());
    position_ = end;
    if (end > length_)
        length_ = end;
}
std::vector<uint8_t> HttpMemoryStream::to_array() const {
    std::lock_guard lock(mutex_);
    return {buffer_->begin(), buffer_->begin() + length_};
}
void HttpMemoryStream::write_to(std::shared_ptr<HttpBodyStream> target) {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    HttpBytes buffer;
    std::size_t length;
    {
        std::lock_guard lock(mutex_);
        if (!target)
            throw std::invalid_argument("Null destination stream");
        require_open();
        buffer = buffer_;
        length = static_cast<size_t>(length_);
    }
    target->write(std::span(buffer->data(), length));
}
HttpResponseState::HttpResponseState(std::shared_ptr<HttpResponseRequest> request, HttpClock& clock)
    : clock_(clock) {
    protocol_version_ = require(request).http_version();
    if (!protocol_version_ || protocol_version_->empty())
        protocol_version_ = "HTTP/1.1";
    status_ = 200;
    context_ = request->context();
    connection_ = request->connection();
    request_id_ = request->id();
    request_timestamp_ = request->arrival_timestamp();
    raw_start_ = -1;
    raw_length_ = -1;
}
HttpResponseState::HttpResponseState(std::shared_ptr<HttpResponseContext> context, HttpName version,
                                     HttpConnectionType connection, HttpClock& clock)
    : context_(std::move(context)), clock_(clock), connection_(connection),
      protocol_version_(std::move(version)) {
}
int32_t HttpResponseState::priority() const {
    std::lock_guard lock(mutex_);
    return priority_;
}
void HttpResponseState::priority(int32_t value) {
    std::lock_guard lock(mutex_);
    priority_ = (value > 0 && priority_ < 3) ? value : 0;
}
int32_t HttpResponseState::keep_alive() const {
    std::lock_guard lock(mutex_);
    return keep_alive_;
}
void HttpResponseState::keep_alive(int32_t value) {
    std::lock_guard lock(mutex_);
    keep_alive_ = value > 400 ? 400 : (value <= 0 ? 0 : value);
}
bool HttpResponseState::headers_sent() const {
    std::lock_guard lock(mutex_);
    return headers_sent_;
}
bool HttpResponseState::sent() const {
    std::lock_guard lock(mutex_);
    return sent_;
}
HttpBytes HttpResponseState::header_bytes() const {
    std::lock_guard lock(mutex_);
    return header_bytes_;
}
std::shared_ptr<HttpBodyStream> HttpResponseState::body() {
    std::lock_guard lock(mutex_);
    if (!body_)
        body_ = std::make_shared<HttpMemoryStream>();
    return body_;
}
void HttpResponseState::set_header(HttpName name, HttpName value) {
    for (auto& [key, existing] : headers_)
        if (equal(key, name)) {
            existing = std::move(value);
            return;
        }
    headers_.emplace_back(std::move(name), std::move(value));
}
void HttpResponseState::redirect(HttpName url, int32_t status) {
    std::lock_guard lock(mutex_);
    if (headers_sent_)
        throw std::logic_error("Headers already sent");
    set_header("Location", std::move(url));
    status_ = status;
}
void HttpResponseState::add_header(HttpName name, HttpName value) {
    std::lock_guard lock(mutex_);
    if (headers_sent_)
        throw std::logic_error("Headers already sent");
    if (!value)
        throw std::invalid_argument("Null header value");
    auto units = framework::utf16_replacing(*value);
    for (size_t i = 1; i < units.size(); ++i) {
        if (units[i] == '\r' && !framework::unicode_whitespace(units[i - 1]))
            throw std::invalid_argument("Header newline whitespace");
        if (units[i] == '\n' && units[i - 1] != '\r')
            throw std::invalid_argument("Header newline sequence");
    }
    set_header(std::move(name), std::move(value));
}
void HttpResponseState::get_headers() {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    std::lock_guard lock(mutex_);
    headers_sent_ = true;
    std::string out;
    auto version = protocol_version_.value_or("");
    if (framework::trim_utf8(version).empty())
        version = "HTTP/1.1";
    ascii(out, version + " " + std::to_string(status_) + " ");
    if (!reason_ || reason_->empty())
        ascii(out, status_text(status_));
    else
        utf8(out, *reason_);
    ascii(out, "\r\nDate: " + http_rfc_date(clock_.local_now()) + "\r\n");
    int64_t length = body_ ? held(body_)->length() : 0;
    if (raw_buffer_)
        length = std::bit_cast<int64_t>(std::bit_cast<uint64_t>(length) +
                                        std::bit_cast<uint64_t>(int64_t(raw_length_)));
    ascii(out, "Content-Length: " + std::to_string(length) + "\r\n");
    HttpName type;
    for (const auto& [name, value] : headers_)
        if (equal(name, HttpName("Content-Type"))) {
            type = value;
            break;
        }
    if (!type)
        ascii(out, "Content-Type: " + content_type_.value_or("text/html;charset=UTF-8") + "\r\n");
    if (status_ == 200 || status_ == 206 || status_ == 202 || status_ == 100 || status_ == 302) {
        const auto seconds = require(context_).timeout_keep_alive() / 1000;
        if (connection_ == HttpConnectionType::KeepAlive && seconds > 0 && context_->max_requests() > 0) {
            ascii(out, "Keep-Alive:timeout=" + std::to_string(seconds) + ", max=" +
                           std::to_string(context_->max_requests()) + "\r\nConnection: Keep-Alive\r\n");
        } else {
            ascii(out, "Connection: close\r\n");
            connection_ = HttpConnectionType::Close;
        }
    } else {
        ascii(out, "Connection: close\r\n");
        connection_ = HttpConnectionType::Close;
    }
    for (size_t i = 0; i < headers_.size(); ++i) {
        auto [name, value] = headers_[i];
        if (name && (name == "Connection" || name == "Content-Length" || name == "Date" ||
                     name == "Keep-Alive" || name == "Server"))
            continue;
        if (value) {
            ascii(out, name.value_or("") + ": ");
            utf8(out, *value);
            ascii(out, "\r\n");
        }
    }
    ascii(out, "Server: OSWebServer\r\n");
    cookies_->each([&](std::shared_ptr<ResponseCookie> cookie) {
        utf8(out,
             "Set-Cookie: " + (cookie ? cookie->to_string(clock_).value_or("") : std::string{}) + "\r\n");
    });
    ascii(out, "\r\n");
    headers_.clear();
    header_bytes_ = std::make_shared<std::vector<uint8_t>>(out.begin(), out.end());
}
void HttpResponseState::send() {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    std::lock_guard lock(mutex_);
    if (require(context_).is_closing())
        return;
    if (sent_)
        throw std::logic_error("Already sent");
    if (context_->max_requests() == 0 || keep_alive_ == 0) {
        connection_ = HttpConnectionType::Close;
        context_->timeout_keep_alive(0);
    } else if (keep_alive_ > 0)
        context_->timeout_keep_alive(keep_alive_ * 1000);
    if (raw_buffer_) {
        const auto size = static_cast<int32_t>(raw_buffer_->size());
        if (raw_start_ > size)
            return;
        if (raw_start_ < 0)
            raw_start_ = 0;
        if (raw_length_ < 0)
            raw_length_ = size;
        if (add(raw_length_, raw_start_) > size)
            raw_length_ = size - raw_start_;
    }
    if (raw_length_ == 0)
        raw_buffer_.reset();
    get_headers();
    if (raw_buffer_ && raw_length_ > 0) {
        const auto size = add(static_cast<int32_t>(header_bytes_->size()), raw_length_);
        if (size < 8192) {
            append_bytes(header_bytes_, raw_buffer_, raw_start_, raw_length_);
            raw_buffer_.reset();
            raw_length_ = 0;
        }
    }
    if (body_ && held(body_)->length() == 0) {
        held(body_)->dispose();
        body_.reset();
    }
    if (!header_bytes_ && !raw_buffer_ && !body_) {
        sent_ = true;
        context_->end_send_response(request_id_, connection_);
    } else
        context_->start_send_response(shared_from_this());
}
bool HttpResponseState::send_next_async(int32_t limit) {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    std::lock_guard lock(mutex_);
    if (header_bytes_) {
        auto bytes = header_bytes_;
        const auto length = static_cast<int32_t>(bytes->size());
        if (!require(context_).send_async_start(bytes, 0, length)) {
            if (header_bytes_)
                header_bytes_.reset();
            if (body_) {
                held(body_)->dispose();
                body_.reset();
            }
            raw_buffer_.reset();
            sent_ = true;
            return false;
        }
        return true;
    }
    bool result;
    if (raw_buffer_) {
        if (raw_length_ > 0) {
            auto bytes = raw_buffer_;
            const auto start = raw_start_;
            if (raw_length_ > limit) {
                raw_length_ = subtract(raw_length_, limit);
                raw_start_ = add(raw_start_, limit);
                if (raw_length_ <= 0)
                    raw_buffer_.reset();
                result = require(context_).send_async_start(bytes, start, limit);
            } else {
                const auto length = raw_length_;
                raw_length_ = 0;
                raw_buffer_.reset();
                result = require(context_).send_async_start(bytes, start, length);
            }
            if (!result) {
                raw_buffer_.reset();
                if (body_) {
                    held(body_)->dispose();
                    body_.reset();
                }
                sent_ = true;
                return false;
            }
            return true;
        } else
            raw_buffer_.reset();
    }
    if (body_) {
        if (held(body_)->length() != 0) {
            auto memory = std::dynamic_pointer_cast<HttpMemoryStream>(body_);
            raw_buffer_ = require(memory).get_buffer();
            raw_start_ = 0;
            raw_length_ = static_cast<int32_t>(memory->length());
            held(body_)->dispose();
            body_.reset();
            if (raw_length_ > 0) {
                auto bytes = raw_buffer_;
                const auto start = raw_start_;
                if (raw_length_ > limit) {
                    raw_length_ = subtract(raw_length_, limit);
                    raw_start_ = add(raw_start_, limit);
                    if (raw_length_ <= 0)
                        raw_buffer_.reset();
                    result = require(context_).send_async_start(bytes, start, limit);
                } else {
                    const auto length = raw_length_;
                    result = require(context_).send_async_start(bytes, start, length);
                    raw_length_ = 0;
                    raw_buffer_.reset();
                }
                if (!result) {
                    raw_buffer_.reset();
                    sent_ = true;
                    return false;
                }
                return true;
            } else
                raw_buffer_.reset();
        } else {
            held(body_)->dispose();
            body_.reset();
        }
    }
    sent_ = true;
    require(context_).end_send_response(request_id_, connection_);
    return false;
}
void HttpResponseState::check_send_next_async_continue() {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    std::lock_guard lock(mutex_);
    if (header_bytes_)
        header_bytes_.reset();
    if (!header_bytes_ && !raw_buffer_ && !body_) {
        sent_ = true;
        require(context_).end_send_response(request_id_, connection_);
    } else
        require(context_).continue_send_response();
}
void HttpResponseState::clear() {
    [[maybe_unused]] const auto lifetime = weak_from_this().lock();
    std::lock_guard lock(mutex_);
    if (header_bytes_)
        header_bytes_.reset();
    if (body_ && held(body_)->can_read()) {
        held(body_)->dispose();
        body_.reset();
    }
    raw_buffer_.reset();
}
}
