// Source: .NET8.0.31 System.Xml.XmlDownloadManager, source-derived orchestration.
#include "opensim/framework/config_xml_transport.hpp"
#include "opensim/framework/nini_config.hpp"
#include <algorithm>
#include <array>
#include "opensim/framework/text.hpp"
namespace opensim::framework {
ConfigXmlStreamError::ConfigXmlStreamError(std::string type, std::u16string text, std::uint32_t hr,
                                           NiniWriterText name)
    : runtime_error(utf8(text)), source_type(std::move(type)), message(std::move(text)), hresult(hr),
      object_name(name ? std::move(name) : nini_writer_text(u"")) {
}
bool ConfigXmlStream::can_seek() const {
    throw std::logic_error("XML stream requires an explicit CanSeek provider");
}
std::int64_t ConfigXmlStream::length() const {
    throw std::logic_error("XML stream requires an explicit Length provider");
}
std::int64_t ConfigXmlStream::position() const {
    throw std::logic_error("XML stream requires an explicit Position provider");
}

namespace {
[[noreturn]] void nr() {
    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
class Memory final : public ConfigXmlStream {
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
    bool closed_ = false;
    [[noreturn]] static void closed() {
        throw ConfigXmlStreamError("System.ObjectDisposedException", u"Cannot access a closed Stream.",
                                   0x80131622);
    }

  public:
    explicit Memory(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {
    }
    std::int32_t read(std::span<std::uint8_t> out) override {
        if (closed_)
            closed();
        auto count = std::min(out.size(), bytes_.size() - position_);
        std::copy_n(bytes_.begin() + position_, count, out.begin());
        position_ += count;
        return static_cast<std::int32_t>(count);
    }
    bool can_seek() const override {
        return !closed_;
    }
    std::int64_t length() const override {
        if (closed_)
            closed();
        return static_cast<std::int64_t>(bytes_.size());
    }
    std::int64_t position() const override {
        if (closed_)
            closed();
        return static_cast<std::int64_t>(position_);
    }
    void close() override {
        closed_ = true;
    }
};
}
std::vector<std::uint8_t> ConfigXmlStream::copy_to_memory() {
    std::vector<std::uint8_t> result;
    std::array<std::uint8_t, 81920> buffer;
    for (;;) {
        auto count = read(buffer);
        if (count < 0 || static_cast<std::size_t>(count) > buffer.size())
            throw std::runtime_error("Invalid stream provider read count");
        if (!count)
            break;
        result.insert(result.end(), buffer.begin(), buffer.begin() + count);
    }
    return result;
}
std::shared_ptr<ConfigXmlStream> config_xml_memory_stream(std::vector<std::uint8_t> bytes) {
    return std::make_shared<Memory>(std::move(bytes));
}
std::shared_ptr<ConfigXmlStream> config_xml_download(std::shared_ptr<ConfigXmlUri> uri,
                                                     const ConfigXmlDownloadBackend& backend,
                                                     std::shared_ptr<ConfigXmlCredentials> credentials,
                                                     std::shared_ptr<ConfigXmlProxy> proxy, bool async_file) {
    if (!uri)
        nr();
    if (!uri->scheme)
        throw std::invalid_argument("XML transport requires System.Uri scheme provider");
    auto scheme = uri->scheme();
    if (scheme && *scheme == u"file") {
        if (!uri->local_path || !backend.open_file)
            throw std::invalid_argument("XML file download requires exact FileStream provider");
        return backend.open_file(uri->local_path(), 1, async_file);
    }
    if (!backend.new_handler || !backend.new_client)
        throw std::invalid_argument(
            "XML non-file download requires fresh HttpClientHandler/HttpClient providers");
    auto handler = backend.new_handler();
    auto client = backend.new_client(handler);
    std::exception_ptr pending;
    std::shared_ptr<ConfigXmlStream> stream;
    std::vector<std::uint8_t> bytes;
    try {
        if (credentials) {
            if (!handler)
                nr();
            handler->credentials(credentials);
        }
        if (proxy) {
            if (!handler)
                nr();
            handler->proxy(proxy);
        }
        if (!client)
            nr();
        stream = client->get_stream(uri);
        try {
            if (!stream)
                nr();
            bytes = stream->copy_to_memory();
        } catch (...) {
            pending = std::current_exception();
        }
        try {
            if (stream)
                stream->close();
        } catch (...) {
            pending = std::current_exception();
        }
    } catch (...) {
        pending = std::current_exception();
    }
    try {
        if (client)
            client->close();
    } catch (...) {
        pending = std::current_exception();
    }
    if (pending)
        std::rethrow_exception(pending);
    return config_xml_memory_stream(std::move(bytes));
}
}
