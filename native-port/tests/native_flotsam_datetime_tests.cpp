// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/native_flotsam_datetime.hpp"
#include "opensim/services/flotsam_console_files.hpp"
#include "opensim/services/flotsam_file_access.hpp"
#include <iostream>
#include <vector>
using namespace opensim;
using namespace services;
namespace {
void check(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Output : FlotsamConsoleOutput {
    std::vector<std::string> lines;
    void output(const std::string& s) override {
        lines.push_back(s);
    }
};
std::optional<std::string> decode(const std::string& value) {
    if (value == "~")
        return {};
    std::string out;
    for (std::size_t i = 0; i < value.size(); i += 2)
        out += static_cast<char>(std::stoi(value.substr(i, 2), nullptr, 16));
    return out;
}
void unit() {
    auto first = framework::shared_native_invariant_datetime_context();
    check(first == framework::shared_native_invariant_datetime_context(),
          "default runtime owner must be shared");
    auto backend = std::make_shared<FlotsamConsoleBackend>();
    auto owner = std::make_shared<framework::NativeInvariantDateTimeContext>();
    std::weak_ptr weak = owner;
    bind_native_invariant_flotsam_datetime(*backend, owner);
    owner.reset();
    check(!weak.expired(), "bound callbacks retain context");
    framework::AssetDateTime date;
    check(backend->try_parse("2024-01-01", date) && date.ticks() == 638396640000000000ll && date.kind() == 0,
          "full invariant parser binding");
    check(!backend->try_parse("definitely invalid", date), "malformed date rejected");
    check(backend->equals_now("NOW"), "invariant now comparison");
    backend->now = [] { return framework::AssetDateTime(638712864000000000ll, 2); };
    auto config = std::make_shared<FlotsamInstanceConfig>();
    config->file = false;
    auto life = std::make_shared<FlotsamCacheLifecycle>();
    auto output = std::make_shared<Output>();
    auto expire = [&](std::initializer_list<FlotsamConsoleBackend::Text> args) {
        FlotsamCacheConsole::expire(std::make_shared<FlotsamCacheConsole::Arguments>(args), config, life,
                                    backend, output);
    };
    expire({"fcache", "expire", "nonsense"});
    check(!life->cleanup_running && output->lines.back() == "expire is not a valid date & time",
          "invalid native parse releases source gate");
    expire({"fcache", "expire", "2030-01-01"});
    check(!life->cleanup_running && output->lines.back() == "expire date & time must be in past",
          "future rejection source gate");
    expire({"fcache", "expire", "2024-01-01"});
    check(life->cleanup_running && output->lines.back() == "File cache not active, not clearing.",
          "disabled-file source successful expire retains gate");
    expire({"fcache", "expire", "now"});
    check(output->lines.back() == "Flotsam assets check already running",
          "disabled-file gate blocks later commands");
    backend.reset();
    check(weak.expired(), "callbacks release context after backend destruction");
    owner = std::make_shared<framework::NativeInvariantDateTimeContext>();
    weak = owner;
    auto access = native_flotsam_access_time_setter(owner);
    owner.reset();
    check(!weak.expired(), "access timestamp setter retains same owner");
    access = {};
    check(weak.expired(), "access timestamp setter releases owner");
    auto stamps = std::make_shared<FlotsamConsoleBackend>();
    owner = std::make_shared<framework::NativeInvariantDateTimeContext>();
    weak = owner;
    bind_flotsam_console_files(*stamps, owner);
    owner.reset();
    check(!weak.expired(), "timestamp backend shares retained owner");
    stamps.reset();
    check(weak.expired(), "timestamp callbacks release owner");
}
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            FlotsamConsoleBackend b;
            bind_native_invariant_flotsam_datetime(b);
            for (std::string line; std::getline(std::cin, line);) {
                auto value = decode(line);
                framework::AssetDateTime date;
                bool parsed = b.try_parse(value, date);
                std::cout << parsed << '|' << date.serialized_data() << '|' << b.equals_now(value) << '\n'
                          << std::flush;
            }
            return 0;
        }
        unit();
        std::cout << "PASS: native Flotsam date binding, shared owner and source expire gates\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
