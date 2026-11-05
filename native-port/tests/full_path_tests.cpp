// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/full_path.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <vector>
using namespace opensim::framework;
namespace {
std::string hex(const std::string& s) {
    std::string out;
    for (unsigned char c : s) {
        out += "0123456789abcdef"[c >> 4];
        out += "0123456789abcdef"[c & 15];
    }
    return out;
}
std::optional<std::string> unhex(const std::string& s) {
    if (s == "-")
        return {};
    std::string out;
    if (s == ".")
        return out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out.push_back(static_cast<char>(std::stoi(s.substr(i, 2), nullptr, 16)));
    return out;
}
std::string exception_name(FullPathFailure kind) {
    switch (kind) {
    case FullPathFailure::ArgumentNull:
        return "ArgumentNullException";
    case FullPathFailure::Argument:
        return "ArgumentException";
    case FullPathFailure::PathTooLong:
        return "PathTooLongException";
    case FullPathFailure::MissingFile:
        return "FileNotFoundException";
    case FullPathFailure::MissingDirectory:
        return "DirectoryNotFoundException";
    case FullPathFailure::Unauthorized:
        return "UnauthorizedAccessException";
    case FullPathFailure::OperationCanceled:
        return "OperationCanceledException";
    case FullPathFailure::ArgumentOutOfRange:
        return "ArgumentOutOfRangeException";
    default:
        return "IOException";
    }
}
struct ProviderFault : std::runtime_error {
    ProviderFault() : runtime_error("cwd provider fault") {
    }
};
std::string unix_probe(const std::string& input) {
    std::istringstream in(input);
    std::string path, cwd;
    int error, fault, calls = 0;
    in >> path >> cwd >> error >> fault;
    auto provider = [&] {
        ++calls;
        if (fault)
            throw ProviderFault();
        return UnixCwdResult{unhex(cwd).value_or(""), static_cast<UnixPathError>(error),
                             "controlled system error", error};
    };
    try {
        auto full = get_full_path_unix(unhex(path), provider);
        return "OK:" + hex(full) + "|" + std::to_string(calls);
    } catch (const FullPathError& e) {
        return "EX:" + exception_name(e.kind) + ":" + e.parameter.value_or("") + ":" + hex(e.what()) + "|" +
               std::to_string(calls);
    } catch (const ProviderFault& e) {
        return "EX:ProviderFault::" + hex(e.what()) + "|" + std::to_string(calls);
    }
}
std::string number_hex(std::uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(8) << value;
    return out.str();
}
std::uint32_t hresult(const FullPathError& error) {
    if (error.kind == FullPathFailure::Io && error.win32_code) {
        auto code = *error.win32_code;
        return code & 0xffff0000u ? code : 0x80070000u | code;
    }
    switch (error.kind) {
    case FullPathFailure::ArgumentNull:
        return 0x80004003u;
    case FullPathFailure::Argument:
        return 0x80070057u;
    case FullPathFailure::PathTooLong:
        return 0x800700ceu;
    case FullPathFailure::MissingFile:
        return 0x80070002u;
    case FullPathFailure::MissingDirectory:
        return 0x80070003u;
    case FullPathFailure::Unauthorized:
        return 0x80070005u;
    case FullPathFailure::OperationCanceled:
        return 0x8013153bu;
    case FullPathFailure::ArgumentOutOfRange:
        return 0x80131502u;
    default:
        return 0x80131620u;
    }
}
std::string probe(const std::string& input, bool metadata = false) {
    try {
        return "OK:" + hex(get_full_path(unhex(input)));
    } catch (const FullPathError& e) {
        std::string type = exception_name(e.kind);
        return "EX:" + type + ":" + e.parameter.value_or("") + ":" + hex(e.what()) +
               (metadata
                    ? ":" + number_hex(hresult(e)) + ":" + (e.win32_code ? number_hex(*e.win32_code) : "-")
                    : "");
    }
}

void providers() {
    FullPathError coded(FullPathFailure::Io, "test", {}, 123);
    if (coded.win32_code != std::optional<std::uint32_t>(123) ||
        FullPathError(FullPathFailure::Argument, "test").win32_code)
        throw std::runtime_error("Full-path code metadata presence");
    auto b = std::make_shared<FullPathBackend>();
    std::vector<std::string> calls;
    b->normalize = [&](const auto& path) {
        calls.push_back("N");
        return path;
    };
    b->expand_short = [&](const auto& path) {
        calls.push_back("E");
        return path;
    };
    try {
        get_full_path({}, b);
        throw std::runtime_error("Null accepted");
    } catch (const FullPathError& e) {
        if (e.kind != FullPathFailure::ArgumentNull || e.parameter != std::optional<std::string>("path"))
            throw;
    }
    try {
        get_full_path(std::string{}, b);
        throw std::runtime_error("Empty accepted");
    } catch (const FullPathError& e) {
        if (e.kind != FullPathFailure::Argument)
            throw;
    }
    try {
        get_full_path(std::string("a\0b", 3), b);
        throw std::runtime_error("NUL accepted");
    } catch (const FullPathError& e) {
        if (e.kind != FullPathFailure::Argument)
            throw;
    }
    if (!calls.empty())
        throw std::runtime_error("Validation called path provider");
    b->normalize = [](const auto&) -> std::u16string { throw std::runtime_error("normalizer fault"); };
    try {
        get_full_path("relative", b);
        throw std::runtime_error("Missing provider fault");
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()) != "normalizer fault")
            throw;
    }
