// Compile-only audit of real LSL calls. Never executes scripts or touches a region.
#include "opensim/script/lsl_runtime.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

int main(int argc, char** argv) {
    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index]);
        if (!file) {
            std::cerr << "Cannot read " << argv[index] << '\n';
            return 2;
        }
        std::ostringstream source;
        source << file.rdbuf();
        const auto result = opensim::script::compile_lsl(source.str());
        std::cout << argv[index] << '\t' << (result ? "accepted" : "rejected");
        for (const auto& diagnostic : result.diagnostics)
            std::cout << '\t' << diagnostic.line << ':' << diagnostic.column << ' ' << diagnostic.message;
        std::cout << '\n';
    }
}
