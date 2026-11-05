// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/framework/glob.hpp"
namespace opensim::framework {
namespace {
std::vector<std::u16string> split(const std::u16string &text, char16_t a, char16_t b, bool two = false) {
    std::vector<std::u16string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && (text[i] == a || text[i] == b))
            ++i;
        if (i == text.size())
            break;
        if (two && out.size() == 1) {
            out.push_back(text.substr(i));
            break;
        }
        auto end = i;
        while (end < text.size() && text[end] != a && text[end] != b)
            ++end;
        out.push_back(text.substr(i, end - i));
        i = end;
    }
    return out;
}
DirectoryEntriesText text(const std::u16string &value) {
    return std::make_shared<const std::u16string>(value);
}
}
std::vector<std::u16string> glob(const std::optional<std::u16string> &input, const GlobBackend &backend) {
    if (!input)
        throw GlobNullInputError();
    auto path = *input;
    std::u16string volume;
    if (backend.volume_separator != backend.directory_separator) {
        auto parts = split(path, backend.volume_separator, backend.volume_separator, true);
        if (parts.size() > 1) {
            path = parts[1];
            volume = parts[0];
        }
    }
    auto components = split(path, backend.directory_separator, backend.alternate_separator);
    path = volume;
    if (!volume.empty()) {
        path += backend.volume_separator;
        path += backend.directory_separator;
    } else
        path.assign(1, backend.directory_separator);
    std::vector<std::u16string> paths{path}, found;
    for (std::size_t index = 0; index < components.size(); ++index) {
        const auto &component = components[index];
        std::vector<std::u16string> added;
        for (const auto &current : paths) {
            auto directories = backend.directories(current, component);
            for (const auto &directory : directories)
                added.push_back(backend.combine(path, directory));
            if (index == components.size() - 1) {
                auto files = backend.files(current, component);
                found.insert(found.end(), files.begin(), files.end());
            }
        }
        paths = std::move(added);
    }
    return found;
}
GlobBackend native_glob_backend(std::shared_ptr<DirectoryEntriesBackend> backend) {
    if (!backend)
        backend = native_directory_entries_backend();
    GlobBackend result;
    const bool windows = backend->platform == DirectoryEntriesPlatform::Windows;
    if (!windows) {
        result.volume_separator = u'/';
        result.directory_separator = u'/';
        result.alternate_separator = u'/';
    }
    auto enumerate = [backend](const std::u16string &p, const std::u16string &pattern, bool directories) {
        auto entries = directories ? directory_get_directories(text(p), text(pattern), backend)
                                   : directory_get_files(text(p), text(pattern), backend);
        std::vector<std::u16string> values;
        values.reserve(entries.size());
        for (const auto &entry : entries)
            values.push_back(*entry);
        return values;
    };
    result.directories = [enumerate](const auto &p, const auto &pattern) {
        return enumerate(p, pattern, true);
    };
    result.files = [enumerate](const auto &p, const auto &pattern) { return enumerate(p, pattern, false); };
    result.combine = [windows](const std::u16string &first, const std::u16string &second) {
        if (second.empty())
            return first;
        if (first.empty())
            return second;
        auto separator = [windows](char16_t c) { return c == u'/' || (windows && c == u'\\'); };
        // Path.IsPathRooted(string), not IsPathFullyQualified: drive-relative paths
        // replace the first operand too. Volume separators are not general slashes.
        bool drive = windows && second.size() >= 2 && second[1] == u':' &&
                     ((second[0] >= u'A' && second[0] <= u'Z') || (second[0] >= u'a' && second[0] <= u'z'));
        if (separator(second[0]) || drive)
            return second;
        auto value = first;
        if (!separator(value.back()))
            value += windows ? u'\\' : u'/';
        return value + second;
    };
    return result;
}
std::vector<std::u16string> glob(const std::optional<std::u16string> &path) {
    return glob(path, native_glob_backend());
}
}
