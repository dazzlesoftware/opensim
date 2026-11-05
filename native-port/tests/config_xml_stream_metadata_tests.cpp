#include "opensim/framework/config_xml_reader_stream.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
using namespace opensim::framework;
std::string hex(std::u16string_view s) {
    std::ostringstream o;
    o << std::hex << std::setfill('0');
    for (auto c : s)
        o << std::setw(4) << unsigned(c);
    return s.empty() ? "-" : o.str();
}
std::string error(const ConfigXmlStreamError& e) {
    std::ostringstream o;
    o << e.source_type << ":" << std::hex << e.hresult << ":" << hex(e.message) << ":" << hex(*e.object_name);
    return o.str();
}
struct Live : ConfigXmlStream {
    mutable std::string trace;
    bool seek = false;
    bool fault = false;
    int closes = 0;
    std::int32_t read(std::span<std::uint8_t>) override {
        trace += "R";
        return 0;
    }
    void close() override {
        ++closes;
    }
    bool can_seek() const override {
        trace += "S";
        return seek;
    }
    std::int64_t length() const override {
        trace += "L";
        if (fault)
            throw std::runtime_error("length-fault");
        return 9000000000;
    }
};
int main(int argc, char**) {
    if (argc == 1) {
        auto memory = config_xml_memory_stream({1, 2});
        if (!memory->can_seek() || memory->length() != 2 || memory->position() != 0)
            return 1;
        auto input = config_xml_reader_stream(memory);
        if (input->length_hint() != 2)
            return 2;
        std::uint8_t b;
        if (input->read({&b, 1}) != 1 || b != 1 || memory->position() != 1)
            return 3;
        input->close();
        if (memory->can_seek() || input->length_hint())
            return 4;
        bool disposed = false;
        try {
            memory->length();
        } catch (const ConfigXmlStreamError& e) {
            disposed = e.source_type == "System.ObjectDisposedException" && e.hresult == 0x80131622;
        }
        if (!disposed)
            return 5;
        auto live = std::make_shared<Live>();
        auto dynamic = config_xml_reader_stream(live);
        if (dynamic->length_hint() || live->trace != "S")
            return 6;
        live->trace.clear();
        live->seek = true;
        if (dynamic->length_hint() != 9000000000 || live->trace != "SL")
            return 7;
        live->fault = true;
        bool forwarded = false;
        try {
            dynamic->length_hint();
        } catch (const std::runtime_error& e) {
            forwarded = std::string(e.what()) == "length-fault";
        }
        if (!forwarded)
            return 8;
        dynamic->close();
        if (live->closes != 1)
            return 9;
        std::weak_ptr<Live> weak = live;
        live.reset();
        if (weak.expired())
            return 10;
        dynamic.reset();
        if (!weak.expired())
            return 11;
        return 0;
    }
    for (std::string line; std::getline(std::cin, line);) {
        std::istringstream row(line);
        int count;
        row >> count;
        std::vector<std::uint8_t> bytes(count);
        for (int i = 0; i < count; ++i)
            bytes[i] = static_cast<std::uint8_t>(i);
        auto stream = config_xml_memory_stream(bytes);
        bool first = true;
        for (std::string command; row >> command;) {
            if (!first)
                std::cout << '/';
            first = false;
            try {
                if (command == "S")
                    std::cout << (stream->can_seek() ? "1" : "0");
                else if (command == "L")
                    std::cout << stream->length();
                else if (command == "P")
                    std::cout << stream->position();
                else if (command == "C") {
                    stream->close();
                    std::cout << "C";
                } else if (command[0] == 'R') {
                    std::vector<std::uint8_t> out(std::stoi(command.substr(1)));
                    auto n = stream->read(out);
                    std::cout << n << ':';
                    for (int i = 0; i < n; ++i)
                        std::cout << std::hex << std::setfill('0') << std::setw(2) << unsigned(out[i]);
                    std::cout << std::dec;
                }
            } catch (const ConfigXmlStreamError& e) {
                std::cout << error(e);
            }
        }
        std::cout << '\n';
    }
}
