#include "opensim/framework/glob.hpp"
#include <bit>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <chrono>
using namespace opensim::framework;
namespace {
std::optional<std::u16string> dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string out;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            out += static_cast<char16_t>(std::stoul(s.substr(i, 2), nullptr, 16) |
                                         (std::stoul(s.substr(i + 2, 2), nullptr, 16) << 8));
    return out;
}
std::string hex(const std::u16string& s) {
    if (s.empty())
        return ".";
    std::string out;
    for (auto c : s)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            out += "0123456789abcdef"[b >> 4];
            out += "0123456789abcdef"[b & 15];
        }
    return out;
}
std::u16string number(int n) {
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
std::string array(const std::vector<std::u16string>& values) {
    std::string out = "A";
    for (auto& s : values)
        out += ":" + hex(s);
    return out;
}
std::string error(const std::string& t, std::int32_t h, const std::u16string& m,
                  const std::optional<std::u16string>& p = {}) {
    return "EX:" + t + ":" + std::to_string(std::bit_cast<std::uint32_t>(h)) + ":" + hex(m) + ":" +
           (p ? hex(*p) : "-");
}
struct Probe {
    int point = 0, nth = 1, hit = 0, scenario = 0, directories = 0;
    std::vector<std::string> trace;
    void event(int p, std::string t) {
        trace.push_back(std::move(t));
        if (point == p && ++hit == nth)
            throw DirectoryEntriesError("System.Exception", std::bit_cast<std::int32_t>(0x80131500u),
                                        u"fault");
    }
    GlobBackend backend(char16_t volume, char16_t directory, char16_t alt) {
        auto result = native_glob_backend();
        auto actual_combine = result.combine;
        result.volume_separator = volume;
        result.directory_separator = directory;
        result.alternate_separator = alt;
        result.directories = [this](const auto& p, const auto& e) {
            event(1, "D:" + hex(p) + ":" + hex(e));
            ++directories;
            if (scenario == 0 || directories > 12)
                return std::vector<std::u16string>{};
            if (scenario == 1)
                return std::vector<std::u16string>{u"dir-" + number(directories)};
            if (scenario == 2)
                return std::vector<std::u16string>{u"dup", u"dup"};
            return std::vector<std::u16string>{u"/absolute", u"\\rooted", u"C:relative", u"",
                                               u"raw-" + std::u16string(1, static_cast<char16_t>(0xd800))};
        };
        result.files = [this](const auto& p, const auto& e) {
            event(2, "F:" + hex(p) + ":" + hex(e));
            return std::vector<std::u16string>{p + u"|" + e, u"duplicate", u"duplicate", u"",
                                               std::u16string(1, static_cast<char16_t>(0xd800))};
        };
        result.combine = [this, actual_combine](const auto& a, const auto& b) {
            event(3, "C:" + hex(a) + ":" + hex(b));
            return scenario == 1 ? u"[" + a + u"]{" + b + u"}" : actual_combine(a, b);
        };
        return result;
    }
};
std::string run(const std::string& row) {
    std::istringstream in(row);
    char mode;
    unsigned volume, directory, alt;
    Probe p;
    std::string input, second;
    in >> mode >> volume >> directory >> alt >> p.point >> p.nth >> p.scenario >> input;
    auto value = dec(input);
    std::string result;
    try {
        if (mode == 'B') {
            in >> second;
            result = "B:" + hex(native_glob_backend().combine(value.value(), dec(second).value()));
        } else if (mode == 'F')
            result = array(glob(value));
        else
            result =
                array(glob(value, p.backend(static_cast<char16_t>(volume), static_cast<char16_t>(directory),
                                            static_cast<char16_t>(alt))));
    } catch (const GlobNullInputError&) {
        result = error("System.NullReferenceException", std::bit_cast<std::int32_t>(0x80004003u),
                       u"Object reference not set to an instance of an object.");
    } catch (const DirectoryEntriesError& e) {
        result = error(e.source_type, e.hresult, e.message, e.parameter);
    }
    if (mode == 'G') {
        result += "/IO:";
        for (std::size_t i = 0; i < p.trace.size(); ++i) {
            if (i)
                result += ';';
            result += p.trace[i];
        }
    }
    return result;
}
void native_filesystem_units() {
    auto root = std::filesystem::temp_directory_path() /
                ("glob-native-" +
                 std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(root))
        throw std::runtime_error("Cannot create owned Glob fixture");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            std::filesystem::remove(root / "x" / "nested.ini", e);
            std::filesystem::remove(root / "x", e);
            std::filesystem::remove(root / "folder.ini", e);
            std::filesystem::remove(root / "first.ini", e);
            std::filesystem::remove(root / "other.txt", e);
            std::filesystem::remove(root, e);
        }
    } cleanup{root};
    std::filesystem::create_directory(root / "x");
    std::filesystem::create_directory(root / "folder.ini");
    for (auto path : {root / "first.ini", root / "other.txt", root / "x" / "nested.ini"}) {
        std::ofstream file(path);
        file << 'x';
    }
    auto direct = glob((root / std::filesystem::path(u"*.ini")).u16string());
    if (direct.size() != 1 ||
        std::filesystem::path(direct[0]).filename() != std::filesystem::path("first.ini"))
        throw std::runtime_error("Native Glob file/directory distinction");
    auto nested = glob((root / std::filesystem::path(u"*/*.ini")).u16string());
    if (nested.size() != 1 ||
        std::filesystem::path(nested[0]).filename() != std::filesystem::path("nested.ini"))
        throw std::runtime_error("Native Glob path expansion");
    if (!glob((root / std::filesystem::path(u"missing/*.ini")).u16string()).empty() ||
        !glob(std::u16string{}).empty())
        throw std::runtime_error("Native Glob empty/missing component behavior");
    bool caught = false;
    try {
        glob(std::nullopt);
    } catch (const std::exception& e) {
        caught = true;
        if (std::string(e.what()) != "Object reference not set to an instance of an object.")
            throw std::runtime_error("Glob null generic error message");
    }
    if (!caught)
        throw std::runtime_error("Missing Glob null error");
}

}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string_view(argv[1]) == "--probe") {
            std::string row;
            while (std::getline(std::cin, row))
                std::cout << run(row) << '\n';
            return 0;
        }
        native_filesystem_units();
        Probe probe;
        probe.scenario = 2;
        auto backend = probe.backend(u':', u'\\', u'/');
        auto found = glob(std::u16string(u"a/b"), backend);
        if (found.size() != 10 || probe.trace.empty() || !probe.trace.front().starts_with("D:"))
            throw std::runtime_error("Glob repeated results/provider ordering");
        std::cout << "PASS Util.Glob orchestration\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