#ifdef _WIN32
    try {
        get_full_path("C:\\" + std::string(40000, 'a'));
        throw std::runtime_error("Huge Windows path accepted");
    } catch (const FullPathError& e) {
        if (e.kind != FullPathFailure::PathTooLong || e.win32_code != std::optional<std::uint32_t>(206))
            throw;
    }
    auto incomplete = std::make_shared<FullPathBackend>();
    if (get_full_path("\\\\?\\a/../b", incomplete) != "\\\\?\\a/../b")
        throw std::runtime_error("Extended prefix did not bypass provider");
    if (get_full_path("\\??\\a/../b", incomplete) != "\\??\\a/../b")
        throw std::runtime_error("NT prefix did not bypass provider");
    b->normalize = [&](const auto&) {
        calls.push_back("N");
        return std::u16string(u"C:\\folder~1\\child");
    };
    b->expand_short = [&](const auto&) -> std::u16string {
        calls.push_back("E");
        throw std::runtime_error("expansion fault");
    };
    try {
        get_full_path("relative", b);
        throw std::runtime_error("Missing expansion fault");
    } catch (const std::runtime_error& e) {
        if (std::string(e.what()) != "expansion fault")
            throw;
    }
    if (calls != std::vector<std::string>{"N", "E"})
        throw std::runtime_error("Expansion provider order");
#endif
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--unix-probe") {
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << unix_probe(line) << '\n';
            return 0;
        }
        if (argc > 1 &&
            (std::string_view(argv[1]) == "--probe" || std::string_view(argv[1]) == "--metadata-probe")) {
            std::string line;
            while (std::getline(std::cin, line))
                std::cout << probe(line, std::string_view(argv[1]) == "--metadata-probe") << '\n';
            return 0;
        }
        providers();
        if (unix_probe("6361636865 2f626164ff 0 0") != "OK:2f626164efbfbd2f6361636865|1")
            throw std::runtime_error("Unix cwd replacement decoding");
        if (unix_probe("6361636865 2f746d70 1 0").find("EX:FileNotFoundException::") != 0)
            throw std::runtime_error("Unix cwd missing-file classification");
        if (unix_probe("2f616273 2f746d70 1 1") != "OK:2f616273|0")
            throw std::runtime_error("Absolute Unix path must bypass cwd provider");
        if (get_full_path(".").empty())
            throw std::runtime_error("Native cwd path");
        std::cout << "PASS: full-path validation, provider faults and native cwd\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
