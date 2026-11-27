// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include "opensim/script/lsl_runtime.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/network/osd_value.hpp"
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <stdexcept>

namespace event_helper_reference {
inline void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}

inline std::string read_source(const std::filesystem::path &path) {
    std::ifstream stream(path, std::ios::binary);
    require(bool(stream), "Missing original source oracle: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

inline std::map<std::string, std::size_t> globals_in_source(const std::string &source) {
    // These exact reference sources declare globals before default. A user
    // function may follow those declarations; its return and parameter types
    // have no assignment/semicolon and therefore do not become global slots.
    const auto prefix = source.substr(0, source.find("default"));
    const std::regex declaration(
        R"(\b(?:integer|float|string|key|vector|rotation|list)\s+([A-Za-z_][A-Za-z_0-9]*)\s*(?:=|;))");
    std::map<std::string, std::size_t> result;
    std::size_t index = 0;
    for (auto match = std::sregex_iterator(prefix.begin(), prefix.end(), declaration); match != std::sregex_iterator();
         ++match)
        result.emplace((*match)[1].str(), index++);
    return result;
}

template <class Compare>
void compare_globals(const opensim::script::LslCheckpoint &checkpoint, const opensim::network::Value &expected,
                     const std::map<std::string, std::size_t> &indices, Compare compare) {
    using namespace opensim::script;
    require(checkpoint.globals.size() == indices.size() && expected.map.size() == indices.size(),
            "Original source oracle global layout changed");
    for (const auto &[name, index] : indices) {
        const auto found = expected.map.find(name);
        require(found != expected.map.end(), "Original source oracle omitted global " + name);
        const auto &actual = checkpoint.globals.at(index);
        const auto &clr = found->second.get("clr").text;
        if (clr == "System.Int32")
            require(std::holds_alternative<std::int32_t>(actual), "Integer scalar global became a boxed list value");
        if (clr == "System.Double")
            require(std::holds_alternative<double>(actual), "Float scalar global became a boxed list value");
        if (clr == "System.String")
            require(std::holds_alternative<std::string>(actual), "String/key scalar global became a boxed list value");
        compare(actual, found->second);
    }
}

template <class Compare>
void verify_sources(const opensim::network::Value &oracle, const std::filesystem::path &fixture, Compare compare) {
    using namespace opensim;
    using namespace script;
    const auto &cases = oracle.get("script_compile_cases").array;
    require(cases.size() == 14, "Expected all fourteen actual original source cases");
    std::string owner;
    for (const auto &test : cases)
        if (test.get("name").text == "key_direct_api_list") {
            const auto &items = test.get("globals").get("output").get("value").array;
            require(items.size() == 1, "Original owner API control is missing");
            owner = items.front().get("value").text;
        }
    require(!owner.empty(), "Original source oracle has no owner environment binding");
    unsigned keys = 0, nested = 0, faults = 0;
    for (const auto &test : cases) {
        const auto name = test.get("name").text;
        try {
            const auto relative = std::filesystem::path(test.get("source_file").text);
            require(!relative.is_absolute() && !relative.empty() && relative.begin()->string() == "script_sources" &&
                        relative.filename() == relative.filename().lexically_normal() &&
                        relative.lexically_normal() == relative,
                    "Invalid original source oracle path");
            const auto source = read_source(fixture / relative);
            require(source == test.get("source").text &&
                        framework::sha256_hex(source) == oracle.get("sha256").get(relative.generic_string()).text,
                    "Exact original helper source or its provenance changed");
            const auto compiled = compile_lsl(source);
            const bool accepted = network::osd_boolean(test.get("accepted"));
            require(bool(compiled) == accepted, "Native/original helper source acceptance differs");
            if (!accepted)
                continue;
            require(network::osd_boolean(test.get("execution_requested")) &&
                        test.get("compile_exception").kind != network::Value::Kind::Map,
                    "Original source case lacks actual generated execution evidence");
            LslContext context;
            context.object = framework::Uuid::random();
            context.item = framework::Uuid::random();
            context.owner = framework::Uuid(owner);
            LslHost host;
            host.clock_ticks = host.monotonic_ticks = [] { return std::int64_t{638000000000000000LL}; };
            LslInstance instance(compiled.program, context, host);
            const auto indices = globals_in_source(source);
            const bool expected_fault = test.get("execution_exception").kind == network::Value::Kind::Map;
            bool finished = false;
            for (unsigned step = 0; step < 10000; ++step) {
                const auto before = instance.checkpoint();
                require(bool(before), "Source VM lost checkpoint before its expected outcome");
                const auto progress = instance.run(1);
                if (progress.status == LslRunStatus::Faulted) {
                    require(expected_fault, "Native helper faulted where original source completed: " + progress.error);
                    require(test.get("execution_exception").get("type").text == "System.InvalidCastException" &&
                                progress.error.find("cast") != std::string::npos,
                            "Native failure does not match the original helper type-cast fault");
                    // The failing LoadDets expression makes no global stores.
                    // The original CallSEH oracle records output=[]/phase=0;
                    // faulted native VMs intentionally expose no checkpoint.
                    compare_globals(*before, test.get("globals"), indices, compare);
                    ++faults;
                    finished = true;
                    break;
                }
                require(progress.status != LslRunStatus::Sleeping, "Pure original source case unexpectedly slept");
                if (progress.status == LslRunStatus::Idle) {
                    require(!expected_fault, "Native helper returned where the original source faulted");
                    compare_globals(*instance.checkpoint(), test.get("globals"), indices, compare);
                    finished = true;
                    break;
                }
            }
            require(finished, "Original source helper execution exceeded its bounded instruction count");
            keys += name.starts_with("key_");
            nested += name == "nested_output";
        } catch (const std::exception &error) {
            throw std::runtime_error("Original source " + name + ": " + error.what());
        }
    }
    require(keys == 10 && nested == 1 && faults == 3,
            "Original key boxing/nested-list/type-fault source coverage unexpectedly shrank");
}
} // namespace event_helper_reference
