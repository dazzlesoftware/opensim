// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/installed_fonts.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/network/osd_json.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace opensim;

namespace {
void check(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

std::string read(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    check(bool(stream), "Missing original font reference input: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void hash(const std::filesystem::path &path, const std::string &expected) {
    check(expected.size() == 64 && framework::sha256_hex(read(path)) == expected,
          "Original font provenance SHA mismatch: " + path.string());
}

std::uint16_t language(const network::Value &value) {
    const auto lcid = network::osd_integer(value);
    check(lcid >= 0 && lcid <= 65535, "Original LANGID exceeds this provider corpus");
    return static_cast<std::uint16_t>(lcid);
}

void equal_names(const std::vector<std::string> &actual, const std::vector<std::string> &expected, std::uint16_t lcid,
                 const std::string &phase) {
    check(actual.size() == expected.size(), phase + ": ordered font count differs for LANGID " + std::to_string(lcid) +
                                                "; native=" + std::to_string(actual.size()) +
                                                " original=" + std::to_string(expected.size()));
    for (std::size_t i = 0; i < expected.size(); ++i)
        check(actual[i] == expected[i], phase + ": font " + std::to_string(i) + " differs for LANGID " +
                                            std::to_string(lcid) + "; native=" + actual[i] +
                                            " original=" + expected[i]);
}

void validate_original(const network::Value &proof, const std::filesystem::path &source_root) {
    check(proof.get("method").text == "xmrFontsAvailable" && proof.get("exception").kind != network::Value::Kind::Map &&
              proof.get("independent_exception").kind != network::Value::Kind::Map,
          "Original font API/enumeration was unavailable; this is not an empty-list oracle");
    check(proof.get("independent_order_matches_method").boolean,
          "Original SDK method and independent FontFamily order differ");
    const auto &source = proof.get("original_source");
    hash(source_root / source.get("path").text, source.get("sha256").text);
    hash(source_root / "bin/OpenSim.Region.ScriptEngine.YEngine.dll",
         proof.get("original_yengine_assembly_sha256").text);
#ifdef _WIN32
    hash(source_root / "native-port/build-https/fonts-available-reference/FontsReference.cs",
         proof.get("generator_sha256").text);
    check(proof.get("platform").text.starts_with("Microsoft Windows"),
          "Windows provider requires the actual Windows original proof");
#else
    hash(source_root / "native-port/build-https/fonts-linux-reference/FontsLinuxReference.cs",
         proof.get("generator_sha256").text);
    check(proof.get("platform").text.starts_with("Unix"), "Linux provider requires the actual Linux original proof");
    check(!proof.get("original_production_prelude").text.empty(),
          "Original Linux Unix-support/platform assembly selection provenance missing");
#endif
    check(proof.get("registered").array.size() == 1 &&
              proof.get("registered").array[0].get("signature").text == "xmrFontsAvailable()",
          "Original compiler API registration changed");
    for (const auto &assembly : proof.get("drawing_assemblies").array) {
        auto filename = assembly.get("file").text;
#ifndef _WIN32
        // Original Application.cs selects .linux into an isolated default DLL;
        // the workspace's default DLL remains its Windows original assembly.
        if (filename == "System.Drawing.Common.dll")
            filename += ".linux";
#endif
        hash(source_root / "bin" / filename, assembly.get("sha256").text);
        check(!assembly.get("assembly_version").text.empty(), "Original Drawing assembly version missing");
    }
    check(!proof.get("loaded_drawing_path").text.empty() && !proof.get("framework").text.empty(),
          "Original runtime/library provenance missing");
}
#ifndef _WIN32
std::map<std::string, std::string> installed_packages() {
    std::map<std::string, std::string> result;
    std::istringstream input(read("/var/lib/dpkg/status"));
    std::string line, package, version;
    const auto flush = [&] {
        if (!package.empty() && !version.empty())
            result[package] = version;
        package.clear();
        version.clear();
    };
    while (std::getline(input, line)) {
        if (line.empty()) {
            flush();
            continue;
        }
        if (line.starts_with("Package: "))
            package = line.substr(9);
        if (line.starts_with("Version: "))
            version = line.substr(9);
    }
    flush();
    return result;
}

void validate_linux_environment(const std::filesystem::path &environment_path, const std::filesystem::path &proof_path,
                                const std::filesystem::path &source_root) {
    const auto env = network::parse_osd_json(read(environment_path));
    const char *image = std::getenv("OPENSIM_FONT_REFERENCE_IMAGE_ID");
    check(image && env.get("reference_image_id").text == image,
          "Linux font matching must run in the recorded immutable original-grid image; orchestrator must provide its "
          "actual inspected image ID");
    check(env.get("reference_image").text == "opensim-native-original-grid:ubuntu24.04",
          "Unreviewed original Linux image");
    check(read("/etc/os-release") == env.get("os_release").text, "Linux reference OS release differs");
    hash(proof_path, env.get("original_font_method_evidence_sha256").text);
    hash(source_root / "bin/System.Drawing.Common.dll.linux", env.get("selected_drawing_linux_sha256").text);
    const auto packages = installed_packages();
    std::istringstream declared(env.get("packages").text);
    std::string line;
    bool gdiplus = false, fontconfig = false;
    while (std::getline(declared, line)) {
        const auto tab = line.find('\t');
        check(tab != std::string::npos, "Malformed original package provenance");
        auto name = line.substr(0, tab);
        const auto architecture = name.find(':');
        if (architecture != std::string::npos)
            name.resize(architecture);
        const auto found = packages.find(name);
        check(found != packages.end() && found->second == line.substr(tab + 1),
              "Linux original font library/package version differs: " + name);
        gdiplus |= name == "libgdiplus";
        fontconfig |= name == "libfontconfig1";
    }
    check(gdiplus && fontconfig && !env.get("runtime").text.empty(),
          "Original fontconfig/libgdiplus/.NET provenance absent");
    check(!env.get("font_files_sha256").map.empty(), "Original font inputs were not fingerprinted");
    for (const auto &[path, digest] : env.get("font_files_sha256").map)
        hash(path, digest.text);
    // Extra or user-installed fonts still cause the exact family/order assertion
    // to fail; matching a subset of input hashes is never used as a skip rule.
}
#endif
void script_fonts(const std::vector<std::string> &expected, std::uint16_t ui, bool callback) {
    const auto compiled =
        script::compile_lsl("list fonts;integer calls;default{state_entry(){fonts=xmrFontsAvailable();calls++;}}");
    check(bool(compiled), "Actual xmrFontsAvailable source failed native compilation");
    unsigned backend_calls = 0;
    script::LslHost host;
    if (callback)
        host.fonts_available = [&] {
            ++backend_calls;
            return framework::installed_font_families(ui);
        };
    script::LslInstance vm(compiled.program, {}, host);
    bool done = false;
    for (unsigned step = 0; step < 1000; ++step) {
        const auto run = vm.run(1);
        check(run.status != script::LslRunStatus::Faulted, "Real font source faulted: " + run.error);
        check(run.status != script::LslRunStatus::Sleeping, "Font source unexpectedly requested sleep");
        const auto saved = vm.checkpoint();
        check(saved.has_value(), "Font source checkpoint unavailable");
        const auto decoded = script::decode_lsl_checkpoint(script::encode_lsl_checkpoint(*saved));
        script::LslInstance fresh(compiled.program, {}, host);
        check(decoded && fresh.restore_checkpoint(*decoded), "Font source budget checkpoint failed fresh-VM restore");
        vm = std::move(fresh);
        if (run.status == script::LslRunStatus::Idle) {
            done = true;
            break;
        }
    }
    check(done, "Font source did not finish within bounded execution");
    const auto compare = [&](script::LslInstance &instance) {
        const auto saved = instance.checkpoint();
        check(saved && saved->globals.size() == 2, "Font source global scope differs");
        const auto *list = std::get_if<std::shared_ptr<const script::LslList>>(&saved->globals[0]);
        check(list && *list && (*list)->items.size() == expected.size(), "Source font list count differs");
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const auto *name = std::get_if<script::LslBoxedString>(&(*list)->items[i]);
            check(name && name->value == expected[i],
                  "Source font list lost ordered original LSLString identity at " + std::to_string(i));
        }
        const auto *calls = std::get_if<std::int32_t>(&saved->globals[1]);
        check(calls && *calls == 1, "Font source counter replayed");
    };
    compare(vm);
    if (callback)
        check(backend_calls == 1, "Real provider callback replayed across internal budget checkpoint");
    const auto completed = vm.checkpoint();
    const auto decoded = script::decode_lsl_checkpoint(script::encode_lsl_checkpoint(*completed));
    script::LslInstance restored(compiled.program, {}, host);
    check(decoded && restored.restore_checkpoint(*decoded), "Completed font source failed checkpoint restore");
    for (auto *instance : {&vm, &restored}) {
        check(instance->run(1).status == script::LslRunStatus::Idle, "Completed font checkpoint restarted source");
        compare(*instance);
    }
    if (callback)
        check(backend_calls == 1, "Completed font checkpoint replayed real provider callback");
}

void provider_failure_control() {
    try {
        const auto result = framework::installed_font_families(127);
        throw std::logic_error("Expected a deliberately unavailable provider, but enumeration succeeded with " +
                               std::to_string(result.size()) + " families; an empty result is not a failure signal");
    } catch (const std::runtime_error &error) {
        const std::string message = error.what();
        check(message.find("font provider") != std::string::npos || message.starts_with("Gdip"),
              "Unexpected provider failure category: " + message);
        std::cout << "PASS: explicit installed-font provider error (no empty-list fallback): " << message << '\n';
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--expect-provider-error") {
            provider_failure_control();
            return 0;
        }
        check(argc == 3 || argc == 4,
              "Original font proof, original source root and Linux reference environment proof required");
        const std::filesystem::path proof_path = argv[1], source_root = argv[2];
        const auto proof = network::parse_osd_json(read(proof_path));
        validate_original(proof, source_root);
#ifndef _WIN32
        check(argc == 4, "Linux original font proof requires its immutable image/library/font-file environment");
        validate_linux_environment(argv[3], proof_path, source_root);
#endif
        std::vector<std::string> expected;
        for (const auto &entry : proof.get("returned").array) {
            check(entry.get("clr").text == "OpenSim.Region.ScriptEngine.Shared.LSL_Types+LSLString" &&
                      network::osd_integer(entry.get("index")) == static_cast<std::int32_t>(expected.size()),
                  "Original font order or wrapper identity changed");
            expected.push_back(entry.get("value").text);
        }
        check(expected.size() == static_cast<std::size_t>(network::osd_integer(proof.get("font_count"))),
              "Original font count mismatch");
        const auto ui = language(proof.get("current_ui_culture").get("lcid"));
        equal_names(framework::installed_font_families(ui), expected, ui, "Actual original API UI language");
        const auto &families = proof.get("independent_families").array;
        check(families.size() == expected.size(), "Original independent family count changed");
        std::set<std::uint16_t> tested;
        if (!families.empty())
            for (const auto &requested : families[0].get("get_names").array) {
                const auto lcid = language(requested.get("lcid"));
                std::vector<std::string> localized;
                for (const auto &family : families) {
                    const network::Value *entry = nullptr;
                    for (const auto &candidate : family.get("get_names").array)
                        if (language(candidate.get("lcid")) == lcid) {
                            entry = &candidate;
                            break;
                        }
                    check(entry && entry->get("exception").kind != network::Value::Kind::Map,
                          "Original GetName did not establish this language");
                    localized.push_back(entry->get("name").text);
                }
                equal_names(framework::installed_font_families(lcid), localized, lcid, "Original GetName language");
                tested.insert(lcid);
            }
        // Verify Name's UI-culture behavior independently from explicit GetName,
        // including localized Windows family names and return-to-original calls.
        if (!families.empty())
            for (std::size_t profile = 0; profile < families[0].get("name_property_under_ui_cultures").array.size();
                 ++profile) {
                const auto lcid =
                    language(families[0].get("name_property_under_ui_cultures").array[profile].get("lcid"));
                std::vector<std::string> localized;
                for (const auto &family : families)
                    localized.push_back(
                        family.get("name_property_under_ui_cultures").array.at(profile).get("name").text);
                equal_names(framework::installed_font_families(lcid), localized, lcid,
                            "Original Name UI-culture profile");
            }
        equal_names(framework::installed_font_families(ui), expected, ui, "Repeated call after language changes");
        script_fonts(expected, ui, true);
        // Default binding chooses the actual native UI language. Its expected
        // list comes from that real provider, not an unrelated proof language.
        const auto default_ui = framework::installed_font_ui_language();
        const auto default_names = framework::installed_font_families(default_ui);
        script_fonts(default_names, default_ui, false);
        std::cout << "PASS: " << expected.size() << " exact ordered original family names, " << tested.size()
                  << " language IDs, UI profiles and source/library/environment provenance\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
