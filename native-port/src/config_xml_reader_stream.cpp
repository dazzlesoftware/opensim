#include "opensim/framework/config_xml_reader_stream.hpp"
#include <stdexcept>
namespace opensim::framework {
namespace {
class Input final : public NativeNiniXmlByteInput {
    std::shared_ptr<ConfigXmlStream> stream_;

public:
    explicit Input(std::shared_ptr<ConfigXmlStream> stream) : stream_(std::move(stream)) {}
    std::int32_t read(std::span<std::uint8_t> out) override {
        return stream_->read(out);
    }
    void close() override {
        stream_->close();
    }
    std::optional<std::int64_t> length_hint() const override {
        if (!stream_->can_seek())
            return {};
        return stream_->length();
    }
};
}
std::shared_ptr<NativeNiniXmlByteInput> config_xml_reader_stream(std::shared_ptr<ConfigXmlStream> stream) {
    if (!stream)
        throw std::invalid_argument("XML reader input bridge requires a non-null stream");
    return std::make_shared<Input>(std::move(stream));
}
}
