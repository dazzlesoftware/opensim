// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/scene/uuid_gatherer.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/services/user_account.hpp"
#include "opensim/framework/texture_entry.hpp"
#include <bit>
#include <algorithm>
#include <stdexcept>
namespace opensim::scene {
namespace {
using View = std::string_view;
View trim(View v, View chars = " ") {
    auto start = v.find_first_not_of(chars);
    if (start == View::npos)
        return {};
    auto end = v.find_last_not_of(chars);
    return v.substr(start, end - start + 1);
}
View data(const UuidGatherer::Asset &a) {
    if (!a || !a->data)
        throw std::runtime_error("Null asset data");
    return {reinterpret_cast<const char *>(a->data->data()), a->data->size()};
}
// osUTF8Slice ReadLine returns false for an unterminated last line, while
// SkipLine returns true. Escaped CR/LF are ordinary bytes, including CRLF.
class Lines {
    View all_;
    std::size_t pos_ = 0;

  public:
    explicit Lines(View v) : all_(v) {}
    bool read(View &out, bool skip = false) {
        if (pos_ >= all_.size()) {
            out = {};
            return false;
        }
        auto start = pos_;
        for (; pos_ < all_.size(); ++pos_)
            if ((all_[pos_] == '\r' || all_[pos_] == '\n') && (pos_ == 0 || all_[pos_ - 1] != '\\')) {
                out = all_.substr(start, pos_ - start);
                char c = all_[pos_++];
                if (c == '\r' && pos_ < all_.size() && all_[pos_] == '\n')
                    ++pos_;
                return true;
            }
        out = all_.substr(start);
        return skip;
    }
    bool skip() {
        View out;
        return read(out, true);
    }
};
bool integer(View value, std::int32_t &out) {
    value = trim(value);
    out = 0;
    if (value.empty())
        return false;
    bool negative = value[0] == '-';
    if (value[0] == '-' || value[0] == '+')
        value.remove_prefix(1);
    std::uint32_t number = 0;
    for (unsigned char c : value) {
        if (c < '0' || c > '9')
            break;
        number = number * 10u + (c - '0');
    }
    out = std::bit_cast<std::int32_t>(negative ? 0u - number : number);
    return true;
}
bool byte_uuid(View value, framework::Uuid &id) {
    while (value.starts_with(' '))
        value.remove_prefix(1);
    if (value.size() < 32) {
        id = {};
        return false;
    }
    auto size = value[8] == '-' ? 36u : 32u;
    if (value.size() < size) {
        id = {};
        return false;
    }
    return framework::try_parse_sdk_uuid(value.substr(0, size), id);
}
std::vector<View> split(View v) {
    std::vector<View> out;
    if (v.empty()) {
        out.push_back(v);
        return out;
    }
    while (!v.empty()) {
        auto at = v.find_first_of(" \t");
        if (at == View::npos) {
            out.push_back(v);
            break;
        }
        if (at)
            out.push_back(v.substr(0, at));
        v.remove_prefix(at + 1);
    }
    return out;
}
// Original parser mutates the shared osUTF8Slice. It deliberately does not
// implement general XML parsing (including its escaped-angle handling).
int node(View &value, View &header) {
    header = value;
    auto start = value.find('<');
    if (start == View::npos)
        return -1;
    if (start > 0 && value[start - 1] == '\\') {
        if (start + 1 > value.size())
            throw std::out_of_range("slice");
        value.remove_prefix(start + 1);
    }
    ++start;
    auto end = value.find('>');
    if (end == View::npos)
        return -1;
    if (start > 0 && start - 1 < value.size() && value[start - 1] == '\\') {
        value.remove_prefix(std::min(start + 1, value.size()));
    }
    header = end >= start && start < value.size() ? trim(value.substr(start, end - start)) : View{};
    value.remove_prefix(std::min(end + 1, value.size()));
    return static_cast<int>(end + 1);
}
bool xml_uuid(View &value, framework::Uuid &id) {
    id = {};
    View header;
    if (node(value, header) < 0)
        return false;
    if (!header.starts_with("UUID") && !header.starts_with("uuid"))
        return false;
    if (header.ends_with('/'))
        return true;
    auto at = value.find('<');
    if (at == View::npos)
        return false;
    auto text = value.substr(0, at);
    value.remove_prefix(at);
    return byte_uuid(text, id);
}
std::optional<std::vector<std::uint8_t>> binary(View &value) {
    auto at = value.find('<');
    if (at == View::npos || at == 0)
        return {};
    auto text = trim(value.substr(0, at));
    value.remove_prefix(at);
    if (text.empty())
        return {};
    std::string raw;
    for (char c : text)
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            raw += c;
    if (raw.size() % 4)
        return {};
    auto digit = [](char c) -> int {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        return c == '+' ? 62 : c == '/' ? 63 : -1;
    };
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i < raw.size(); i += 4) {
        int a = digit(raw[i]), b = digit(raw[i + 1]), c = digit(raw[i + 2]), d = digit(raw[i + 3]);
        if (a < 0 || b < 0)
            return {};
        bool two = raw[i + 2] == '=', one = raw[i + 3] == '=';
        if (two && !one)
            return {};
        if ((two || one) && i + 4 != raw.size())
            return {};
        if ((!two && c < 0) || (!one && d < 0))
            return {};
        out.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
        if (!two)
            out.push_back(static_cast<std::uint8_t>((b << 4) | (c >> 2)));
        if (!one)
            out.push_back(static_cast<std::uint8_t>((c << 6) | d));
    }
    return out;
}
std::int32_t add32(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
struct ExtraReferences {
    framework::Uuid projection;
    std::vector<framework::Uuid> materials;
};
ExtraReferences extra_references(const std::vector<std::uint8_t> &bytes) {
    ExtraReferences out;
    auto length = static_cast<std::int32_t>(bytes.size());
    auto byte = [&](std::int32_t at) {
        if (at < 0 || at >= length)
            throw std::out_of_range("extra parameter byte");
        return bytes[static_cast<std::size_t>(at)];
    };
    auto guid = [&](std::int32_t at) {
        if (at < 0 || at > length || length - at < 16)
            throw std::out_of_range("extra parameter UUID");
        return framework::Uuid::from_bytes(std::span(bytes).subspan(static_cast<std::size_t>(at), 16));
    };
    auto remaining = [&](std::int32_t at) {
        return add32(length, std::bit_cast<std::int32_t>(0u - static_cast<std::uint32_t>(at)));
    };
    if (length == 1)
        return out;
    int count = byte(0);
    std::int32_t cursor = 1;
    for (int i = 0; i < count; ++i) {
        switch (byte(cursor)) {
        case 0x10:
        case 0x20:
            cursor = add32(cursor, 22);
            break;
        case 0x30:
        case 0x60:
            cursor = add32(cursor, 23);
            break;
        case 0x40:
            cursor = add32(cursor, 6);
            out.projection = remaining(cursor) >= 28 ? guid(cursor) : framework::Uuid{};
            cursor = add32(cursor, 28);
            break;
        case 0x70:
            cursor = add32(cursor, 10);
            break;
        case 0x90:
            cursor = add32(cursor, 15);
            break;
        case 0x80: {
            cursor = add32(cursor, 2);
            if (remaining(cursor) >= 4) {
                std::uint32_t raw = 0;
                for (int j = 0; j < 4; ++j)
                    raw |= std::uint32_t(byte(add32(cursor, j))) << (8 * j);
                auto size = std::bit_cast<std::int32_t>(raw);
                cursor = add32(cursor, 4);
                if (size > 17) {
                    int entries = byte(cursor);
                    if (size >= 1 + 17 * entries) {
                        auto position = add32(cursor, 1);
                        std::vector<framework::Uuid> ids;
                        for (int j = 0; j < entries; ++j) {
                            byte(position);
                            position = add32(position, 1);
                            ids.push_back(guid(position));
                            position = add32(position, 16);
                        }
                        out.materials = std::move(ids);
                    }
                }
                cursor = add32(cursor, add32(size, 4));
            }
            break;
        }
        default:
            break;
        }
    }
    return out;
}
framework::Uuid particle_texture(std::span<const std::uint8_t> bytes) {
    if (bytes.size() == 86)
        return framework::Uuid::from_bytes(std::span(bytes).subspan(36, 16));
    if (bytes.size() < 87 || bytes.size() > 98)
        return {};
    std::uint32_t header = 0;
    for (unsigned i = 0; i < 4; ++i)
        header |= std::uint32_t(bytes[i]) << (8 * i);
    if (header != 68)
        return {};
    if (bytes.size() < 94)
        throw std::out_of_range("Index was outside the bounds of the array.");
    return framework::Uuid::from_bytes(std::span(bytes).subspan(40, 16));
}

}
namespace {
std::optional<std::vector<framework::Uuid>> note_ids(const UuidGatherer::Asset &asset) {
    if (!asset || !asset->data || asset->data->size() < 79)
        return std::nullopt;
    auto note = framework::utf16_replacing(data(asset));
    if (note.empty() || std::all_of(note.begin(), note.end(), framework::unicode_whitespace))
        return std::nullopt;
    auto fixed = [&](std::size_t offset, std::size_t count) {
        if (offset > note.size() || count > note.size() - offset)
            throw std::out_of_range(
                "Index and length must refer to a location within the string. (Parameter 'length')");
        return std::u16string_view(note).substr(offset, count);
    };
    if (fixed(0, 21) != u"Linden text version 2" || fixed(24, 25) != u"LLEmbeddedItems version 1" ||
        fixed(52, 5) != u"count")
        return std::nullopt;
    using Index = std::int64_t;
    auto size = static_cast<Index>(note.size());
    auto search = [&](std::u16string_view needle, Index start, Index count) -> Index {
        if (start < 0 || start > size)
            throw std::out_of_range(
                "Index was out of range. Must be non-negative and less than or equal to the size of the collection. (Parameter 'startIndex')");
        if (count < 0 || count > size - start)
            throw std::out_of_range(
                "Count must be positive and count must refer to a location within the string/array/collection. (Parameter 'count')");
        auto found =
            framework::invariant_index_of(std::u16string_view(note).substr(static_cast<std::size_t>(start),
                                                                           static_cast<std::size_t>(count)),
                                          needle);
        return found < 0 ? -1 : start + found;
    };
    auto find_char = [&](char16_t c, Index start) -> Index {
        if (start < 0 || start > size)
            throw std::out_of_range(
                "Index was out of range. Must be non-negative and less than or equal to the size of the collection. (Parameter 'startIndex')");
        auto found = note.find(c, static_cast<std::size_t>(start));
        return found == std::u16string::npos ? -1 : static_cast<Index>(found);
    };
    Index index = -1, limit = std::min<Index>(size - 59, 8);
    for (Index i = 57; i < 57 + limit; ++i)
        if (note[static_cast<std::size_t>(i)] == u'\t' || note[static_cast<std::size_t>(i)] == u'\n') {
            index = i;
            break;
        }
    if (index < 0)
        return std::nullopt;
    std::int32_t count;
    if (!services::parse_account_integer(
            framework::utf8(std::u16string_view(note).substr(57, static_cast<std::size_t>(index - 57))),
            count))
        return std::nullopt;
    auto field = [&](Index start, std::u16string_view name, std::u16string_view &value) -> Index {
        value = {};
        auto at = search(name, start, std::min<Index>(size - start, 64));
        if (at < 0)
            return -1;
        at += static_cast<Index>(name.size()) + 1;
        auto max = std::min<Index>(size - at - 2, 129);
        if (at < 0 || at > size)
            throw std::out_of_range(
                "Index was out of range. Must be non-negative and less than or equal to the size of the collection. (Parameter 'startIndex')");
        if (max < 0 || max > size - at)
            throw std::out_of_range(
                "Count must be positive and count must refer to a location within the string/array/collection. (Parameter 'count')");
        Index end = -1;
        for (Index i = at; i < at + max; ++i)
            if (note[static_cast<std::size_t>(i)] == u'\t' || note[static_cast<std::size_t>(i)] == u'\n') {
                end = i;
                break;
            }
        if (end < 0)
            return -1;
        value = std::u16string_view(note).substr(static_cast<std::size_t>(at),
                                                 static_cast<std::size_t>(end - at));
        return end;
    };
    std::vector<framework::Uuid> ids;
    while (count > 0) {
        index = find_char(u'}', index);
        if (index < 0)
            return std::nullopt;
        auto original = index;
        std::u16string_view value;
        index = field(index, u"asset_id", value);
        bool shadow = index < 0;
        if (shadow) {
            index = field(original, u"shadow_id", value);
            if (index < 0)
                return std::nullopt;
        }
        framework::Uuid id;
        if (!framework::try_parse_sdk_uuid(framework::utf8(value), id))
            return std::nullopt;
        if (shadow) {
            auto bytes = id.bytes();
            constexpr std::uint8_t key[16] = {60,  17,  94,  81,  4,   244, 82, 60,
                                              159, 166, 152, 175, 241, 3,   71, 48};
            for (std::size_t i = 0; i < 16; ++i)
                bytes[i] ^= key[i];
            id = framework::Uuid::from_bytes(bytes);
        }
        ids.push_back(id);
        index = find_char(u'}', index);
        if (index < 0)
            return std::nullopt;
        index = field(index, u"name", value);
        if (index < 0)
            return std::nullopt;
        index = field(index, u"desc", value);
        if (index < 0)
            return std::nullopt;
        if (count > 1) {
            index = search(u"ext char index", index, size - index);
            if (index < 0)
                return std::nullopt;
        }
        --count;
    }
    index = search(u"Text length", index, size - index);
    if (index > 0) {
        index += 14;
        if (index > size)
            throw std::out_of_range(
                "Specified argument was out of the range of valid values. (Parameter 'start')");
        auto body = std::u16string_view(note).substr(static_cast<std::size_t>(index));
        for (std::int64_t i = 8; i < static_cast<Index>(body.size()) - 28;) {
            if (body[static_cast<std::size_t>(i)] == u'-') {
                framework::Uuid id;
                if (framework::try_parse_sdk_uuid(
                        framework::utf8(body.substr(static_cast<std::size_t>(i - 8), 36)), id)) {
                    if (!id.is_zero())
                        ids.push_back(id);
                    i += 37;
                } else
                    i += 9;
            } else
                ++i;
        }
    }
    if (ids.empty())
        return std::nullopt;
    return ids;
}
}
void UuidGatherer::inspect_notecard(const Asset &asset) {
    auto ids = note_ids(asset);
    if (!ids)
        return;
    for (auto id : *ids) {
        if (id.is_zero())
            continue;
        state_->uncertain.add(id);
        add_for_inspection(id);
    }
}
void UuidGatherer::inspect_gesture(const Asset &asset) {
    Lines lines(data(asset));
    for (int i = 0; i < 5; ++i)
        if (!lines.skip())
            return;
    View line;
    if (!lines.read(line))
        return;
    std::int32_t count;
    if (!integer(line, count) || count == 0)
        return;
    for (std::int32_t i = 0; i < count; ++i) {
        if (!lines.read(line))
            return;
        line = trim(line);
        std::int32_t type;
        if (!integer(line, type))
            return;
        switch (type) {
        case 0:
        case 1: {
            if (!lines.skip() || !lines.read(line))
                return;
            Uuid key;
            if (byte_uuid(line, key) && !key.is_zero())
                state_->gathered[key] = type == 0 ? 20 : 1;
            if (!lines.skip())
                return;
            break;
        }
        case 2:
        case 3:
            if (!lines.skip() || !lines.skip())
                return;
            break;
        default:
            return;
        }
    }
}
void UuidGatherer::inspect_wearable(const Asset &asset) {
    if (!asset || !asset->data || asset->data->size() < 64)
        return;
    try {
        Lines lines(data(asset));
        for (int i = 0; i < 4; ++i)
            if (!lines.skip())
                return;
        View line;
        while (lines.read(line)) {
            auto parts = split(trim(line, " \t"));
            if (parts.empty())
                throw std::out_of_range("parts");
            if (parts[0].empty())
                continue;
            auto name = trim(parts[0], " \t");
            if (name == "parameters" || name == "textures") {
                if (parts.size() < 2)
                    throw std::out_of_range("parts");
                if (parts[1].empty())
                    return;
                std::int32_t count;
                if (!integer(trim(parts[1], " \t"), count) || (name == "textures" && count == 0))
                    return;
                for (std::int32_t i = 0; i < count; ++i) {
                    if (name == "parameters") {
                        if (!lines.skip())
                            return;
                    } else {
                        if (!lines.read(line))
                            return;
                        auto tex = split(trim(line, " \t"));
                        if (tex.size() < 2 || tex[1].size() < 36)
                            continue;
                        Uuid key;
                        if (framework::try_parse_sdk_uuid(trim(tex[1], " \t"), key) && !key.is_zero())
                            state_->gathered[key] = 0;
                    }
                }
            }
        }
    } catch (...) {
    }
}
void UuidGatherer::inspect_material(const Asset &asset) {
    auto value = data(asset);
    View header;
    while (node(value, header) > 0) {
        if (header.starts_with('/'))
            continue;
        if (header.starts_with("uuid")) {
            if (header.ends_with('/'))
                continue;
            auto index = value.find('<');
            if (index == View::npos)
                continue;
            Uuid key;
            if (byte_uuid(value.substr(0, index), key) && !key.is_zero())
                state_->gathered[key] = 0;
            value.remove_prefix(index);
        }
    }
}
framework::Uuid gather_particle_texture(std::span<const std::uint8_t> bytes) {
    return particle_texture(bytes);
}
void UuidGatherer::inspect_object(const Asset &asset) {
    auto value = data(asset);
    View header;
    auto texture = [&](Uuid id) {
        if (!id.is_zero() && !skipped(id) && !state_->failed.contains(id))
            state_->gathered[id] = 0;
    };
    auto face = [&](const std::shared_ptr<framework::TextureFace> &f) {
        if (!f)
            return;
        texture(f->texture_id());
        auto id = f->material_id();
        if (!id.is_zero())
            add_for_inspection(id);
    };
    while (node(value, header) > 0) {
        if (header.starts_with('/'))
            continue;
        if (header.starts_with("SOPAnims")) {
            if (header.ends_with('/'))
                continue;
            auto bytes = binary(value);
            if (bytes && bytes->size() > 16) {
                try {
                    int count = (*bytes)[0] | (int((*bytes)[1]) << 8);
                    std::size_t cursor = 2;
                    while (--count >= 0) {
                        if (cursor > bytes->size() || bytes->size() - cursor < 16)
                            throw std::out_of_range("animation UUID");
                        auto id = Uuid::from_bytes(std::span(*bytes).subspan(cursor, 16));
                        if (id.is_zero())
                            break;
                        if (!skipped(id) && !state_->failed.contains(id))
                            state_->gathered[id] = 20;
                        cursor += 16;
                        if (cursor >= value.size())
                            throw std::out_of_range("animation name");
                        auto length = static_cast<unsigned char>(value[cursor++]);
                        cursor += length;
                    }
                } catch (...) {
                }
            }
        } else if (header.starts_with("CollisionSound")) {
            if (!header.ends_with('d'))
                continue;
            Uuid id;
            if (xml_uuid(value, id) && !id.is_zero())
                state_->gathered[id] = 1;
        } else if (header.starts_with("SoundID") || header.starts_with("SculptTexture")) {
            if (header.ends_with('/'))
                continue;
            bool sound = header.starts_with("SoundID");
            Uuid id;
            if (xml_uuid(value, id) && !id.is_zero())
                state_->gathered[id] = sound ? 1 : 0;
        } else if (header.starts_with("ExtraParams")) {
            if (header.ends_with('/'))
                continue;
            auto bytes = binary(value);
            if (bytes && bytes->size() > 16) {
                try {
                    auto refs = extra_references(*bytes);
                    texture(refs.projection);
                    for (auto id : refs.materials)
                        if (!id.is_zero())
                            add_for_inspection(id, std::int8_t(57));
                } catch (...) {
                }
            }
        } else if (header.starts_with("ParticleSystem")) {
            if (header.ends_with('/'))
                continue;
            auto bytes = binary(value);
            if (bytes && bytes->size() > 16) {
                try {
                    texture(particle_texture(*bytes));
                } catch (...) {
                }
            }
        } else if (header.starts_with("TextureEntry")) {
            if (header.ends_with('/'))
                continue;
            auto bytes = binary(value);
            if (bytes && bytes->size() > 16) {
                try {
                    framework::TextureEntry entry(*bytes);
                    face(entry.default_texture);
                    for (auto &f : entry.faces)
                        face(f);
                } catch (...) {
                }
            }
        } else if (header.starts_with("TaskInventory")) {
            if (header.ends_with('/'))
                continue;
            while (node(value, header) > 0) {
                if (header.starts_with("AssetID")) {
                    Uuid id;
                    if (xml_uuid(value, id) && !id.is_zero())
                        add_for_inspection(id);
                } else if (header.starts_with("/TaskInventory"))
                    break;
            }
        } else if (header.starts_with("MatOvrd")) {
            if (header.ends_with('/'))
                continue;
            auto bytes = binary(value);
            if (bytes && bytes->size() > 36)
                inspect_embedded(*bytes);
        }
    }
}
std::shared_ptr<UuidGathererBackend> native_uuid_gatherer_backend(
    std::shared_ptr<services::AssetService> service, UuidGathererBackend::Inspector object,
    std::function<void(const std::string &)> error, std::function<void(const std::string &)> debug) {
    if (!service || !object || !error || !debug)
        throw std::invalid_argument("Native gatherer requires service, object inspector and logs");
    auto backend = std::make_shared<UuidGathererBackend>();
    backend->get_asset = [service = std::move(service)](auto id) { return service->get_asset(id.str()); };
    backend->object = std::move(object);
    backend->error = std::move(error);
    backend->debug = std::move(debug);
    backend->wearable = [](auto &g, auto &a) { g.inspect_wearable(a); };
    backend->gesture = [](auto &g, auto &a) { g.inspect_gesture(a); };
    backend->notecard = [](auto &g, auto &a) { g.inspect_notecard(a); };
    backend->material = [](auto &g, auto &a) { g.inspect_material(a); };
    backend->embedded = [](auto &g, auto &a) { g.inspect_embedded(*a->data); };
    return backend;
}

std::shared_ptr<UuidGathererBackend>
native_uuid_gatherer_backend(std::shared_ptr<services::AssetService> service,
                             std::function<void(const std::string &)> error,
                             std::function<void(const std::string &)> debug) {
    return native_uuid_gatherer_backend(
        std::move(service), [](auto &g, auto &a) { g.inspect_object(a); }, std::move(error),
        std::move(debug));
}

}
