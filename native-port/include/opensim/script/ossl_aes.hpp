// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <string>
#include <string_view>

namespace opensim::script {
// Original Util AES-CBC format: uppercase IV hex, ':', lowercase ciphertext.
std::string ossl_aes_encrypt(std::string_view secret, std::string_view text, std::string_view iv = {});
std::string ossl_aes_decrypt(std::string_view secret, std::string_view text, std::string_view iv = {});
} // namespace opensim::script
