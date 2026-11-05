// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/status_date_format.hpp"
#include "opensim/services/flotsam_cache_console.hpp"
#include <iostream>
#include <stdexcept>
using namespace opensim;
int main(int argc, char** argv) {
    try {
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);
        if (argc > 1 && std::string(argv[1]) == "--probe") {
            std::uint64_t ticks;
            unsigned kind;
            while (std::cin >> ticks >> kind) {
                if (ticks > 3155378975999999999ull || kind > 3)
                    throw std::invalid_argument("The formatter fixture accepts only valid DateTime models");
                auto value = framework::AssetDateTime::from_serialized(ticks | (std::uint64_t(kind) << 62));
                std::cout << framework::format_invariant_status_date(value) << '\n';
            }
            return 0;
        }
        if (framework::format_invariant_status_date(framework::AssetDateTime(0, 0)) !=
                "01/01/0001 12:00:00" ||
            framework::format_invariant_status_date(framework::AssetDateTime(3155378975999999999ll, 2)) !=
                "12/31/9999 11:59:59")
            throw std::runtime_error("Status date full-range endpoints");
        services::FlotsamConsoleBackend console;
        auto provider = std::make_shared<framework::StatusDateFormatBackend>();
        bool invariant = true;
        int predicates = 0, fallbacks = 0;
        provider->is_invariant_culture = [&] {
            ++predicates;
            return invariant;
        };
        provider->current_culture_format = [&](auto date, const auto& pattern) {
            ++fallbacks;
            return std::to_string(date.serialized_data()) + ":" + pattern;
        };
        std::weak_ptr<framework::StatusDateFormatBackend> weak = provider;
        services::bind_flotsam_status_date_format(console, provider);
        provider.reset();
        if (weak.expired())
            throw std::runtime_error("Formatter binding lost provider ownership");
        auto date = framework::AssetDateTime(0, 1);
        if (console.format_date(date, "MM/dd/yyyy hh:mm:ss") != "01/01/0001 12:00:00" || predicates != 1 ||
            fallbacks)
            throw std::runtime_error("Invariant status fast path");
        invariant = false;
        if (console.format_date(date, "MM/dd/yyyy hh:mm:ss") !=
                std::to_string(date.serialized_data()) + ":MM/dd/yyyy hh:mm:ss" ||
            predicates != 2 || fallbacks != 1)
            throw std::runtime_error("Other culture was silently formatted invariant");
        invariant = true;
        console.format_date(date, "R");
        if (predicates != 2 || fallbacks != 2)
            throw std::runtime_error("Other pattern did not delegate its full ownership");
        auto retained = weak.lock();
        retained->current_culture_format = [](auto, const auto&) -> std::string {
            throw std::invalid_argument("provider format fault");
        };
        bool format_failed = false;
        try {
            console.format_date(date, "x");
        } catch (const std::invalid_argument& error) {
            format_failed = std::string(error.what()) == "provider format fault";
        }
        if (!format_failed)
            throw std::runtime_error("Provider format error was replaced or swallowed");
        retained->is_invariant_culture = [] {
            throw std::runtime_error("provider culture fault");
            return false;
        };
        bool culture_failed = false;
        try {
            console.format_date(date, "MM/dd/yyyy hh:mm:ss");
        } catch (const std::runtime_error& error) {
            culture_failed = std::string(error.what()) == "provider culture fault";
        }
        if (!culture_failed)
            throw std::runtime_error("Provider culture error was replaced or swallowed");
        retained.reset();
        console.format_date = {};
        if (!weak.expired())
            throw std::runtime_error("Formatter binding retained released provider");
        bool rejected = false;
        try {
            services::bind_flotsam_status_date_format(console, {});
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Missing formatter provider accepted");
        auto incomplete = std::make_shared<framework::StatusDateFormatBackend>();
        incomplete->is_invariant_culture = [] { return true; };
        rejected = false;
        try {
            services::bind_flotsam_status_date_format(console, incomplete);
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        if (!rejected)
            throw std::runtime_error("Invariant fast path accepted missing other-culture provider");
        std::cout << "PASS: invariant Gregorian status dates and explicit culture/provider ownership\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
