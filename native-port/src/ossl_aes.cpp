// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/script/ossl_aes.hpp"
#include "opensim/framework/identity.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/network/service_protocol.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif

namespace opensim::script {
namespace {
std::vector<unsigned char> unhex(std::string_view value) {
    if (value.size() % 2)
        throw std::invalid_argument("Invalid AES hex length");
    const auto digit = [](char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        throw std::invalid_argument("Invalid AES hex character");
    };
    std::vector<unsigned char> result;
    result.reserve(value.size() / 2);
    for (std::size_t i = 0; i < value.size(); i += 2)
        result.push_back(static_cast<unsigned char>((digit(value[i]) << 4) | digit(value[i + 1])));
    return result;
}

std::string hex(const std::vector<unsigned char> &bytes, bool upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
        result.push_back(digits[byte >> 4]);
        result.push_back(digits[byte & 15]);
    }
    return result;
}

std::string utf8(std::string_view value) {
    return framework::utf8(framework::utf16_replacing(value));
}

std::string sdk_bytes(std::string_view value) {
    if (value.empty())
        return {};
    auto bytes = utf8(value);
    if (!bytes.empty() && bytes.back() == '\0')
        bytes.pop_back();
    const auto zero = bytes.find('\0');
    if (zero != std::string::npos)
        std::fill(bytes.begin() + zero, bytes.end(), '\0');
    return bytes;
}

std::vector<unsigned char> crypt(bool encrypt, std::string_view secret, const std::vector<unsigned char> &iv,
                                 const std::vector<unsigned char> &input) {
    if (iv.size() != 16)
        throw std::invalid_argument("Invalid AES initialization vector size");
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) - 16)
        throw std::length_error("AES input is too large");
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    const auto key = unhex(framework::sha256_hex(sdk_bytes(secret)));
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> context(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!context || EVP_CipherInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, key.data(), iv.data(), encrypt) != 1 ||
        EVP_CIPHER_CTX_set_padding(context.get(), 1) != 1)
        throw std::runtime_error("AES cipher initialization failed");
    if (!encrypt && input.empty())
        return {};
    std::vector<unsigned char> result(input.size() + 16);
    int produced = 0, final = 0;
    if (EVP_CipherUpdate(context.get(), result.data(), &produced, input.data(), static_cast<int>(input.size())) != 1 ||
        EVP_CipherFinal_ex(context.get(), result.data() + produced, &final) != 1)
        throw std::runtime_error("Invalid AES ciphertext or padding");
    result.resize(static_cast<std::size_t>(produced + final));
    return result;
#else
    throw std::runtime_error("OSSL AES requires an OpenSSL-enabled native build");
#endif
}
} // namespace

std::string ossl_aes_encrypt(std::string_view secret, std::string_view text, std::string_view iv_string) {
    if (secret.empty() || text.empty())
        return {};
    std::vector<unsigned char> iv;
    if (iv_string.empty()) {
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
        std::array<unsigned char, 16> uuid;
        if (RAND_bytes(uuid.data(), static_cast<int>(uuid.size())) != 1)
            throw std::runtime_error("AES random UUID generation failed");
        uuid[6] = (uuid[6] & 15) | 64;
        uuid[8] = (uuid[8] & 63) | 128;
        iv = unhex(framework::md5(std::string_view(reinterpret_cast<const char *>(uuid.data()), uuid.size())));
#else
        throw std::runtime_error("OSSL AES requires an OpenSSL-enabled native build");
#endif
    } else
        iv = unhex(framework::md5(sdk_bytes(iv_string)));
    const auto plain = utf8(text);
    return hex(iv, true) + ':' + hex(crypt(true, secret, iv, {plain.begin(), plain.end()}), false);
}

std::string ossl_aes_decrypt(std::string_view secret, std::string_view text, std::string_view iv_string) {
    if (secret.empty() || text.empty())
        return {};
    const auto separator = text.find(':');
    if (separator == std::string_view::npos)
        return {};
    std::vector<unsigned char> iv, input;
    try {
        iv = unhex(iv_string.empty() ? std::string(text.substr(0, separator)) : framework::md5(sdk_bytes(iv_string)));
        input = unhex(text.substr(separator + 1));
    } catch (const std::invalid_argument &) {
        return {};
    }
    const auto plain = crypt(false, secret, iv, input);
    if (plain.empty())
        return {};
    return network::read_service_body(std::string_view(reinterpret_cast<const char *>(plain.data()), plain.size()));
}
} // namespace opensim::script
