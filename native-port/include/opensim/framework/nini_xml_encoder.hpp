// Port of .NET 8.0.31 System.Xml.XmlTextEncoder. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
// Consumed synchronous encoder dependency of the native XML writer.
#pragma once
#include <functional>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
namespace opensim::framework {
enum class XmlEncoderWriteKind { Character, String, Span, Array };
struct XmlEncoderError : std::runtime_error {
    enum class Kind { InvalidPair, InvalidHigh, MissingLow, SplitPair };
    Kind kind;
    char16_t high = 0, low = 0;
    std::u16string message;
    static constexpr const char* source_type = "System.ArgumentException";
    static constexpr unsigned hresult = 0x80070057;
    explicit XmlEncoderError(Kind, char16_t high = 0, char16_t low = 0);
};
struct XmlEncoderArrayError : std::runtime_error {
    std::string source_type;
    unsigned hresult;
    std::u16string message, parameter;
    std::optional<std::int32_t> actual_value;
    XmlEncoderArrayError(std::string type, unsigned code, std::u16string text, std::u16string param,
                         std::optional<std::int32_t> actual = {})
        : std::runtime_error("XML encoder array argument error"), source_type(std::move(type)), hresult(code),
          message(std::move(text)), parameter(std::move(param)), actual_value(actual) {}
};
// The sink preserves TextWriter overload dispatch and partial-write failures.
// Nullable String writes are distinct from empty strings. Views are synchronous.
class NativeXmlTextEncoder {
public:
    using Sink = std::function<void(XmlEncoderWriteKind, std::optional<std::u16string_view>)>;
    explicit NativeXmlTextEncoder(Sink sink) : sink_(std::move(sink)) {}
    void quote_char(char16_t q) noexcept {
        quote_ = q;
    }
    void start_attribute(bool cache);
    void end_attribute();
    std::u16string attribute_value() const {
        return cache_ ? attribute_ : std::u16string{};
    }
    void write(std::u16string_view);
    // Prevalidated slices used by the checked array overload.
    void write_array_slice(std::u16string_view);
    void write_raw_array_slice(std::u16string_view);
    void write_array(std::optional<std::u16string_view>, std::int32_t offset, std::int32_t count,
                     bool raw = false);
    void write_raw_checked(std::optional<std::u16string_view>);
    void write_char_entity(char16_t);
    void write_surrogate_char(char16_t low, char16_t high);
    void write_surrogate_entity(char16_t low, char16_t high);
    void write_entity_ref(std::optional<std::u16string_view>);

private:
    Sink sink_;
    bool in_attribute_ = false, cache_ = false;
    char16_t quote_ = u'"';
    std::u16string attribute_;
    void character(char16_t);
    void text(std::optional<std::u16string_view>);
    void entity(std::optional<std::u16string_view>);
    void numeric(std::u16string_view);
    static std::u16string hex(unsigned);
};
}
