// Source Marvin UTF16 hashing and process entropy, .NET8.0.31.
// Extracted from the previously verified native cookie hash implementation.
#include "opensim/framework/dotnet_string_hash.hpp"
#include <bit>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <cstdlib>
#else
#include <cerrno>
#include <sys/random.h>
#endif
namespace opensim::framework {
namespace {
void block(std::uint32_t &p0, std::uint32_t &p1) {
    p1 ^= p0;
    p0 = std::rotl(p0, 20);
    p0 += p1;
    p1 = std::rotl(p1, 9);
    p1 ^= p0;
    p0 = std::rotl(p0, 27);
    p0 += p1;
    p1 = std::rotl(p1, 19);
}

} // namespace

std::uint32_t dotnet_string_hash(std::u16string_view input, std::uint64_t seed) noexcept {
    std::uint32_t p0 = static_cast<std::uint32_t>(seed), p1 = static_cast<std::uint32_t>(seed >> 32);
    std::size_t i = 0;
    for (; i + 1 < input.size(); i += 2) {
        p0 += std::uint32_t(input[i]) | (std::uint32_t(input[i + 1]) << 16);
        block(p0, p1);
    }
    p0 += i < input.size() ? std::uint32_t(input[i]) | 0x800000u : 0x80u;
    block(p0, p1);
    block(p0, p1);
    return p0 ^ p1;
}

std::uint64_t dotnet_string_hash_seed() {
    static const auto seed = []() {
        std::uint64_t value{};
#ifdef _WIN32
        if (BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&value), sizeof(value),
                            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
            throw std::runtime_error("Native cookie hash entropy initialization failed");
#elif defined(__APPLE__)
        arc4random_buf(&value, sizeof(value));
#else
        auto pointer = reinterpret_cast<std::uint8_t *>(&value);
        std::size_t size = 0;
        while (size < sizeof(value)) {
            auto n = getrandom(pointer + size, sizeof(value) - size, 0);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                throw std::runtime_error("Native cookie hash entropy initialization failed");
            size += n;
        }
#endif
        return value;
    }();
    return seed;
}

std::uint32_t dotnet_string_hash(std::u16string_view input) {
    return dotnet_string_hash(input, dotnet_string_hash_seed());
}
} // namespace opensim::framework
