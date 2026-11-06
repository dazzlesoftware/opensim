// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/fs_asset_files.hpp"
#include "opensim/framework/sha256.hpp"
#include "opensim/framework/uuid.hpp"
#include "opensim/network/gzip.hpp"
#include "opensim/network/simulation_body.hpp"
#include <fstream>
#include <exception>
#include <iterator>
#include <stdexcept>

namespace opensim::services {
namespace {
namespace fs = std::filesystem;

bool valid_hash(std::string_view hash) {
    return !hash.empty() && hash.size() <= 80 &&
           hash.find_first_not_of("0123456789abcdefABCDEF") == std::string_view::npos;
}

std::optional<std::string> read_file(const fs::path& file) {
    std::ifstream stream(file, std::ios::binary);
    if (!stream)
        return std::nullopt;
    try {
        return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    } catch (const std::ios_base::failure&) {
        return std::nullopt;
    }
}

bool regular_file(const fs::path& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

void create_archive_path(const fs::path& base, const fs::path& relative) {
    fs::create_directories(base);
    auto directory = base;
    for (const auto& component : relative) {
        directory /= component;
        const auto status = fs::symlink_status(directory);
        if (fs::is_regular_file(status)) {
            // Some legacy stores have files where a shard directory belongs.
            // Move the obstruction aside in the same directory so its bytes survive.
            auto saved = directory;
            saved += ".legacy-" + framework::Uuid::random().str();
            fs::rename(directory, saved);
        } else if (fs::exists(status) && !fs::is_directory(status)) {
            throw std::runtime_error("Filesystem asset shard is not a regular directory");
        }
        fs::create_directory(directory);
    }
}

void publish(const fs::path& temporary_directory, const fs::path& destination, std::string_view bytes) {
    const auto temporary = temporary_directory / (framework::Uuid::random().str() + ".tmp");
    try {
        std::ofstream stream(temporary, std::ios::binary);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        if (!bytes.empty())
            stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        fs::rename(temporary, destination);
    } catch (...) {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
}
}

FsAssetFiles::FsAssetFiles(fs::path base, fs::path spool, bool osgrid_layout)
    : base_(std::move(base)), spool_(std::move(spool)), osgrid_layout_(osgrid_layout) {
    if (base_.empty() || spool_.empty())
        throw std::invalid_argument("Filesystem assets require a base directory and spool directory");
    fs::create_directories(spool_ / "spool");
}

std::string FsAssetFiles::digest(std::string_view bytes) {
    auto hash = framework::sha256_hex(bytes);
    for (auto& digit : hash)
        if (digit >= 'a' && digit <= 'f')
            digit -= 'a' - 'A';
    return hash;
}

fs::path FsAssetFiles::relative_path(std::string_view hash) const {
    if (!valid_hash(hash))
        throw std::invalid_argument("Invalid filesystem asset digest");
    if (hash.size() < 10)
        return fs::path("junkyard") / hash;
    if (osgrid_layout_)
        return fs::path(hash.substr(0, 3)) / hash.substr(3, 3) / hash;
    return fs::path(hash.substr(0, 2)) / hash.substr(2, 2) / hash.substr(4, 2) / hash.substr(6, 4) / hash;
}

fs::path FsAssetFiles::spool_path(std::string_view hash) const {
    return spool_ / (std::string(hash) + ".asset");
}

std::optional<std::string> FsAssetFiles::read(std::string_view hash) const {
    if (!valid_hash(hash))
        return std::nullopt;
    const auto raw = base_ / relative_path(hash);
    if (auto bytes = read_file(spool_path(hash)))
        return bytes;
    auto compressed = raw;
    compressed += ".gz";
    if (regular_file(compressed)) {
        const auto bytes = read_file(compressed);
        if (!bytes)
            return std::nullopt;
        try {
            return network::inflate_gzip(*bytes);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return read_file(raw);
}

bool FsAssetFiles::stage(std::string_view hash, std::string_view bytes) const {
    const auto raw = base_ / relative_path(hash);
    auto compressed = raw;
    compressed += ".gz";
    const auto staged = spool_path(hash);
    if (regular_file(raw) || regular_file(compressed) || regular_file(staged))
        return false;
    try {
        publish(spool_ / "spool", staged, bytes);
        return true;
    } catch (const fs::filesystem_error&) {
        if (!regular_file(staged) && !regular_file(compressed))
            throw;
        return false;
    }
}

std::size_t FsAssetFiles::flush() const {
    std::size_t count = 0;
    std::exception_ptr first_failure;
    for (const auto& file : fs::directory_iterator(spool_)) {
        if (!file.is_regular_file() || file.path().extension() != ".asset")
            continue;
        const auto hash = file.path().stem().string();
        if (!valid_hash(hash))
            continue;
        const auto bytes = read_file(file.path());
        if (!bytes)
            continue;
        try {
            const auto relative = relative_path(hash);
            auto target = base_ / relative;
            target += ".gz";
            create_archive_path(base_, relative.parent_path());
            publish(target.parent_path(), target, network::gzip_compress(*bytes));
            if (fs::remove(file.path()))
                ++count;
        } catch (...) {
            if (!first_failure)
                first_failure = std::current_exception();
        }
    }
    if (first_failure)
        std::rethrow_exception(first_failure);
    return count;
}
}
