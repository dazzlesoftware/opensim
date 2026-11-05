// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/gzip.hpp"
#include "opensim/network/simulation_body.hpp"
#include <iostream>
#include <future>
#include <vector>
#include <stdexcept>
std::string hex(std::string_view s) {
    std::string out;
    const char* h = "0123456789abcdef";
    for (unsigned char c : s) {
        out += h[c >> 4];
        out += h[c & 15];
    }
    return out;
}
std::string decode(std::string_view s) {
    if (s == ".")
        return {};
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2)
        out += static_cast<char>(std::stoi(std::string(s.substr(i, 2)), nullptr, 16));
    return out;
}
int main(int argc, char** argv) {
    if (argc > 1) {
        std::string line;
        std::getline(std::cin, line);
        auto data = decode(line);
        std::promise<void> start;
        auto ready = start.get_future().share();
        std::vector<std::future<std::string>> jobs;
        for (int i = 0; i < 16; ++i)
            jobs.push_back(std::async(std::launch::async, [&data, ready] {
                ready.wait();
                return opensim::network::gzip_compress(data);
            }));
        start.set_value();
        for (auto& job : jobs)
            std::cout << hex(job.get()) << "\n";
        return 0;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            auto value = decode(line.substr(2));
            std::cout << hex(line[0] == 'C' ? opensim::network::gzip_compress(value)
                                            : opensim::network::inflate_gzip(value))
                      << "\n";
        } catch (const std::exception&) {
            std::cout << "EX\n";
        }
    }
}
