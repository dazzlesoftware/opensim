#include "opensim/framework/directory_entries.hpp"
#include <bit>
#include <iostream>
#include <sstream>
#include <filesystem>
#include <fstream>
#include <chrono>
#include <algorithm>
using namespace opensim::framework;
namespace {
DirectoryEntriesText dec(const std::string& s) {
    if (s == "-")
        return {};
    std::u16string t;
    if (s != ".")
        for (std::size_t i = 0; i < s.size(); i += 4)
            t += static_cast<char16_t>(std::stoul(s.substr(i, 2), nullptr, 16) |
                                       (std::stoul(s.substr(i + 2, 2), nullptr, 16) << 8));
    return std::make_shared<const std::u16string>(std::move(t));
}
std::string hex(const std::u16string& t) {
    if (t.empty())
        return ".";
    std::string o;
    for (auto c : t)
        for (unsigned char b : {static_cast<unsigned char>(c), static_cast<unsigned char>(c >> 8)}) {
            o += "0123456789abcdef"[b >> 4];
            o += "0123456789abcdef"[b & 15];
        }
    return o;
}
std::string error(const DirectoryEntriesError& e) {
    return e.source_type + ":" + std::to_string(std::bit_cast<std::uint32_t>(e.hresult)) + ":" +
           hex(e.message) + ":" + (e.parameter ? hex(*e.parameter) : "-");
}
DirectoryEntriesPlatform platform() {
#ifdef _WIN32
    return DirectoryEntriesPlatform::Windows;
#elif defined(__APPLE__)
    return DirectoryEntriesPlatform::MacOS;
#else
    return DirectoryEntriesPlatform::Linux;
#endif
}
struct Probe;
struct Cursor : DirectoryEntriesCursor {
    Probe& p;
    std::size_t index = 0;
    bool closed = false;
    explicit Cursor(Probe& p) : p(p) {
    }
    std::optional<DirectoryEntry> next() override;
    void close() override;
};
struct Probe {
    int point = 0, nth = 1, hit = 0;
    std::vector<std::string> trace;
    void event(int p, std::string t) {
        trace.push_back(std::move(t));
        if (point == p && ++hit == nth)
            throw DirectoryEntriesError("System.Exception", std::bit_cast<std::int32_t>(0x80131500u),
                                        u"fault");
    }
};
std::optional<DirectoryEntry> Cursor::next() {
    p.event(3, "N:" + std::to_string(index));
    static const std::vector<DirectoryEntry> entries = {
        {u".", true},          {u"..", true},
        {u"alpha.txt", false}, {u"ALPHA.TXT", false},
        {u"alpha", false},     {u"sub", true},
        {u"SUB.EXT", true},    {u"配置😀", false},
        {u"<literal>", false}, {std::u16string(1, static_cast<char16_t>(0xd800)), false},
        {u".hidden", false},   {u"a.b.c", false}};
    if (index >= entries.size())
        return {};
    return entries[index++];
}
void Cursor::close() {
    if (!closed) {
        closed = true;
        p.event(4, "C");
    }
}
std::string run(const std::string& row) {
    std::istringstream in(row);
    std::string mode, p, e;
    int target;
    Probe probe;
    in >> mode >> target >> p >> e >> probe.point >> probe.nth;
    auto path = dec(p), expr = dec(e);
    std::string result;
    try {
        if (mode == "N") {
            auto n = normalize_directory_search(path, expr, platform());
            result = hex(*n.directory) + ":" + hex(*n.expression) + ":" + (n.directory == path ? "1" : "0") +
                     ":" + (n.expression == expr ? "1" : "0") + ":" +
                     (n.expression == file_system_name::star_literal() ? "1" : "0") + ":" +
                     (n.unchanged_directory ? "1" : "0");
        } else {
            std::shared_ptr<DirectoryEntriesBackend> backend;
            if (mode == "P") {
                backend = std::make_shared<DirectoryEntriesBackend>();
                backend->platform = platform();
                backend->full_path = [&](DirectoryEntriesText t) {
                    probe.event(1, "F:" + hex(*t));
                    return std::make_shared<const std::u16string>(
                        platform() == DirectoryEntriesPlatform::Windows ? u"X:\\normalized\\"
                                                                        : u"/normalized/");
                };
                backend->open = [&](DirectoryEntriesText t) {
                    probe.event(2, "O:" + hex(*t));
                    return std::make_shared<Cursor>(probe);
                };
            }
            auto entries = target == 2   ? directory_get_files(path)
                           : target == 3 ? directory_get_directories(path)
                           : target      ? directory_get_directories(path, expr, backend)
                                         : directory_get_files(path, expr, backend);
            result = "A";
            for (auto& entry : entries)
                result += ":" + hex(*entry);
        }
    } catch (const DirectoryEntriesError& e) {
        result = "EX:" + error(e);
    }
    if (mode == "P") {
        result += "/IO:";
        for (std::size_t i = 0; i < probe.trace.size(); ++i) {
            if (i)
                result += ';';
            result += probe.trace[i];
        }
    }
    return result;
}
void native_units() {
    auto folder = std::filesystem::temp_directory_path() /
                  ("directory-entries-native-" +
                   std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(folder))
        throw std::runtime_error("Cannot create owned directory fixture");
    struct Cleanup {
        std::filesystem::path root;
        ~Cleanup() {
            std::error_code e;
            for (auto name : {"alpha", "alpha.txt", ".hidden", "sub"})
                std::filesystem::remove(root / name, e);
            std::filesystem::remove(root, e);
        }
    } cleanup{folder};
    for (auto name : {"alpha", "alpha.txt", ".hidden"}) {
        std::ofstream file(folder / name);
        file << 'x';
    }
    std::filesystem::create_directory(folder / "sub");
    auto path = std::make_shared<const std::u16string>(folder.u16string());
    auto star = std::make_shared<const std::u16string>(u"*.*");
    auto files = directory_get_files(path, star);
    if (files.size() != 3 || directory_get_files(path).size() != 3)
        throw std::runtime_error("Win32 *.* compatibility must include extensionless/hidden files");
    auto dirs = directory_get_directories(path);
    if (dirs.size() != 1 || *dirs[0] != folder.u16string() +
                                            (platform() == DirectoryEntriesPlatform::Windows ? u"\\" : u"/") +
                                            u"sub")
        throw std::runtime_error("Default directory filter/projection");
    if (!directory_get_files(path, std::make_shared<const std::u16string>(u"sub/*")).empty())
        throw std::runtime_error("Pattern directory prefix");
    bool caught = false;
    try {
        directory_get_files(std::make_shared<const std::u16string>(
            (folder / std::filesystem::path(u"missing-配置😀")).u16string()));
    } catch (const std::exception& e) {
        caught = true;
        std::string message = e.what();
        if (message.find("Could not find a part of the path '") == std::string::npos ||
            message.find("\xe9\x85\x8d\xe7\xbd\xae\xf0\x9f\x98\x80") == std::string::npos)
            throw std::runtime_error("Directory OS failure what() must retain Unicode message");
    }
    if (!caught)
        throw std::runtime_error("Missing-directory default API must fail");
    Probe probe;
    auto backend = std::make_shared<DirectoryEntriesBackend>();
    backend->platform = platform();
    DirectoryEntriesText caller = std::make_shared<const std::u16string>(u"original");
    backend->full_path = [&](DirectoryEntriesText received) {
        if (*received != u"original")
            throw std::runtime_error("Argument snapshot changed");
        caller = std::make_shared<const std::u16string>(u"mutated");
        return std::make_shared<const std::u16string>(
            platform() == DirectoryEntriesPlatform::Windows ? u"X:\\root" : u"/root");
    };
    backend->open = [&](DirectoryEntriesText) { return std::make_shared<Cursor>(probe); };
    auto output = directory_get_files(caller, file_system_name::star_literal(), backend);
    if (output.empty() || *caller != u"mutated" || !output.front()->starts_with(u"original"))
        throw std::runtime_error("Provider callback mutated by-value directory argument");
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
        native_units();
        auto raw = dec("6100");
        auto n = normalize_directory_search(raw, dec("2a002e002a00"), platform());
        if (n.directory != raw || n.expression != file_system_name::star_literal())
            throw std::runtime_error("Directory normalization identity");
        std::cout << "PASS Directory default compatibility enumeration\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
