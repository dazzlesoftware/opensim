#include "opensim/framework/nini_config_invariant.hpp"
#include <algorithm>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iostream>
using namespace opensim::framework;
namespace {
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
class Memory final : public NiniByteStream {
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;
    bool closed_ = false;

  public:
    explicit Memory(const std::u16string& input) {
        bytes_ = {0xff, 0xfe};
        for (auto c : input) {
            bytes_.push_back(static_cast<std::uint8_t>(c));
            bytes_.push_back(static_cast<std::uint8_t>(c >> 8));
        }
    }
    bool can_read() override {
        return !closed_;
    }
    bool can_write() override {
        return false;
    }
    bool can_seek() override {
        return !closed_;
    }
    std::int64_t position() override {
        return static_cast<std::int64_t>(position_);
    }
    std::size_t read(std::span<std::uint8_t> buffer, std::size_t offset, std::size_t count) override {
        if (closed_)
            throw std::runtime_error("Fixture read after close");
        auto n = std::min(count, bytes_.size() - position_);
        std::copy_n(bytes_.begin() + position_, n, buffer.begin() + offset);
        position_ += n;
        return n;
    }
    void write(std::span<const std::uint8_t>) override {
        throw std::runtime_error("Read-only fixture");
    }
    void flush() override {
    }
    void close() override {
        closed_ = true;
    }
};
}
int main() {
    try {
        auto backend = native_nini_invariant_config_backend();
        auto arena = std::make_shared<NiniConfigArena>(backend);
        auto stream = std::make_shared<Memory>(
            u"[Settings]\r\nanswer = 42\r\nround = 1.0000000596046447753906250000001\r\nzero = -0\r\nenabled = yEs\r\nKELVIN\u212a = value\r\nexpanded = ${answer}\r\n");
        auto source = arena->create_ini_source(std::static_pointer_cast<NiniWriterStream>(stream));
        auto settings = source->configs()->at(nini_writer_text(u"Settings"));
        require(bool(settings), "UTF16 BOM document section");
        require(settings->get_int(nini_writer_text(u"answer")) == 42, "INI through actual number provider");
        require(std::bit_cast<std::uint32_t>(settings->get_float(nini_writer_text(u"round"))) == 0x3f800001,
                "INI direct Single rounding");
        require(std::bit_cast<std::uint64_t>(settings->get_double(nini_writer_text(u"zero"))) ==
                    0x8000000000000000ull,
                "INI negative zero");
        source->alias()->add_alias(nini_writer_text(u"Yes"), true);
        require(settings->get_boolean(nini_writer_text(u"enabled")), "Captured invariant alias comparer");
        source->case_sensitive(false);
        require(*settings->get(nini_writer_text(u"kelvink")) == u"value",
                "Invariant raw UTF16 lower provider");
        require(*settings->get_expanded(nini_writer_text(u"expanded")) == u"42",
                "Configuration expansion through native search provider");
        settings->set_text(nini_writer_text(u"answer"), nini_writer_text(u"43"));
        require(settings->get_int(nini_writer_text(u"ANSWER")) == 43,
                "Case-insensitive update reads live value");
        auto directory =
            std::filesystem::temp_directory_path() /
            ("nini-config-invariant-" +
             std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
        require(std::filesystem::create_directory(directory), "Isolated native config directory");
        auto path = directory / std::filesystem::path(u"configuration-\u914d\u7f6e.ini");
        struct Cleanup {
            std::filesystem::path file, directory;
            ~Cleanup() {
                std::error_code e;
                std::filesystem::remove(file, e);
                std::filesystem::remove(directory, e);
            }
        } cleanup{path, directory};
        auto filename = nini_writer_text(path.u16string());
        source->save(filename);
        auto reloaded = arena->create_ini_source(filename);
        auto again = reloaded->configs()->at(nini_writer_text(u"Settings"));
        require(again->get_int(nini_writer_text(u"answer")) == 43,
                "Actual native Nini source file save/load");
        try {
            settings->set_text(nini_writer_text(u"bad"), nini_writer_text(std::u16string(1, 0xd800)));
            settings->get_int(nini_writer_text(u"bad"));
            throw std::runtime_error("Expected numeric format error");
        } catch (const NiniConfigError& e) {
            require(e.kind == NiniConfigError::Kind::Format &&
                        e.message.find(static_cast<char16_t>(0xd800)) != e.message.npos,
                    "Raw UTF16 error metadata survives provider adapter");
        }
        std::cout << "PASS concrete invariant Nini config numeric/culture/stream/file integration\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
