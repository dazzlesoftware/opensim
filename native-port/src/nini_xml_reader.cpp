// Native synchronous System.Xml consumed reader state machine, runtime v8.0.31.
// Copyright (c) .NET Foundation and Contributors. MIT license:
// native-port/third_party/dotnet_collation/LICENSE.
#include "opensim/framework/nini_xml_reader.hpp"
#include "opensim/framework/nini_xml_character.hpp"
#include "opensim/framework/nini_xml_reader_encoding.hpp"
#include "opensim/framework/nini_xml_reader_dtd.hpp"
#include <bit>
#include <algorithm>
#include <map>
#include <limits>
#include <sstream>
#include <iomanip>
namespace opensim::framework {
namespace {
using Text = NiniWriterText;
constexpr std::u16string_view xml_uri = u"http://www.w3.org/XML/1998/namespace";
constexpr std::u16string_view xmlns_uri = u"http://www.w3.org/2000/xmlns/";
std::int32_t unchecked_add(std::int32_t a, std::int32_t b) {
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(a) + static_cast<std::uint32_t>(b));
}
std::u16string decimal(std::int64_t n) {
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
std::pair<std::u16string, std::u16string> character_arguments(char16_t ch, char16_t next = 0) {
    std::u16string display;
    std::uint32_t code = ch;
    if (ch >= 0xd800 && ch <= 0xdbff && next != 0) {
        display = {ch, next};
        code = (std::uint32_t(next) - 0xdc00) | (((std::uint32_t(ch) - 0xd800) << 10) + 0x10000);
    } else
        display = ch == 0 ? u"." : std::u16string(1, ch);
    std::ostringstream h;
    h << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << code;
    auto v = h.str();
    return {display, {v.begin(), v.end()}};
}
}
NiniXmlReaderSettings NiniXmlReaderSettings::legacy_document() {
    NiniXmlReaderSettings s;
    s.dtd_processing = NiniXmlDtdProcessing::Parse;
    s.entity_handling = NiniXmlEntityHandling::ExpandCharEntities;
    s.normalize = false;
    s.legacy_compatibility = true;
    s.validating_reader_compatibility = true;
    s.close_input = true;
    s.max_characters_from_entities = 0;
    return s;
}
struct NativeNiniXmlReader::State {
    struct Frame {
        std::u16string name, prefix, local, uri;
        std::map<std::u16string, std::u16string> namespaces;
        bool preserve = false;
        std::int32_t line = 0, position = 0;
    };
    struct ReaderEntity {
        std::size_t frame_depth;
        bool manual, empty_pending;
        std::u16string name;
        int reference_line = 0, reference_position = 0;
    };
    struct AttributeChunk {
        int type, line, position;
        std::u16string value;
    };
    std::vector<std::vector<AttributeChunk>> attribute_chunks;
    std::size_t attribute_chunk_index = 0, attribute_entity_base_depth = 0;
    std::vector<ReaderEntity> reader_entities;
    std::size_t manual_entity_depth = 0;
    bool after_resolve_content = false;
    std::shared_ptr<NiniXmlDtdInfo> dtd_info;
    std::function<std::shared_ptr<NiniXmlDtdInfo>()> parse_dtd;
    std::function<std::shared_ptr<NativeNiniXmlByteInput>()> deferred_open;
    bool deferred_pending = false;
    NiniWriterText materialized_uri;
    NiniXmlReaderSettings settings;
    std::u16string input;
    std::size_t cursor = 0;
    std::int32_t line = 1, column = 1;
    bool previous_cr = false;
    std::vector<Frame> frames;
    NiniXmlReadToken current, element;
    std::vector<NiniXmlReadToken> node_slots = std::vector<NiniXmlReadToken>(1);
    std::size_t source_index = 0;
    NiniXmlReadState state = NiniXmlReadState::Initial;
    std::ptrdiff_t attribute = -1;
    bool attribute_value = false, root_seen = false, pop_frame = false, doctype_seen = false,
         parsing_error = false, entity_eol_normalized = false;
    std::shared_ptr<NativeNiniXmlByteInput> stream;
    std::shared_ptr<NativeNiniXmlTextInput> text_input;
    std::vector<std::uint8_t> bytes;
    bool byte_mode = false, byte_eof = false, char_eof = true, append_mode = true,
         declaration_checked = false;
    std::size_t byte_position = 0, bytes_used = 0, document_start_byte_position = 0,
                character_capacity = 4097, buffer_origin = 0, read_data_calls = 0, text_position = 0;
    std::u16string text_source;
    std::optional<std::size_t> buffer_anchor;
    std::int64_t consumed_characters = 0;
    NiniXmlEncoding encoding;
    std::optional<NiniXmlEncoding> declaration_encoding;
    std::unique_ptr<NiniXmlDecoder> decoder;
    NiniWriterText input_base_uri;
    std::shared_ptr<NiniXmlDtdEntity> current_entity;
    std::int32_t current_entity_id = 0, next_entity_id = 1;
    std::int64_t characters_from_entities = 0;
    std::map<std::u16string, NiniWriterText> interned_names;
    std::map<const NiniXmlDtdEntity*, std::shared_ptr<NiniXmlDtdEntity>> active_entities;
    struct InputSnapshot {
        std::u16string input, text_source;
        std::vector<std::uint8_t> bytes;
        std::shared_ptr<NativeNiniXmlByteInput> stream;
        std::shared_ptr<NativeNiniXmlTextInput> text_input;
        std::unique_ptr<NiniXmlDecoder> decoder;
        std::size_t cursor = 0, byte_position = 0, bytes_used = 0, document_start_byte_position = 0,
                    character_capacity = 0, buffer_origin = 0, text_position = 0;
        std::int32_t line = 1, column = 1;
        bool previous_cr = false, byte_mode = false, byte_eof = false, char_eof = true, append_mode = false,
             entity_eol_normalized = true;
        NiniXmlEncoding encoding;
        std::optional<NiniXmlEncoding> declaration_encoding;
        NiniWriterText input_base_uri, materialized_uri;
        std::shared_ptr<NiniXmlDtdEntity> current_entity;
        std::int32_t current_entity_id = 0;
    };
    std::vector<InputSnapshot> entity_inputs;

    explicit State(NiniXmlReaderSettings s)
        : settings(std::move(s)), entity_eol_normalized(!settings.normalize),
          input_base_uri(settings.base_uri ? settings.base_uri : nini_writer_text(u"")) {
    }
    [[noreturn]] void error(std::u16string message, std::int32_t ln = -1, std::int32_t pos = -1,
                            std::string resource = {}) {
        if (ln < 0) {
            ln = line;
            pos = column;
        }
        if (ln != 0)
            message += u" Line " + decimal(ln) + u", position " + decimal(pos) + u".";
        NiniXmlDtdError e("System.Xml.XmlException", 0x80131940, std::move(message));
        e.resource_name = std::move(resource);
        e.source_uri = input_base_uri ? *input_base_uri : u"";
        e.line_number = ln;
        e.line_position = pos;
        state = NiniXmlReadState::Error;
        parsing_error = true;
        current.line_number = ln;
        current.line_position = pos;
        throw e;
    }
    void register_characters(std::int64_t count, bool entity) {
        auto add = [&](std::int64_t& total, std::int64_t maximum, std::u16string_view setting) {
            if (maximum <= 0)
                return;
            if (count > std::numeric_limits<std::int64_t>::max() - total)
                error(u"The input document has exceeded a limit set by " + std::u16string(setting) + u".", 0,
                      0);
            total += count;
            if (total > maximum)
                error(u"The input document has exceeded a limit set by " + std::u16string(setting) + u".", 0,
                      0);
        };
        add(consumed_characters, settings.max_characters_in_document, u"MaxCharactersInDocument");
        if (entity)
            add(characters_from_entities, settings.max_characters_from_entities,
                u"MaxCharactersFromEntities");
    }
    InputSnapshot save_input() {
        InputSnapshot x;
        x.input = std::move(input);
        x.text_source = std::move(text_source);
        x.bytes = std::move(bytes);
        x.stream = std::move(stream);
        x.text_input = std::move(text_input);
        x.decoder = std::move(decoder);
        x.cursor = std::move(cursor);
        x.byte_position = std::move(byte_position);
        x.bytes_used = std::move(bytes_used);
        x.document_start_byte_position = std::move(document_start_byte_position);
        x.character_capacity = std::move(character_capacity);
        x.buffer_origin = std::move(buffer_origin);
        x.text_position = std::move(text_position);
        x.line = std::move(line);
        x.column = std::move(column);
        x.previous_cr = std::move(previous_cr);
        x.byte_mode = std::move(byte_mode);
        x.byte_eof = std::move(byte_eof);
        x.char_eof = std::move(char_eof);
        x.append_mode = std::move(append_mode);
        x.entity_eol_normalized = std::move(entity_eol_normalized);
        x.encoding = std::move(encoding);
        x.declaration_encoding = std::move(declaration_encoding);
        x.input_base_uri = std::move(input_base_uri);
        x.materialized_uri = std::move(materialized_uri);
        x.current_entity = std::move(current_entity);
        x.current_entity_id = std::move(current_entity_id);
        return x;
    }
    void restore_input(InputSnapshot&& x) {
        input = std::move(x.input);
        text_source = std::move(x.text_source);
        bytes = std::move(x.bytes);
        stream = std::move(x.stream);
        text_input = std::move(x.text_input);
        decoder = std::move(x.decoder);
        cursor = std::move(x.cursor);
        byte_position = std::move(x.byte_position);
        bytes_used = std::move(x.bytes_used);
        document_start_byte_position = std::move(x.document_start_byte_position);
        character_capacity = std::move(x.character_capacity);
        buffer_origin = std::move(x.buffer_origin);
        text_position = std::move(x.text_position);
        line = std::move(x.line);
        column = std::move(x.column);
        previous_cr = std::move(x.previous_cr);
        byte_mode = std::move(x.byte_mode);
        byte_eof = std::move(x.byte_eof);
        char_eof = std::move(x.char_eof);
        append_mode = std::move(x.append_mode);
        entity_eol_normalized = std::move(x.entity_eol_normalized);
        encoding = std::move(x.encoding);
        declaration_encoding = std::move(x.declaration_encoding);
        input_base_uri = std::move(x.input_base_uri);
        materialized_uri = std::move(x.materialized_uri);
        current_entity = std::move(x.current_entity);
        current_entity_id = std::move(x.current_entity_id);
    }
    void push_string_input(Text base, Text text, bool normalized, bool internal_dtd = false) {
        if (entity_inputs.size() == entity_inputs.capacity())
            entity_inputs.reserve(entity_inputs.capacity() ? entity_inputs.capacity() * 2 : 2);
        auto previous_encoding = encoding;
        auto previous_entity = current_entity;
        auto previous_id = current_entity_id;
        auto previous_append = append_mode;
        entity_inputs.push_back(save_input());
        input.clear();
        text_source.clear();
        bytes.clear();
        stream.reset();
        text_input.reset();
        decoder.reset();
        cursor = byte_position = bytes_used = document_start_byte_position = buffer_origin = text_position =
            0;
        character_capacity = 0;
        line = column = 1;
        previous_cr = false;
        byte_mode = byte_eof = char_eof = false;
        append_mode = previous_append;
        entity_eol_normalized = normalized;
        encoding = previous_encoding;
        declaration_encoding.reset();
        input_base_uri = base;
        materialized_uri.reset();
        current_entity = previous_entity;
        current_entity_id = previous_id;
        if (internal_dtd) {
            input_base_uri = nini_writer_text(u"");
            entity_eol_normalized = true;
            encoding = {};
        }
        if (!text)
            throw NiniXmlProviderError("System.NullReferenceException", 0x80004003,
                                       u"Object reference not set to an instance of an object.");
        if (internal_dtd) {
            register_characters(static_cast<std::int64_t>(text->size()), false);
            input_base_uri = base;
            entity_eol_normalized = normalized;
        }
        input = *text;
        character_capacity = input.size() + 1;
        char_eof = true;
        buffer_anchor.reset();
    }
    bool push_dtd_entity(const std::shared_ptr<NiniXmlDtdEntity>& entity, std::int32_t& id) {
        if (!entity)
            throw NiniXmlProviderError("System.NullReferenceException", 0x80004003,
                                       u"Object reference not set to an instance of an object.");
        if (entity->external) {
            id = -1;
            return false;
        }
        push_string_input(entity->declared_uri ? entity->declared_uri : nini_writer_text(u""), entity->text,
                          !settings.normalize);
        if (active_entities.contains(entity.get()))
            error(entity->parameter
                      ? u"Parameter entity '" + entity->name.local_name + u"' references itself."
                      : u"General entity '" + entity->name.local_name + u"' references itself.",
                  entity_inputs.back().line, entity_inputs.back().column);
        current_entity = entity;
        current_entity_id = next_entity_id;
        next_entity_id = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(next_entity_id) + 1u);
        active_entities.emplace(entity.get(), entity);
        line = entity->line.number;
        column = unchecked_add(entity->line.position, 1);
        entity_eol_normalized = true;
        register_characters(static_cast<std::int64_t>(input.size()), true);
        id = current_entity_id;
        return true;
    }
    bool pop_dtd_entity(std::shared_ptr<NiniXmlDtdEntity>& old, std::int32_t& id) {
        if (entity_inputs.empty()) {
            old.reset();
            id = -1;
            return false;
        }
        old = current_entity;
        if (stream)
            stream->close();
        if (current_entity)
            active_entities.erase(current_entity.get());
        if (stream)
            stream->close();
        auto previous = std::move(entity_inputs.back());
        entity_inputs.pop_back();
        restore_input(std::move(previous));
        id = current_entity_id;
        return true;
    }
    std::int32_t input_read(std::span<std::uint8_t> target) {
        auto count = stream->read(target);
        if (count < 0 || static_cast<std::size_t>(count) > target.size())
            throw std::invalid_argument("Native XML byte input returned an invalid Read count");
        return count;
    }
    std::size_t read_data() {
        if (char_eof)
            return 0;
        ++read_data_calls;
        if (!byte_mode) {
            auto used = input.size() - buffer_origin;
            if (append_mode) {
                if (used == character_capacity - 1)
                    character_capacity *= 2;
            } else if (character_capacity - used <= character_capacity / 2) {
                auto read_position = buffer_anchor.value_or(cursor);
                auto unread = input.size() - read_position;
                if (unread < character_capacity - 1) {
                    buffer_origin = read_position;
                    used = unread;
                } else
                    character_capacity *= 2;
            }
            std::size_t count;
            if (text_input) {
                std::vector<char16_t> chars(character_capacity - used - 1);
                auto read = text_input->read(chars);
                if (read < 0 || static_cast<std::size_t>(read) > chars.size())
                    throw std::invalid_argument("Native XML text input returned an invalid Read count");
                count = static_cast<std::size_t>(read);
                input.append(chars.data(), count);
            } else {
                count = std::min(character_capacity - used - 1, text_source.size() - text_position);
                input.append(text_source, text_position, count);
                text_position += count;
            }
            register_characters(static_cast<std::int64_t>(count), bool(current_entity));
            if (count == 0)
                char_eof = true;
            return count;
        }
        auto used = input.size() - buffer_origin;
        std::size_t maximum;
        if (append_mode) {
            if (used == character_capacity - 1)
                character_capacity *= 2;
            if (bytes_used - byte_position < 6 && bytes.size() - bytes_used < 6)
                bytes.resize(bytes.size() * 2);
            maximum = std::min<std::size_t>(character_capacity - used - 1, 80);
        } else {
            if (character_capacity - used <= character_capacity / 2) {
                auto read_position = buffer_anchor.value_or(cursor);
                auto unread = input.size() - read_position;
                if (unread < character_capacity - 1) {
                    buffer_origin = read_position;
                    used = unread;
                } else
                    character_capacity *= 2;
            }
            auto remaining = bytes_used - byte_position;
            if (remaining <= 128) {
                std::move(bytes.begin() + static_cast<std::ptrdiff_t>(byte_position),
                          bytes.begin() + static_cast<std::ptrdiff_t>(bytes_used), bytes.begin());
                bytes_used = remaining;
                byte_position = 0;
            }
            maximum = character_capacity - used - 1;
        }
        if (!byte_eof && byte_position == bytes_used && bytes.size() > bytes_used) {
            auto count = input_read(std::span(bytes).subspan(bytes_used));
            if (count == 0)
                byte_eof = true;
            bytes_used += static_cast<std::size_t>(count);
        }
        auto before = byte_position;
        auto result = decoder->convert(
            std::span<const std::uint8_t>(bytes).subspan(byte_position, bytes_used - byte_position), maximum);
        if (result.fault == NiniXmlDecodeFault::Xml) {
            NiniXmlProviderError e("System.Xml.XmlException", 0x80131940,
                                   u"Invalid character in the given encoding.");
            e.source_uri = u"";
            e.line_number = 0;
            e.line_position = 0;
            throw e;
        }
        if (result.fault == NiniXmlDecodeFault::Argument)
            error(u"Invalid character in the given encoding.", line,
                  column + static_cast<std::int32_t>(input.size() - cursor));
        byte_position += result.bytes_used;
        auto count = result.text.size();
        input += result.text;
        register_characters(static_cast<std::int64_t>(count), bool(current_entity));
        if (count == 0 && byte_position != before)
            return read_data();
        if (count == 0)
            char_eof = true;
        return count;
    }
    void initialize_stream() {
        byte_mode = true;
        char_eof = false;
        auto length = stream->length_hint();
        std::size_t capacity = 4096;
        if (length) {
            if (*length < 0)
                throw NiniXmlProviderError("System.OverflowException", 0x80131516,
                                           u"Arithmetic operation resulted in an overflow.");
            if (*length < 4096)
                capacity = static_cast<std::size_t>(*length);
            else if (*length > 65536)
                capacity = 8192;
        }
        const bool new_buffer = bytes.empty();
        if (bytes.size() < capacity)
            bytes.resize(capacity);
        character_capacity = new_buffer ? capacity + 1 : std::max(character_capacity, capacity + 1);
        byte_position = 0;
        auto minimum = std::min<std::size_t>(4, bytes.size() - bytes_used);
        if (bytes_used < 4 && bytes.size() > bytes_used) {
            std::size_t acquired = 0;
            while (acquired < minimum) {
                auto count = input_read(std::span(bytes).subspan(bytes_used + acquired));
                if (count == 0) {
                    byte_eof = true;
                    break;
                }
                acquired += static_cast<std::size_t>(count);
            }
            bytes_used += acquired;
        }
        encoding = nini_xml_detect_encoding(std::span<const std::uint8_t>(bytes).first(bytes_used));
        if (encoding.kind == NiniXmlEncodingKind::Ebcdic)
            error(u"System does not support 'ebcdic' encoding.");
        decoder = std::make_unique<NiniXmlDecoder>(encoding.kind);
        byte_position = encoding.preamble;
        document_start_byte_position = byte_position;
        read_data();
    }
    void check_encoding(std::u16string_view value, std::int32_t ln, std::int32_t pos) {
        if (!byte_mode)
            return;
        std::u16string lower(value);
        for (auto& c : lower)
            if (c >= u'A' && c <= u'Z')
                c += 32;
        if (lower == u"ucs-2" || lower == u"utf-16" || lower == u"iso-10646-ucs-2" || lower == u"ucs-4") {
            if (encoding.web_name != "utf-16" && encoding.web_name != "utf-16BE" && lower != u"ucs-4")
                error(u"There is no Unicode byte order mark. Cannot switch to Unicode.", 0, 0);
            declaration_encoding = encoding;
            if (encoding.kind == NiniXmlEncodingKind::SafeAscii)
                declaration_encoding->kind = NiniXmlEncodingKind::Utf8Replacement;
            return;
        }
        auto found = nini_xml_builtin_encoding(value);
        if (!found)
            error(u"System does not support '" + std::u16string(value) + u"' encoding.", ln, pos);
        declaration_encoding = *found;
    }
    void switch_encoding(NiniXmlEncoding next) {
        if (next.web_name != encoding.web_name || encoding.kind == NiniXmlEncodingKind::SafeAscii) {
            if (settings.max_characters_in_document > 0)
                consumed_characters -= static_cast<std::int64_t>(input.size() - cursor);
            byte_position =
                document_start_byte_position +
                nini_xml_encoded_byte_count(std::u16string_view(input).substr(0, cursor), encoding.kind);
            input.resize(cursor);
            if (byte_position > bytes_used)
                throw std::out_of_range("XML source encoding rewind exceeded the current byte buffer");
            encoding = next;
            decoder = std::make_unique<NiniXmlDecoder>(encoding.kind);
            append_mode = false;
            char_eof = false;
            read_data();
        }
        append_mode = false;
    }
    [[noreturn]] void unexpected_end(bool pending = false) {
        if (frames.empty()) {
            if (pending)
                error(u"Unexpected end of file has occurred. The following elements are not closed: ");
            error(u"Unexpected end of file has occurred.");
        }
        std::u16string names;
        for (auto i = frames.rbegin(); i != frames.rend(); ++i) {
            if (!names.empty())
                names += u", ";
            names += i->name;
        }
        error(u"Unexpected end of file has occurred. The following elements are not closed: " + names + u".");
    }
    bool ensure(std::size_t count) {
        while (input.size() - cursor < count && !char_eof)
            if (read_data() == 0)
                break;
        return input.size() - cursor >= count;
    }
    bool at_end() {
        return !ensure(1);
    }
    bool zero_ending_stream() {
        if (settings.legacy_compatibility && cursor + 1 == input.size() && input[cursor] == 0 &&
            read_data() == 0 && byte_eof) {
            input.pop_back();
            return true;
        }
        return false;
    }
    char16_t peek(std::size_t n = 0) {
        return ensure(n + 1) ? input[cursor + n] : 0;
    }
    bool starts(std::u16string_view value) {
        auto available = std::min(value.size(), input.size() - cursor);
        if (std::u16string_view(input).substr(cursor, available) != value.substr(0, available))
            return false;
        return ensure(value.size()) && std::u16string_view(input).substr(cursor).starts_with(value);
    }
    char16_t take() {
        if (at_end())
            error(u"Unexpected end of file.");
        auto c = input[cursor++];
        if (c == u'\r') {
            line = unchecked_add(line, 1);
            column = 1;
            previous_cr = true;
        } else if (c == u'\n') {
            if (!previous_cr)
                line = unchecked_add(line, 1);
            column = 1;
            previous_cr = false;
        } else {
            column = unchecked_add(column, 1);
            previous_cr = false;
        }
        return c;
    }
    void consume(std::size_t n) {
        while (n--)
            take();
    }
    bool white(char16_t c) const {
        return (nini_xml_character_properties(c) & 1) != 0;
    }
    bool skip_white() {
        bool any = false;
        while (!at_end() && white(peek())) {
            take();
            any = true;
        }
        return any;
    }
    std::u16string unexpected_token() const {
        if (cursor == input.size())
            return {};
        auto i = cursor;
        if ((nini_xml_character_properties(input[i]) & 8) != 0) {
            for (++i; i < input.size() && (nini_xml_character_properties(input[i]) & 8) != 0; ++i) {
            }
            return input.substr(cursor, i - cursor);
        }
        return std::u16string(1, input[cursor]);
    }
    void require(char16_t c) {
        if (at_end())
            error(u"Unexpected end of file has occurred.");
        if (peek() != c) {
            auto a = character_arguments(peek());
            error(u"'" + unexpected_token() + u"' is an unexpected token. The expected token is '" +
                  std::u16string(1, c) + u"'.");
        }
        take();
    }
    void valid_character() {
        auto c = peek();
        if (c >= 0xd800 && c <= 0xdbff && !ensure(2))
            error(u"Unexpected end of file has occurred.");
        if (c >= 0xd800 && c <= 0xdbff && cursor + 1 < input.size() &&
            (peek(1) < 0xdc00 || peek(1) > 0xdfff)) {
            auto a = character_arguments(peek(1), cursor + 2 < input.size() ? input[cursor + 2] : 0);
            error(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.", line,
                  column + 1);
        }
        if ((nini_xml_character_properties(c) & 16) != 0)
            return;
        if (c >= 0xd800 && c <= 0xdbff && cursor + 1 < input.size() && peek(1) >= 0xdc00 && peek(1) <= 0xdfff)
            return;
        auto a = character_arguments(c, cursor + 1 < input.size() ? input[cursor + 1] : 0);
        error(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.");
    }
    std::u16string read_character(bool attribute_context = false) {
        valid_character();
        auto c = take();
        std::u16string value(1, c);
        if (c >= 0xd800 && c <= 0xdbff) {
            value += take();
            return value;
        }
        if (!entity_eol_normalized && c == u'\r') {
            if (peek() == u'\n')
                take();
            c = u'\n';
        }
        if (attribute_context && settings.normalize && (c == u'\r' || c == u'\n' || c == u'\t'))
            c = u' ';
        value.assign(1, c);
        return value;
    }
    std::u16string name(bool qname = true) {
        auto begin = cursor;
        struct Anchor {
            State& state;
            std::optional<std::size_t> previous;
            explicit Anchor(State& s) : state(s), previous(s.buffer_anchor) {
                s.buffer_anchor = s.cursor;
            }
            ~Anchor() {
                state.buffer_anchor = previous;
            }
        } anchor(*this);
        bool start = true, colon = false;
        while (true) {
            if (at_end())
                error(u"Unexpected end of file while parsing Name has occurred.");
            auto c = peek();
            if (start) {
                if ((nini_xml_character_properties(c) & 4) == 0) {
                    auto a = character_arguments(c, peek(1));
                    error(u"Name cannot begin with the '" + a.first + u"' character, hexadecimal value " +
                          a.second + u".");
                }
                take();
                start = false;
            } else if ((nini_xml_character_properties(c) & 8) != 0)
                take();
            else if (c == u':') {
                if (colon || !qname) {
                    auto a = character_arguments(c);
                    error(u"The '" + a.first + u"' character, hexadecimal value " + a.second +
                          u", cannot be included in a name.");
                }
                colon = true;
                take();
                start = true;
            } else
                break;
        }
        return input.substr(begin, cursor - begin);
    }
    static std::pair<std::u16string, std::u16string> split(const std::u16string& name) {
        auto c = name.find(u':');
        return c == name.npos ? std::pair<std::u16string, std::u16string>{{}, name}
                              : std::pair{name.substr(0, c), name.substr(c + 1)};
    }
    std::u16string reference(bool attribute_context = false) {
        auto reference_line = line, reference_position = column;
        require(u'&');
        if (at_end())
            error(u"Unexpected end of file has occurred.", reference_line, reference_position);
        if (peek() == u'#') {
            take();
            bool hex = peek() == u'x';
            if (hex)
                take();
            std::int32_t code = 0;
            bool any = false;
            while (!at_end() && peek() != u';') {
                auto c = peek();
                unsigned digit = c >= u'0' && c <= u'9'          ? unsigned(c - u'0')
                                 : hex && c >= u'a' && c <= u'f' ? unsigned(c - u'a' + 10)
                                 : hex && c >= u'A' && c <= u'F' ? unsigned(c - u'A' + 10)
                                                                 : 99;
                auto radix = hex ? 16 : 10;
                if (digit >= unsigned(radix))
                    error(hex ? u"Invalid syntax for a hexadecimal numeric entity reference."
                              : u"Invalid syntax for a decimal numeric entity reference.");
                if (hex) {
                    if (code >
                        (std::numeric_limits<std::int32_t>::max() - static_cast<std::int32_t>(digit)) / 16)
                        error(u"Invalid value of a character entity reference.");
                    code = code * 16 + static_cast<std::int32_t>(digit);
                } else {
                    if (code > std::numeric_limits<std::int32_t>::max() / 10 ||
                        code * 10 > std::numeric_limits<std::int32_t>::max() - static_cast<std::int32_t>(c))
                        error(u"Invalid value of a character entity reference.");
                    code = code * 10 + static_cast<std::int32_t>(c) - 48;
                }
                any = true;
                take();
            }
            if (at_end())
                error(u"Unexpected end of file while parsing  has occurred.", reference_line,
                      reference_position);
            if (!any)
                error(hex ? u"Invalid syntax for a hexadecimal numeric entity reference."
                          : u"Invalid syntax for a decimal numeric entity reference.");
            take();
            if (code <= 65535) {
                auto c = static_cast<char16_t>(code);
                if ((nini_xml_character_properties(c) & 16) == 0 &&
                    (!settings.legacy_compatibility || settings.normalize)) {
                    auto a = character_arguments(c);
                    error(u"'" + a.first + u"', hexadecimal value " + a.second +
                              u", is an invalid character.",
                          reference_line, reference_position + 2 + (hex ? 1 : 0));
                }
                return std::u16string(1, c);
            }
            auto n = code - 65536;
            auto low = static_cast<char16_t>(0xdc00 + n % 1024),
                 high = static_cast<char16_t>(0xd800 + n / 1024);
            if (settings.normalize && (high < 0xd800 || high > 0xdbff || low < 0xdc00 || low > 0xdfff)) {
                auto a = character_arguments(high, low);
                error(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.",
                      reference_line, reference_position + 2 + (hex ? 1 : 0));
            }
            return {high, low};
        }
        std::u16string entity;
        try {
            entity = name(false);
        } catch (const NiniXmlProviderError&) {
            error(u"An error occurred while parsing EntityName.", reference_line, reference_position + 1);
        }
        if (peek() != u';')
            error(u"An error occurred while parsing EntityName.", reference_line, reference_position + 1);
        take();
        if (entity == u"lt")
            return u"<";
        if (entity == u"gt")
            return u">";
        if (entity == u"amp")
            return u"&";
        if (entity == u"apos")
            return u"'";
        if (entity == u"quot")
            return u"\"";
        if (attribute_context) {
            enter_general(lookup_general(entity, reference_position + 1, true), false);
            return {};
        }
        error(u"Reference to undeclared entity '" + entity + u"'.", reference_line, reference_position + 1);
    }
    bool numeric_reference_incomplete() const {
        auto i = cursor + 2;
        if (i >= input.size())
            return true;
        bool hex = input[i] == u'x';
        if (hex)
            ++i;
        std::int32_t value = 0;
        while (i < input.size()) {
            auto c = input[i];
            unsigned digit = c >= u'0' && c <= u'9'          ? unsigned(c - u'0')
                             : hex && c >= u'a' && c <= u'f' ? unsigned(c - u'a' + 10)
                             : hex && c >= u'A' && c <= u'F' ? unsigned(c - u'A' + 10)
                                                             : 99;
            if (digit >= unsigned(hex ? 16 : 10))
                break;
            if (hex) {
                if (value >
                    (std::numeric_limits<std::int32_t>::max() - static_cast<std::int32_t>(digit)) / 16)
                    return false;
                value = value * 16 + static_cast<std::int32_t>(digit);
            } else {
                if (value > std::numeric_limits<std::int32_t>::max() / 10 ||
                    value * 10 > std::numeric_limits<std::int32_t>::max() - static_cast<std::int32_t>(c))
                    return false;
                value = value * 10 + static_cast<std::int32_t>(c) - 48;
            }
            ++i;
        }
        return i == input.size();
    }
    bool unexpanded_reference() {
        if (settings.entity_handling != NiniXmlEntityHandling::ExpandCharEntities || peek() != u'&')
            return false;
        if (!ensure(2))
            error(u"Unexpected end of file has occurred.");
        if (peek(1) == u'#')
            return false;
        return !starts(u"&lt;") && !starts(u"&gt;") && !starts(u"&amp;") && !starts(u"&apos;") &&
               !starts(u"&quot;");
    }
    bool entity_reference_token() {
        take();
        auto ln = line, pos = column;
        std::u16string entity;
        try {
            entity = name(false);
        } catch (const NiniXmlProviderError&) {
            error(u"An error occurred while parsing EntityName.");
        }
        if (peek() != u';')
            error(u"An error occurred while parsing EntityName.");
        take();
        reset_token(5, ln, pos);
        current.name = current.local_name = std::move(entity);
        if (state != NiniXmlReadState::Error)
            state = NiniXmlReadState::Interactive;
        return true;
    }
    std::shared_ptr<NiniXmlDtdEntity> lookup_general(std::u16string_view name, int position,
                                                     bool attribute_context) {
        std::shared_ptr<NiniXmlDtdEntity> found;
        if (dtd_info)
            for (auto& entity : dtd_info->general_entities)
                if (entity->name.local_name == name) {
                    found = entity;
                    break;
                }
        if (!found)
            error(u"Reference to undeclared entity '" + std::u16string(name) + u"'.", line, position);
        if (found->unparsed())
            error(u"Reference to unparsed entity '" + std::u16string(name) + u"'.", line, position);
        if (attribute_context && found->external)
            error(u"External entity '" + std::u16string(name) +
                      u"' reference cannot appear in the attribute value.",
                  line, position);
        return found;
    }
    bool enter_general(const std::shared_ptr<NiniXmlDtdEntity>& entity, bool manual) {
        if (entity->external && !manual)
            return false;
        auto depth = frames.size();
        int id;
        if (entity->external) {
            push_string_input(entity->system_id, nini_writer_text(u""), false);
            if (active_entities.contains(entity.get()))
                error(u"General entity '" + entity->name.local_name + u"' references itself.",
                      entity_inputs.back().line, entity_inputs.back().column);
            current_entity = entity;
            current_entity_id = next_entity_id;
            next_entity_id = unchecked_add(next_entity_id, 1);
            active_entities.emplace(entity.get(), entity);
            register_characters(0, true);
        } else
            push_dtd_entity(entity, id);
        reader_entities.push_back({depth, manual, manual && input.empty(), entity->name.local_name});
        if (manual)
            ++manual_entity_depth;
        return true;
    }
    bool leave_general() {
        auto entry = reader_entities.back();
        if (frames.size() != entry.frame_depth)
            error(u"Incomplete entity contents.");
        std::shared_ptr<NiniXmlDtdEntity> old;
        int id;
        pop_dtd_entity(old, id);
        reader_entities.pop_back();
        if (entry.manual) {
            --manual_entity_depth;
            reset_token(16, line, column - 1);
            current.name = current.local_name = entry.name;
            return true;
        }
        return false;
    }
    bool content_general_reference() {
        auto reference_line = line, reference_position = column;
        take();
        if (at_end())
            error(u"Unexpected end of file has occurred.", reference_line, reference_position);
        auto position = column;
        std::u16string entity;
        try {
            entity = name(false);
        } catch (const NiniXmlProviderError&) {
            error(u"An error occurred while parsing EntityName.", reference_line, reference_position + 1);
        }
        if (peek() != u';')
            error(u"An error occurred while parsing EntityName.", reference_line, reference_position + 1);
        take();
        return enter_general(lookup_general(entity, position, false), false);
    }
    void finish_attribute_value() {
        if (attribute_value) {
            while (manual_entity_depth > attribute_entity_base_depth && !reader_entities.empty()) {
                std::shared_ptr<NiniXmlDtdEntity> old;
                int id;
                pop_dtd_entity(old, id);
                reader_entities.pop_back();
                --manual_entity_depth;
            }
            attribute_value = false;
            attribute_chunk_index = 0;
        }
    }
    void attribute_value_token(int type, std::u16string value, int ln, int pos, std::u16string name = {}) {
        current.node_type = type;
        current.name = current.local_name = std::move(name);
        current.prefix.clear();
        current.namespace_uri.clear();
        current.value = std::move(value);
        current.is_empty_element = false;
        current.depth =
            element.depth + 2 + static_cast<int>(manual_entity_depth - attribute_entity_base_depth);
        current.line_number = ln;
        current.line_position = pos;
        auto slot = static_cast<std::size_t>(element.depth) + element.attributes.size() + 1 +
                    manual_entity_depth - attribute_entity_base_depth;
        if (node_slots.size() <= slot)
            node_slots.resize(slot + 1);
        node_slots[slot] = current;
        node_slots[slot].attributes.clear();
    }
    bool next_attribute_value() {
        if (attribute < 0)
            return false;
        if (!attribute_value) {
            attribute_value = true;
            attribute_chunk_index = 0;
            attribute_entity_base_depth = manual_entity_depth;
        } else if (manual_entity_depth > attribute_entity_base_depth) {
            if (at_end()) {
                auto entry = reader_entities.back();
                if (entry.empty_pending) {
                    reader_entities.back().empty_pending = false;
                    attribute_value_token(3, u"", line, column);
                    return true;
                }
                std::shared_ptr<NiniXmlDtdEntity> old;
                int id;
                pop_dtd_entity(old, id);
                reader_entities.pop_back();
                --manual_entity_depth;
                attribute_value_token(16, u"", entry.reference_line,
                                      entry.reference_position + static_cast<int>(entry.name.size()),
                                      entry.name);
                return true;
            }
            auto ln = line, pos = column;
            std::u16string value;
            while (!at_end()) {
                if (unexpanded_reference()) {
                    if (!value.empty())
                        break;
                    take();
                    auto en_line = line, en_pos = column;
                    std::u16string entity;
                    try {
                        entity = name(false);
                    } catch (const NiniXmlProviderError&) {
                        error(u"An error occurred while parsing EntityName.");
                    }
                    if (peek() != u';')
                        error(u"An error occurred while parsing EntityName.");
                    take();
                    attribute_value_token(5, u"", en_line, en_pos, std::move(entity));
                    return true;
                }
                if (peek() == u'<')
                    error(u"'<', hexadecimal value 0x3C, is an invalid attribute character.");
                if (peek() == u'&')
                    value += reference();
                else
                    value += read_character(true);
            }
            attribute_value_token(3, std::move(value), ln, pos);
            return true;
        }
        auto index = static_cast<std::size_t>(attribute);
        if (index < attribute_chunks.size() && !attribute_chunks[index].empty()) {
            auto& chunks = attribute_chunks[index];
            if (attribute_chunk_index >= chunks.size())
                return false;
            auto& chunk = chunks[attribute_chunk_index++];
            attribute_value_token(chunk.type, chunk.type == 5 ? u"" : chunk.value, chunk.line, chunk.position,
                                  chunk.type == 5 ? chunk.value : u"");
            return true;
        }
        if (attribute_chunk_index++ != 0)
            return false;
        auto& value = element.attributes[index];
        attribute_value_token(3, value.value, value.value_line_number, value.value_line_position);
        return true;
    }
    void reset_token(std::int32_t type, std::int32_t ln, std::int32_t pos) {
        current = {};
        if (type != 1 && type != 17 && type != 10)
            element = {};
        current.node_type = type;
        current.depth = static_cast<std::int32_t>(frames.size() + manual_entity_depth);
        current.line_number = ln;
        current.line_position = pos;
        current.base_uri = input_base_uri ? *input_base_uri : u"";
        attribute = -1;
        attribute_value = false;
    }
    void parse_element() {
        attribute_chunks.clear();
        const auto ln = line, pos = column;
        auto qualified = name();
        reset_token(1, ln, pos);
        current.name = qualified;
        auto parts = split(qualified);
        current.prefix = parts.first;
        current.local_name = parts.second;
        Frame frame;
        frame.name = qualified;
        frame.prefix = parts.first;
        frame.local = parts.second;
        frame.line = ln;
        frame.position = pos;
        if (!frames.empty()) {
            frame.namespaces = frames.back().namespaces;
            frame.preserve = frames.back().preserve;
        } else
            frame.namespaces[u"xml"] = xml_uri;
        auto namespace_attribute = [&](NiniXmlReadAttribute& a) {
            if (a.prefix == u"xmlns" || (a.prefix.empty() && a.local_name == u"xmlns")) {
                a.namespace_uri = xmlns_uri;
                auto prefix = a.prefix.empty() ? std::u16string{} : a.local_name;
                if (!a.prefix.empty() && a.value.empty())
                    error(u"Invalid namespace declaration.", a.value_line_number, a.value_line_position - 1);
                if (a.value == xmlns_uri) {
                    if (prefix == u"xmlns")
                        error(u"Prefix \"xmlns\" is reserved for use by XML.", a.value_line_number,
                              a.value_line_position);
                    error(u"Prefix '" + prefix +
                              u"' cannot be mapped to namespace name reserved for \"xml\" or \"xmlns\".",
                          a.value_line_number, a.value_line_position);
                }
                if (a.value == xml_uri && prefix != u"xml" && !settings.legacy_compatibility)
                    error(u"Prefix '" + prefix +
                              u"' cannot be mapped to namespace name reserved for \"xml\" or \"xmlns\".",
                          a.value_line_number, a.value_line_position);
                if (prefix == u"xmlns")
                    error(u"Prefix \"xmlns\" is reserved for use by XML.", a.line_number, a.line_position);
                if (prefix == u"xml" && a.value != xml_uri)
                    error(
                        u"Prefix \"xml\" is reserved for use by XML and can be mapped only to namespace name \"http://www.w3.org/XML/1998/namespace\".",
                        a.line_number, a.line_position);
                frame.namespaces[prefix] = a.value;
            }
        };
        while (true) {
            auto original_buffer_origin = buffer_origin;
            bool separated = skip_white();
            if (at_end())
                unexpected_end(true);
            if (peek() == u'>') {
                take();
                break;
            }
            if (peek() == u'/' && peek(1) == u'>') {
                consume(2);
                current.is_empty_element = true;
                break;
            }
            if (peek() == u'/' && cursor + 1 == input.size()) {
                if (separated || !current.attributes.empty()) {
                    take();
                    unexpected_end(true);
                }
                error(u"Unexpected end of file while parsing > has occurred.", line,
                      column + static_cast<std::int32_t>(buffer_origin - original_buffer_origin));
            }
            if (peek() == u'/') {
                if (separated || !current.attributes.empty())
                    take();
                error(u"'" + unexpected_token() + u"' is an unexpected token. The expected token is '>'.");
            }
            if (!separated) {
                auto a = character_arguments(peek(), cursor + 1 < input.size() ? input[cursor + 1] : 0);
                if (!current.attributes.empty() && (nini_xml_character_properties(peek()) & 4) != 0)
                    error(u"'" + unexpected_token() + u"' is an unexpected token. Expecting whitespace.");
                if (!current.attributes.empty())
                    error(u"Name cannot begin with the '" + a.first + u"' character, hexadecimal value " +
                          a.second + u".");
                error(u"The '" + a.first + u"' character, hexadecimal value " + a.second +
                      u", cannot be included in a name.");
            }
            NiniXmlReadAttribute a;
            a.line_number = line;
            a.line_position = column;
            a.name = name();
            auto ap = split(a.name);
            a.prefix = ap.first;
            a.local_name = ap.second;
            skip_white();
            require(u'=');
            skip_white();
            if (at_end())
                error(u"Unexpected end of file has occurred.");
            auto quote = peek();
            if (quote != u'\'' && quote != u'\"')
                error(u"'" + std::u16string(1, quote) +
                      u"' is an unexpected token. The expected token is '\"' or '\''.");
            take();
            a.value_line_number = line;
            a.value_line_position = column;
            auto quote_entity_id = current_entity_id;
            std::vector<AttributeChunk> value_chunks;
            std::u16string outer_text;
            bool has_general_chunks = false;
            int outer_line = line, outer_position = column;
            auto flush_outer = [&] {
                if (!outer_text.empty()) {
                    value_chunks.push_back({3, outer_line, outer_position, std::move(outer_text)});
                    outer_text.clear();
                }
            };
            while (true) {
                if (at_end()) {
                    if (current_entity_id != quote_entity_id && !reader_entities.empty()) {
                        leave_general();
                        continue;
                    }
                    error(u"There is an unclosed literal string.");
                }
                if (peek() == quote && current_entity_id == quote_entity_id)
                    break;
                if (peek() == u'<')
                    error(u"'<', hexadecimal value 0x3C, is an invalid attribute character.");
                auto old_entity_id = current_entity_id;
                if (peek() == u'&' && !settings.validating_reader_compatibility && unexpanded_reference()) {
                    // Standalone XmlTextReader retains general references in attributes until ResolveEntity.
                    auto ref_line = line, ref_pos = column;
                    take();
                    std::u16string entity;
                    try {
                        entity = name(false);
                    } catch (const NiniXmlProviderError&) {
                        error(u"An error occurred while parsing EntityName.");
                    }
                    if (peek() != u';')
                        error(u"An error occurred while parsing EntityName.");
                    take();
                    a.value += u"&" + entity + u";";
                    has_general_chunks = true;
                    flush_outer();
                    value_chunks.push_back({5, ref_line, ref_pos + 1, std::move(entity)});
                    outer_line = line;
                    outer_position = column;
                } else if (peek() == u'&') {
                    auto ref_line = line, ref_pos = column;
                    auto part = reference(true);
                    a.value += part;
                    if (old_entity_id == quote_entity_id) {
                        if (current_entity_id != old_entity_id) {
                            has_general_chunks = true;
                            flush_outer();
                            value_chunks.push_back(
                                {5, ref_line, ref_pos + 1, current_entity->name.local_name});
                            outer_line = entity_inputs.back().line;
                            outer_position = entity_inputs.back().column;
                        } else
                            outer_text += part;
                    }
                } else {
                    auto part = read_character(true);
                    a.value += part;
                    if (old_entity_id == quote_entity_id)
                        outer_text += part;
                }
            }
            take();
            if (has_general_chunks && settings.entity_handling == NiniXmlEntityHandling::ExpandCharEntities) {
                flush_outer();
                attribute_chunks.push_back(std::move(value_chunks));
            } else
                attribute_chunks.emplace_back();
            namespace_attribute(a);
            if (a.prefix == u"xml" && a.local_name == u"space") {
                std::u16string_view value = a.value;
                while (!value.empty() && white(value.front()))
                    value.remove_prefix(1);
                while (!value.empty() && white(value.back()))
                    value.remove_suffix(1);
                if (value == u"preserve")
                    frame.preserve = true;
                else if (value == u"default")
                    frame.preserve = false;
                else
                    error(u"'" + a.value + u"' is an invalid xml:space value.", a.line_number,
                          a.line_position);
            }
            current.attributes.push_back(std::move(a));
        }

        if (dtd_info && (!settings.legacy_compatibility || settings.validating_reader_compatibility)) {
            std::shared_ptr<NiniXmlDtdAttributeList> declarations;
            NiniXmlDtdName ename{current.local_name, current.prefix};
            for (auto* lists :
                 {&dtd_info->element_declarations, &dtd_info->undeclared_element_declarations}) {
                for (auto& candidate : *lists)
                    if (candidate->name == ename) {
                        declarations = candidate;
                        break;
                    }
                if (declarations)
                    break;
            }
            if (declarations) {
                if (settings.normalize && declarations->has_non_cdata_attributes)
                    for (auto& attribute : current.attributes)
                        for (auto& declared : declarations->attributes)
                            if (declared->name == NiniXmlDtdName{attribute.local_name, attribute.prefix} &&
                                declared->non_cdata()) {
                                auto& value = attribute.value;
                                auto begin = value.find_first_not_of(u' ');
                                if (begin == value.npos) {
                                    if (!value.empty())
                                        value = u" ";
                                } else {
                                    auto end = value.find_last_not_of(u' ');
                                    std::u16string normalized;
                                    for (auto i = begin; i <= end; ++i) {
                                        normalized += value[i];
                                        if (value[i] == u' ')
                                            while (i < end && value[i + 1] == u' ')
                                                ++i;
                                    }
                                    value = std::move(normalized);
                                }
                                break;
                            }
                for (auto& def : declarations->default_attributes) {
                    if (std::any_of(
                            current.attributes.begin(), current.attributes.end(), [&](auto& attribute) {
                                return NiniXmlDtdName{attribute.local_name, attribute.prefix} == def->name;
                            }))
                        continue;
                    NiniXmlReadAttribute attribute;
                    attribute.name = def->name.qualified();
                    attribute.prefix = def->name.prefix;
                    attribute.local_name = def->name.local_name;
                    attribute.value = def->default_value_expanded;
                    attribute.line_number = def->line.number;
                    attribute.line_position = def->line.position;
                    attribute.value_line_number = def->value_line.number;
                    attribute.value_line_position = def->value_line.position;
                    attribute.is_default = true;
                    namespace_attribute(attribute);
                    if (def->is_xml_attribute && attribute.local_name == u"space") {
                        auto value = std::u16string_view(attribute.value);
                        while (!value.empty() && white(value.front()))
                            value.remove_prefix(1);
                        while (!value.empty() && white(value.back()))
                            value.remove_suffix(1);
                        if (value == u"preserve")
                            frame.preserve = true;
                        else if (value == u"default")
                            frame.preserve = false;
                        else
                            error(u"'" + attribute.value + u"' is an invalid xml:space value.",
                                  attribute.line_number, attribute.line_position);
                    }
                    current.attributes.push_back(std::move(attribute));
                }
            }
        }
        auto lookup = [&](const std::u16string& prefix, std::int32_t l, std::int32_t p) {
            auto i = frame.namespaces.find(prefix);
            if (i == frame.namespaces.end()) {
                if (prefix.empty())
                    return std::u16string{};
                error(u"'" + prefix + u"' is an undeclared prefix.", l, p);
            }
            return i->second;
        };
        if (current.prefix == u"xmlns")
            error(u"Prefix \"xmlns\" is reserved for use by XML.", ln, pos);
        current.namespace_uri = lookup(current.prefix, ln, pos);
        frame.uri = current.namespace_uri;
        for (auto& a : current.attributes) {
            if (a.namespace_uri.empty() && !a.prefix.empty())
                a.namespace_uri = lookup(a.prefix, a.line_number, a.line_position);
        }
        for (std::size_t i = 0; i < current.attributes.size(); ++i)
            for (std::size_t j = 0; j < i; ++j)
                if (current.attributes[i].local_name == current.attributes[j].local_name &&
                    current.attributes[i].namespace_uri == current.attributes[j].namespace_uri)
                    error(u"'" + current.attributes[i].name + u"' is a duplicate attribute name.",
                          current.attributes[i].line_number, current.attributes[i].line_position);
        root_seen = true;
        frames.push_back(std::move(frame));
        pop_frame = current.is_empty_element;
        element = current;
    }
    void skip_until(char16_t stop, bool recognize_literals) {
        bool literal = false, comment = false, pi = false;
        char16_t quote = u'"';
        while (true) {
            if (at_end())
                error(u"Unexpected end of file has occurred.");
            auto c = peek();
            if (c == stop && !literal) {
                take();
                return;
            }
            if ((nini_xml_character_properties(c) & 128) != 0 && c != stop && c != u'-' && c != u'?') {
                take();
                continue;
            }
            if (c == u'<') {
                if (!ensure(2))
                    error(u"Unexpected end of file has occurred.");
                if (peek(1) == u'?') {
                    if (recognize_literals && !literal && !comment) {
                        pi = true;
                        consume(2);
                        continue;
                    }
                } else if (peek(1) == u'!') {
                    if (!ensure(4))
                        error(u"Unexpected end of file has occurred.");
                    if (peek(2) == u'-' && peek(3) == u'-' && recognize_literals && !literal && !pi) {
                        comment = true;
                        consume(4);
                        continue;
                    }
                }
                take();
                continue;
            }
            if (c == u'-' && comment) {
                if (!ensure(3))
                    error(u"Unexpected end of file has occurred.");
                if (peek(1) == u'-' && peek(2) == u'>') {
                    comment = false;
                    consume(2);
                    continue;
                }
            }
            if (c == u'?' && pi) {
                if (!ensure(2))
                    error(u"Unexpected end of file has occurred.");
                if (peek(1) == u'>') {
                    pi = false;
                    take();
                    continue;
                }
            }
            if (c == u'\'' || c == u'"') {
                if (literal) {
                    if (quote == c)
                        literal = false;
                } else if (recognize_literals && !comment && !pi) {
                    literal = true;
                    quote = c;
                }
                take();
                continue;
            }
            if (c >= 0xd800 && c <= 0xdbff && !ensure(2))
                error(u"Unexpected end of file has occurred.");
            read_character();
        }
    }
    std::u16string cdata_or_comment(bool comment) {
        auto label = comment ? u"Comment" : u"CDATA";
        auto unexpected = [&]() {
            error(u"Unexpected end of file while parsing " + std::u16string(label) + u" has occurred.", -1,
                  -1, "Xml_UnexpectedEOF");
        };
        if (input.size() - cursor < 3 && read_data() == 0)
            unexpected();
        auto stop = comment ? u'-' : u']';
        std::u16string value;
        while (true) {
            if (at_end())
                unexpected();
            if (peek() == stop) {
                auto remaining = input.size() - cursor;
                if (remaining < 2 && !ensure(2))
                    unexpected();
                if (peek(1) == stop) {
                    if (input.size() - cursor < 3 && !ensure(3))
                        unexpected();
                    if (peek(2) == u'>') {
                        consume(3);
                        return value;
                    }
                    if (comment)
                        error(u"An XML comment cannot contain '--', and '-' cannot be the last character.");
                }
            }
            if (peek() == u'\r' && cursor + 1 == input.size() && !ensure(2))
                unexpected();
            if (peek() >= 0xd800 && peek() <= 0xdbff && cursor + 1 == input.size() && !ensure(2))
                unexpected();
            value += read_character();
        }
    }
    void parse_declaration(std::int32_t ln, std::int32_t pos) {
        reset_token(17, ln, pos);
        current.name = current.local_name = u"xml";
        std::size_t value_begin = 0, value_end = 0;
        int phase = 0;
        while (true) {
            bool separated = skip_white();
            if (!ensure(2) && (phase > 0 || peek() == u'?'))
                error(u"Unexpected end of file has occurred.");
            if (starts(u"?>")) {
                if (phase == 0)
                    error(u"Syntax for an XML declaration is invalid.");
                consume(2);
                break;
            }
            if (!separated && phase != 0)
                error(u"'" + std::u16string(1, peek()) +
                      u"' is an unexpected token. The expected token is '?>'.");
            NiniXmlReadAttribute a;
            a.line_number = line;
            a.line_position = column;
            auto start = cursor;
            a.name = name(false);
            a.local_name = a.name;
            if (a.name == u"version" && phase == 0) {
            } else if (a.name == u"encoding" && phase == 1) {
            } else if (a.name == u"standalone" && (phase == 1 || phase == 2))
                phase = 2;
            else
                error(u"Syntax for an XML declaration is invalid.", a.line_number, a.line_position);
            skip_white();
            require(u'=');
            skip_white();
            if (at_end())
                error(u"Unexpected end of file has occurred.");
            auto quote = peek();
            if (quote != u'\'' && quote != u'\"')
                error(u"Syntax for an XML declaration is invalid.");
            take();
            auto vl = line, vp = column;
            a.value_line_number = vl;
            a.value_line_position = vp;
            while (!at_end() && peek() != quote) {
                if ((nini_xml_character_properties(peek()) & 128) == 0)
                    error(u"Syntax for an XML declaration is invalid.");
                a.value += take();
            }
            if (at_end())
                error(u"There is an unclosed literal string.", vl, vp);
            if (phase == 0) {
                if (!std::u16string_view(a.value).starts_with(u"1.0"))
                    error(u"Version number '" + a.value + u"' is invalid.", vl, vp);
                phase = 1;
            } else if (phase == 1) {
                check_encoding(a.value, vl, vp);
                phase = 2;
            } else {
                if (a.value != u"yes" && a.value != u"no")
                    error(u"Syntax for an XML declaration is invalid.", vl, vp - 1);
                phase = 3;
            }
            take();
            if (current.attributes.empty())
                value_begin = start;
            value_end = cursor;
            current.attributes.push_back(std::move(a));
        }
        current.value = input.substr(value_begin, value_end - value_begin);
        if (settings.normalize) {
            std::u16string normalized;
            for (std::size_t i = 0; i < current.value.size(); ++i) {
                auto c = current.value[i];
                if (c == u'\r') {
                    if (i + 1 < current.value.size() && current.value[i + 1] == u'\n')
                        ++i;
                    c = u'\n';
                }
                normalized += c;
            }
            current.value = std::move(normalized);
        }
        element = current;
        if (byte_mode) {
            if (declaration_encoding)
                switch_encoding(*declaration_encoding);
            else if (encoding.kind == NiniXmlEncodingKind::SafeAscii)
                switch_encoding({NiniXmlEncodingKind::Utf8Strict, 0, "utf-8"});
            append_mode = false;
        }
        append_mode = false;
    }
    // The remaining synchronous reader branches and native loader publication are
    // being translated against the pinned source; this module is not registered
    // as a complete/default XML backend while that closure is unfinished.
    void remember_node() {
        source_index = static_cast<std::size_t>(std::max(current.depth, 0));
        if (node_slots.size() <= source_index + current.attributes.size())
            node_slots.resize(source_index + current.attributes.size() + 1);
        node_slots[source_index] = current;
        node_slots[source_index].attributes.clear();
        for (std::size_t i = 0; i < current.attributes.size(); ++i) {
            auto& a = current.attributes[i];
            NiniXmlReadToken slot;
            slot.node_type = 2;
            slot.depth = current.depth + 1;
            slot.line_number = a.line_number;
            slot.line_position = a.line_position;
            slot.name = a.name;
            slot.prefix = a.prefix;
            slot.local_name = a.local_name;
            slot.namespace_uri = a.namespace_uri;
            slot.value = a.value;
            slot.base_uri = current.base_uri;
            node_slots[source_index + i + 1] = std::move(slot);
        }
    }
    void prepare_node() {
        remember_node();
        if (current.node_type == 1 && !current.is_empty_element) {
            source_index = frames.size() + manual_entity_depth;
            if (node_slots.size() <= source_index)
                node_slots.resize(source_index + 1);
            current = node_slots[source_index];
            current.depth = static_cast<std::int32_t>(source_index);
        } else if (current.node_type == 1 && current.is_empty_element)
            current.is_empty_element = false;
        element = {};
    }
    bool read();
};
NativeNiniXmlReader::NativeNiniXmlReader(std::unique_ptr<State> s) : state_(std::move(s)) {
    state_->parse_dtd = [this] {
        auto adapter = native_nini_xml_dtd_adapter(shared_from_this());
        return native_nini_xml_parse_dtd(*adapter, true);
    };
}
NativeNiniXmlReader::~NativeNiniXmlReader() = default;
std::shared_ptr<NativeNiniXmlReader> NativeNiniXmlReader::from_text(Text text,
                                                                    NiniXmlReaderSettings settings) {
    if (!text)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003,
                                   u"Value cannot be null. (Parameter 's')", u"s");
    auto s = std::make_unique<State>(std::move(settings));
    s->text_source = *text;
    s->char_eof = false;
    s->read_data();
    return std::shared_ptr<NativeNiniXmlReader>(new NativeNiniXmlReader(std::move(s)));
}
std::shared_ptr<NativeNiniXmlReader>
NativeNiniXmlReader::from_text_input(std::shared_ptr<NativeNiniXmlTextInput> input,
                                     NiniXmlReaderSettings settings) {
    if (!input)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003,
                                   u"Value cannot be null. (Parameter 'input')", u"input");
    auto s = std::make_unique<State>(std::move(settings));
    s->text_input = std::move(input);
    s->char_eof = false;
    s->read_data();
    return std::shared_ptr<NativeNiniXmlReader>(new NativeNiniXmlReader(std::move(s)));
}
namespace {
class BufferedXmlInput final : public NativeNiniXmlByteInput {
    std::vector<std::uint8_t> bytes_;
    std::size_t position_ = 0;

  public:
    explicit BufferedXmlInput(std::span<const std::uint8_t> bytes) : bytes_(bytes.begin(), bytes.end()) {
    }
    std::int32_t read(std::span<std::uint8_t> destination) override {
        auto count = std::min(destination.size(), bytes_.size() - position_);
        std::copy_n(bytes_.begin() + static_cast<std::ptrdiff_t>(position_), count, destination.begin());
        position_ += count;
        return static_cast<std::int32_t>(count);
    }
    void close() override {
    }
    std::optional<std::int64_t> length_hint() const override {
        return static_cast<std::int64_t>(bytes_.size());
    }
};
}
std::shared_ptr<NativeNiniXmlReader> NativeNiniXmlReader::from_bytes(std::span<const std::uint8_t> bytes,
                                                                     NiniXmlReaderSettings settings) {
    return from_stream(std::make_shared<BufferedXmlInput>(bytes), std::move(settings));
}
std::shared_ptr<NativeNiniXmlReader>
NativeNiniXmlReader::from_stream(std::shared_ptr<NativeNiniXmlByteInput> input,
                                 NiniXmlReaderSettings settings) {
    if (!input)
        throw NiniXmlProviderError("System.ArgumentNullException", 0x80004003,
                                   u"Value cannot be null. (Parameter 'input')", u"input");
    auto s = std::make_unique<State>(std::move(settings));
    s->stream = std::move(input);
    s->initialize_stream();
    return std::shared_ptr<NativeNiniXmlReader>(new NativeNiniXmlReader(std::move(s)));
}
std::shared_ptr<NativeNiniXmlReader>
NativeNiniXmlReader::from_deferred_uri(Text base,
                                       std::function<std::shared_ptr<NativeNiniXmlByteInput>()> open,
                                       NiniXmlReaderSettings settings) {
    if (!open)
        throw std::invalid_argument("Deferred URI reader requires an opener");
    settings.base_uri = base;
    auto state = std::make_unique<State>(std::move(settings));
    state->deferred_open = std::move(open);
    state->deferred_pending = true;
    state->materialized_uri = base;
    state->current.base_uri = base ? *base : u"";
    return std::shared_ptr<NativeNiniXmlReader>(new NativeNiniXmlReader(std::move(state)));
}
const NiniXmlReadToken& NativeNiniXmlReader::token() const {
    return state_->current;
}
NiniXmlReadState NativeNiniXmlReader::read_state() const {
    return state_->state;
}
bool NativeNiniXmlReader::eof() const {
    return state_->state == NiniXmlReadState::EndOfFile;
}
bool NativeNiniXmlReader::State::read() {
    if (state == NiniXmlReadState::Closed || state == NiniXmlReadState::EndOfFile || parsing_error)
        return false;
    if (deferred_pending) {
        try {
            auto open = deferred_open;
            stream = open();
        } catch (...) {
            state = NiniXmlReadState::Error;
            parsing_error = true;
            throw;
        }
        if (!stream)
            error(u"Cannot resolve '" + (input_base_uri ? *input_base_uri : u"") + u"'.", 0, 0);
        initialize_stream();
        deferred_pending = false;
    }
    const bool initial_read = state == NiniXmlReadState::Initial;
    if (initial_read)
        state = NiniXmlReadState::Interactive;
    if (!declaration_checked) {
        declaration_checked = true;
        ensure(6);
        bool declaration =
            starts(u"<?xml") && (nini_xml_character_properties(peek(5)) & 8) == 0 && peek(5) != u':';
        if (!declaration) {
            if (byte_mode && encoding.kind == NiniXmlEncodingKind::SafeAscii)
                switch_encoding({NiniXmlEncodingKind::Utf8Strict, 0, "utf-8"});
            append_mode = false;
        }
        declaration_checked = true;
    }
    if (attribute >= 0) {
        finish_attribute_value();
        current = element;
        attribute = -1;
        attribute_value = false;
    }
    prepare_node();
    if (after_resolve_content) {
        source_index = frames.size() + manual_entity_depth;
        if (node_slots.size() <= source_index)
            node_slots.resize(source_index + 1);
        current = node_slots[source_index];
        current.depth = static_cast<int>(source_index);
        after_resolve_content = false;
    }

    if (pop_frame) {
        frames.pop_back();
        pop_frame = false;
    }
    if (initial_read && !byte_mode && input.size() < 4 && !input.empty() && input.front() == u'<')
        error(u"Data at the root level is invalid.", 1, 1);
    while (true) {
        if (at_end() && !reader_entities.empty()) {
            if (reader_entities.back().empty_pending) {
                reader_entities.back().empty_pending = false;
                reset_token(3, line, column);
                return true;
            }
            if (leave_general())
                return true;
            continue;
        }
        if (at_end()) {
            if (!frames.empty()) {
                std::u16string names;
                for (auto i = frames.rbegin(); i != frames.rend(); ++i) {
                    if (!names.empty())
                        names += u", ";
                    names += i->name;
                }
                error(u"Unexpected end of file has occurred. The following elements are not closed: " +
                      names + u".");
            }
            if (!root_seen)
                error(u"Root element is missing.", 0, 0);
            current = {};
            element = {};
            current.base_uri = input_base_uri ? *input_base_uri : u"";
            state = NiniXmlReadState::EndOfFile;
            return false;
        }
        // Pinned V1 ParseDocumentContent drains on an existing NUL without moving
        // charPos. This differs from its separate one-trailing-NUL text rule.
        if (frames.empty() && settings.legacy_compatibility && peek() == 0) {
            while (read_data() != 0) {
            }
            if (!root_seen)
                error(u"Root element is missing.", 0, 0);
            current = {};
            element = {};
            current.base_uri = input_base_uri ? *input_base_uri : u"";
            state = NiniXmlReadState::EndOfFile;
            return false;
        }
        auto ln = line, pos = column;
        if (frames.empty() && peek() == u'<' && !ensure(4))
            error(u"Data at the root level is invalid.");
        if (peek() != u'<') {
            if (frames.empty()) {
                std::u16string whitespace;
                while (!at_end() && white(peek()))
                    whitespace += read_character();
                if (at_end() || peek() == u'<' || zero_ending_stream()) {
                    if (whitespace.empty() || settings.ignore_whitespace)
                        continue;
                    reset_token(13, ln, pos);
                    current.value = std::move(whitespace);
                    if (state != NiniXmlReadState::Error)
                        state = NiniXmlReadState::Interactive;
                    return true;
                }
                if ((nini_xml_character_properties(peek()) & 16) != 0)
                    error(u"Data at the root level is invalid.");
                auto a = character_arguments(peek(), cursor + 1 < input.size() ? input[cursor + 1] : 0);
                error(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.");
            }
            auto initial_read_calls = read_data_calls;
            bool all_white = true, has_reference = false, decimal_reference = false, text_nonwhite = false,
                 partial_text = false, zero_text = false;
            std::u16string value;
            auto mark_partial = [&] {
                if (!settings.legacy_compatibility && !partial_text &&
                    (text_nonwhite || (value.size() >= 4096 && read_data_calls != initial_read_calls))) {
                    reset_token(3, ln, pos);
                    current.value = value;
                    partial_text = true;
                }
            };
            auto append_text = [&](std::u16string part) {
                for (auto c : part)
                    if (!white(c))
                        text_nonwhite = true;
                value += part;
            };
            while (true) {
                if (cursor == input.size() && !char_eof)
                    mark_partial();
                if (at_end() && !reader_entities.empty()) {
                    if (reader_entities.back().manual)
                        break;
                    leave_general();
                    continue;
                }
                if (at_end() || peek() == u'<')
                    break;
                if (zero_ending_stream()) {
                    zero_text = true;
                    break;
                }
                if (peek() == u'&') {
                    if (unexpanded_reference()) {
                        if (value.empty())
                            return entity_reference_token();
                        break;
                    }
                    bool complete_named = std::u16string_view(input).substr(cursor).starts_with(u"&lt;") ||
                                          std::u16string_view(input).substr(cursor).starts_with(u"&gt;") ||
                                          std::u16string_view(input).substr(cursor).starts_with(u"&amp;") ||
                                          std::u16string_view(input).substr(cursor).starts_with(u"&apos;") ||
                                          std::u16string_view(input).substr(cursor).starts_with(u"&quot;");
                    if (cursor + 1 >= input.size() ||
                        (input[cursor + 1] == u'#' ? numeric_reference_incomplete() : !complete_named))
                        mark_partial();
                    if (peek(1) != u'#' && !complete_named) {
                        content_general_reference();
                        has_reference = true;
                        continue;
                    }
                    has_reference = true;
                    if (settings.legacy_compatibility && peek(1) == u'#' && peek(2) != u'x')
                        decimal_reference = true;
                    append_text(reference());
                } else {
                    auto c = peek();
                    if (!char_eof &&
                        ((c == u']' && input.size() - cursor < 3) ||
                         ((c == u'\r' || (c >= 0xd800 && c <= 0xdbff)) && input.size() - cursor < 2)))
                        mark_partial();
                    if (starts(u"]]>"))
                        error(u"']]>' is not allowed in character data.");
                    append_text(read_character());
                }
            }
            if (value.empty() && !zero_text)
                continue;
            for (auto c : value)
                if (!white(c))
                    all_white = false;
            if (frames.empty() && (!all_white || has_reference))
                error(u"Data at the root level is invalid.", ln, pos);
            auto type =
                all_white && !decimal_reference ? (frames.empty() || !frames.back().preserve ? 13 : 14) : 3;
            if (!frames.empty() && !settings.legacy_compatibility && all_white && value.size() >= 4096 &&
                read_data_calls != initial_read_calls)
                type = 3;
            if (type == 13 && settings.ignore_whitespace)
                continue;
            reset_token(type, ln, pos);
            current.value = std::move(value);
            if (state != NiniXmlReadState::Error)
                state = NiniXmlReadState::Interactive;
            return true;
        }
        take();
        if (at_end() && !frames.empty())
            unexpected_end();
        if (peek() == u'/') {
            take();
            ln = line;
            pos = column;
            if (frames.empty() || (!reader_entities.empty() && reader_entities.back().manual &&
                                   frames.size() == reader_entities.back().frame_depth))
                error(u"Unexpected end tag.");
            auto frame = frames.back();
            ensure(frame.name.size() + (frame.prefix.empty() ? 1 : 0));
            if (std::u16string_view(input).substr(cursor) == frame.name && !ensure(frame.name.size() + 1)) {
                consume(frame.name.size());
                unexpected_end();
            }
            auto end_name = name();
            if (end_name != frame.name)
                error(u"The '" + frame.name + u"' start tag on line " + decimal(frame.line) + u" position " +
                          decimal(frame.position) + u" does not match the end tag of '" + end_name + u"'.",
                      ln, pos);
            skip_white();
            if (at_end())
                unexpected_end();
            require(u'>');
            reset_token(15, ln, pos);
            --current.depth;
            current.name = frame.name;
            current.prefix = frame.prefix;
            current.local_name = frame.local;
            current.namespace_uri = frame.uri;
            pop_frame = true;
            if (state != NiniXmlReadState::Error)
                state = NiniXmlReadState::Interactive;
            return true;
        }
        if (peek() == u'?') {
            take();
            ln = line;
            pos = column;
            auto target = name(false);
            bool is_xml = target == u"xml";
            bool reserved = target.size() == 3 && (target[0] == u'x' || target[0] == u'X') &&
                            (target[1] == u'm' || target[1] == u'M') &&
                            (target[2] == u'l' || target[2] == u'L');
            if (reserved && !is_xml)
                error(u"'" + target + u"' is an invalid name for processing instructions.", ln, pos);
            if (is_xml && (!initial_read || ln != 1 || pos != 3))
                error(
                    u"Unexpected XML declaration. The XML declaration must be the first node in the document, and no whitespace characters are allowed to appear before it.",
                    ln, pos);
            if (is_xml) {
                parse_declaration(ln, pos);
                if (state != NiniXmlReadState::Error)
                    state = NiniXmlReadState::Interactive;
                return true;
            }
            if (!settings.ignore_processing_instructions) {
                reset_token(7, ln, pos);
                current.name = current.local_name = target;
            }
            bool separated = skip_white();
            if (!separated && !starts(u"?>")) {
                auto a = character_arguments(peek(), cursor + 1 < input.size() ? input[cursor + 1] : 0);
                error(u"The '" + a.first + u"' character, hexadecimal value " + a.second +
                      u", cannot be included in a name.");
            }
            if (input.size() - cursor < 2 && read_data() == 0)
                error(u"Unexpected end of file while parsing PI has occurred.", line,
                      column + static_cast<std::int32_t>(input.size() - cursor));
            std::u16string value;
            while (!at_end() && !starts(u"?>"))
                value += read_character();
            if (at_end())
                error(u"Unexpected end of file while parsing PI has occurred.");
            consume(2);
            if (is_xml) {
                if (!value.starts_with(u"version"))
                    error(u"Syntax for an XML declaration is invalid.", ln, pos);
                reset_token(17, ln, pos);
                current.name = current.local_name = u"xml";
                current.value = std::move(value);
            } else {
                if (settings.ignore_processing_instructions)
                    continue;
                reset_token(7, ln, pos);
                current.name = current.local_name = std::move(target);
                current.value = std::move(value);
            }
            if (state != NiniXmlReadState::Error)
                state = NiniXmlReadState::Interactive;
            return true;
        }
        if (peek() == u'!') {
            take();
            if (!frames.empty()) {
                if (!ensure(2)) {
                    consume(input.size() - cursor);
                    unexpected_end();
                }
                if (peek() == u'[' && !ensure(7)) {
                    consume(input.size() - cursor);
                    unexpected_end();
                }
                if (peek() == u'-' && peek(1) != u'-') {
                    take();
                    error(u"'" + unexpected_token() +
                          u"' is an unexpected token. The expected token is '-'.");
                }
                if (peek() == u'[' && !starts(u"[CDATA[")) {
                    take();
                    error(u"'" + unexpected_token() +
                          u"' is an unexpected token. The expected token is 'CDATA['.");
                }
            }
            if (frames.empty() && peek() == u'[')
                error(u"Data at the root level is invalid.", ln, pos);
            if (starts(u"--")) {
                consume(2);
                ln = line;
                pos = column;
                auto value = cdata_or_comment(true);
                if (settings.ignore_comments)
                    continue;
                reset_token(8, ln, pos);
                current.value = std::move(value);
                if (state != NiniXmlReadState::Error)
                    state = NiniXmlReadState::Interactive;
                return true;
            }
            if (starts(u"[CDATA[")) {
                if (frames.empty())
                    error(u"Data at the root level is invalid.", ln, pos);
                consume(7);
                ln = line;
                pos = column;
                auto value = cdata_or_comment(false);
                reset_token(4, ln, pos);
                current.value = std::move(value);
                if (state != NiniXmlReadState::Error)
                    state = NiniXmlReadState::Interactive;
                return true;
            }
            if (!frames.empty()) {
                if (unexpected_token() == u"DOCTYPE")
                    error(u"Unexpected DTD declaration.");
                error(u"'" + unexpected_token() +
                      u"' is an unexpected token. The expected token is '<!--' or '<[CDATA['.");
            }
            if (settings.dtd_processing == NiniXmlDtdProcessing::Prohibit) {
                if (settings.legacy_compatibility)
                    error(u"DTD is prohibited in this XML document.", 0, 0);
                error(
                    u"For security reasons DTD is prohibited in this XML document. To enable DTD processing set the DtdProcessing property on XmlReaderSettings to Parse and pass the settings into XmlReader.Create method.",
                    0, 0);
            }
            if (!ensure(8))
                error(u"Unexpected end of file while parsing DOCTYPE has occurred.");
            if (!starts(u"DOCTYPE"))
                error(u"'" + unexpected_token() + u"' is an unexpected token. The expected token is '" +
                      (root_seen ? std::u16string(u"<!--") : std::u16string(u"DOCTYPE")) + u"'.");
            if (!white(peek(7)))
                error(u"Whitespace is required.", line, column + 7);
            if (doctype_seen && settings.dtd_processing != NiniXmlDtdProcessing::Ignore)
                error(u"Cannot have multiple DTDs.", ln, pos);
            if (root_seen)
                error(u"DTD must be defined before the document root element.", ln, pos);
            consume(8);
            skip_white();
            if (settings.dtd_processing == NiniXmlDtdProcessing::Parse) {
                current.line_number = line;
                current.line_position = column;
                current.attributes.clear();
                dtd_info = parse_dtd();
                current.node_type = 10;
                current.name = current.local_name = dtd_info->name.qualified();
                current.value = dtd_info->internal_subset;
                element = current;
                doctype_seen = true;
                return true;
            }
            name();
            skip_white();
            auto literal = [&]() {
                if (at_end())
                    error(u"Unexpected end of file has occurred.");
                auto quote = peek();
                if (quote != u'\'' && quote != u'\"')
                    error(u"'" + std::u16string(1, quote) +
                          u"' is an unexpected token. The expected token is '\"' or '\''.");
                take();
                skip_until(quote, false);
            };
            bool external = false;
            if ((peek() == u'P' || peek() == u'S') && !ensure(6))
                error(u"Unexpected end of file has occurred.");
            if (starts(u"PUBLIC")) {
                external = true;
                consume(6);
                if (at_end())
                    error(u"Unexpected end of file has occurred.");
                if (!skip_white())
                    error(u"Whitespace is required.");
                literal();
                if (!skip_white())
                    error(u"Whitespace is required.");
                literal();
                skip_white();
            } else if (starts(u"SYSTEM")) {
                external = true;
                consume(6);
                if (at_end())
                    error(u"Unexpected end of file has occurred.");
                if (!skip_white())
                    error(u"Whitespace is required.");
                literal();
                skip_white();
            } else if (peek() != u'[' && peek() != u'>')
                error(u"Expecting external ID, '[' or '>'.");
            if (external && peek() != u'[' && peek() != u'>')
                error(u"Expecting an internal subset or the end of the DOCTYPE declaration.");
            if (peek() == u'[') {
                take();
                skip_until(u']', true);
                skip_white();
            }
            require(u'>');
            doctype_seen = true;
            continue;
        }
        if (frames.empty() && root_seen)
            error(u"There are multiple root elements.");
        parse_element();
        if (state != NiniXmlReadState::Error)
            state = NiniXmlReadState::Interactive;
        return true;
    }
}

// Source DtdParserProxy bridge. The adapter keeps the reader alive while a parser
// holds mutable buffer views, including across provider callbacks.
class NativeNiniXmlReaderDtdAdapter final : public NiniXmlDtdAdapter {
    std::shared_ptr<NativeNiniXmlReader> owner_;
    NativeNiniXmlReader::State& s_;
    std::vector<char16_t> buffer_;
    bool exposed_ = false;
    std::int64_t line_start_ = 0;
    void sync() {
        if (!exposed_)
            return;
        auto count = s_.input.size() - s_.buffer_origin;
        std::copy_n(buffer_.begin(), count, s_.input.begin() + static_cast<std::ptrdiff_t>(s_.buffer_origin));
    }
    void invalidate() {
        exposed_ = false;
        line_start_ = static_cast<std::int64_t>(s_.cursor) - s_.column;
    }
    std::int32_t relative(std::size_t position) const {
        return static_cast<std::int32_t>(position - s_.buffer_origin);
    }
    void id_attribute(std::u16string_view name, NiniWriterText value, NiniXmlDtdLine keyword,
                      NiniXmlDtdLine literal) {
        NiniXmlReadAttribute a;
        a.name = a.local_name = name;
        a.value = value ? *value : u"";
        a.line_number = keyword.number;
        a.line_position = keyword.position;
        a.value_line_number = literal.number;
        a.value_line_position = literal.position;
        s_.current.attributes.push_back(std::move(a));
    }
    std::int32_t reference(std::u16string* builder) {
        sync();
        exposed_ = false;
        auto start = s_.cursor;
        auto value = s_.reference();
        auto end = s_.cursor;
        if (builder)
            builder->append(s_.input, start, end - start);
        auto result = end - value.size();
        std::copy(value.begin(), value.end(), s_.input.begin() + static_cast<std::ptrdiff_t>(result));
        s_.cursor = result;
        s_.column -= static_cast<std::int32_t>(value.size());
        invalidate();
        return relative(end);
    }

  public:
    explicit NativeNiniXmlReaderDtdAdapter(std::shared_ptr<NativeNiniXmlReader> reader)
        : owner_(std::move(reader)), s_(*owner_->state_) {
        invalidate();
    }
    ~NativeNiniXmlReaderDtdAdapter() override {
        sync();
    }
    NiniWriterText intern(std::u16string_view value) override {
        auto [it, inserted] = s_.interned_names.try_emplace(std::u16string(value));
        if (inserted)
            it->second = nini_writer_text(it->first);
        return it->second;
    }
    NiniWriterText lookup_namespace(std::u16string_view prefix) override {
        if (prefix == u"xml")
            return intern(xml_uri);
        if (prefix == u"xmlns")
            return intern(xmlns_uri);
        if (!s_.frames.empty()) {
            auto& ns = s_.frames.back().namespaces;
            auto found = ns.find(std::u16string(prefix));
            if (found != ns.end())
                return intern(found->second);
        }
        return prefix.empty() ? intern(u"") : NiniWriterText{};
    }
    NiniWriterText base_uri() const override {
        if (!s_.input_base_uri)
            throw NiniXmlProviderError("System.NullReferenceException", 0x80004003,
                                       u"Object reference not set to an instance of an object.");
        return s_.materialized_uri;
    }
    std::span<char16_t> parsing_buffer() override {
        if (s_.character_capacity == 0 && s_.input.empty())
            return {};
        if (!exposed_) {
            auto used = s_.input.size() - s_.buffer_origin;
            buffer_.resize(std::max(s_.character_capacity, used + 1));
            std::copy(s_.input.begin() + static_cast<std::ptrdiff_t>(s_.buffer_origin), s_.input.end(),
                      buffer_.begin());
            buffer_[used] = 0;
            exposed_ = true;
        }
        return buffer_;
    }
    std::int32_t parsing_buffer_length() const override {
        return static_cast<std::int32_t>(s_.input.size() - s_.buffer_origin);
    }
    std::int32_t current_position() const override {
        return relative(s_.cursor);
    }
    void current_position(std::int32_t value) override {
        sync();
        s_.cursor = s_.buffer_origin + static_cast<std::size_t>(value);
        s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(s_.cursor) - line_start_);
    }
    std::int32_t line_number() const override {
        return s_.line;
    }
    std::int32_t line_start_position() const override {
        return static_cast<std::int32_t>(static_cast<std::int64_t>(s_.cursor) - s_.column -
                                         static_cast<std::int64_t>(s_.buffer_origin));
    }
    bool is_eof() const override {
        return s_.char_eof;
    }
    std::int32_t entity_stack_length() const override {
        return static_cast<std::int32_t>(s_.entity_inputs.size());
    }
    bool entity_eol_normalized() const override {
        return s_.entity_eol_normalized;
    }
    bool v1_compatibility() const override {
        return s_.settings.legacy_compatibility;
    }
    bool normalization() const override {
        return s_.settings.normalize;
    }
    bool namespaces() const override {
        return true;
    }
    bool validation_enabled() const override {
        return false;
    }
    bool has_validation_handler() const override {
        return false;
    }
    std::int32_t read_data() override {
        sync();
        exposed_ = false;
        auto count = s_.read_data();
        invalidate();
        return static_cast<std::int32_t>(count);
    }
    void on_new_line(std::int32_t next) override {
        s_.line = unchecked_add(s_.line, 1);
        line_start_ = static_cast<std::int64_t>(s_.buffer_origin) + next - 1;
        s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(s_.cursor) - line_start_);
        s_.previous_cr = false;
    }
    std::int32_t parse_numeric_reference(std::u16string* builder) override {
        sync();
        exposed_ = false;
        for (;;) {
            auto start = s_.cursor;
            auto i = start + 2;
            auto get = [&](std::size_t n) { return n < s_.input.size() ? s_.input[n] : char16_t{}; };
            bool hex = get(i) == u'x';
            if (hex)
                ++i;
            auto digits = i;
            std::int32_t code = 0;
            auto bad = [&](std::u16string message, std::size_t position) {
                s_.cursor = position;
                s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(position) - line_start_);
                s_.error(std::move(message));
            };
            while (i < s_.input.size()) {
                auto c = get(i);
                unsigned digit = c >= u'0' && c <= u'9'          ? unsigned(c - u'0')
                                 : hex && c >= u'a' && c <= u'f' ? unsigned(c - u'a' + 10)
                                 : hex && c >= u'A' && c <= u'F' ? unsigned(c - u'A' + 10)
                                                                 : 99;
                if (digit >= unsigned(hex ? 16 : 10))
                    break;
                if (hex) {
                    if (code >
                        (std::numeric_limits<std::int32_t>::max() - static_cast<std::int32_t>(digit)) / 16)
                        bad(u"Invalid value of a character entity reference.", i);
                    code = code * 16 + static_cast<std::int32_t>(digit);
                } else {
                    if (code > std::numeric_limits<std::int32_t>::max() / 10 ||
                        code * 10 > std::numeric_limits<std::int32_t>::max() - static_cast<std::int32_t>(c))
                        bad(u"Invalid value of a character entity reference.", i);
                    code = code * 10 + static_cast<std::int32_t>(c) - 48;
                }
                ++i;
            }
            if (i == s_.input.size()) {
                if (s_.read_data() == 0)
                    s_.error(u"Unexpected end of file while parsing  has occurred.");
                invalidate();
                continue;
            }
            if (get(i) != u';' || digits == i)
                bad(hex ? u"Invalid syntax for a hexadecimal numeric entity reference."
                        : u"Invalid syntax for a decimal numeric entity reference.",
                    i);
            std::u16string value;
            if (code <= 65535) {
                auto c = static_cast<char16_t>(code);
                if ((nini_xml_character_properties(c) & 16) == 0 &&
                    (!s_.settings.legacy_compatibility || s_.settings.normalize)) {
                    auto a = character_arguments(c);
                    bad(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.",
                        digits);
                }
                value += c;
            } else {
                auto n = code - 65536;
                auto low = static_cast<char16_t>(0xdc00 + n % 1024),
                     high = static_cast<char16_t>(0xd800 + n / 1024);
                if (s_.settings.normalize &&
                    (high < 0xd800 || high > 0xdbff || low < 0xdc00 || low > 0xdfff)) {
                    auto a = character_arguments(high, low);
                    bad(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.",
                        digits);
                }
                value = {high, low};
            }
            auto end = i + 1;
            if (builder)
                builder->append(s_.input, start, end - start);
            auto result = end - value.size();
            std::copy(value.begin(), value.end(), s_.input.begin() + static_cast<std::ptrdiff_t>(result));
            s_.cursor = result;
            s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(result) - line_start_);
            invalidate();
            return relative(end);
        }
    }
    std::int32_t parse_named_reference(bool expand, std::u16string* builder) override {
        sync();
        exposed_ = false;
        for (;;) {
            auto position = s_.cursor + 1;
            auto get = [&](std::size_t n) { return n < s_.input.size() ? s_.input[n] : char16_t{}; };
            std::u16string_view suffix;
            char16_t value = 0;
            switch (get(position)) {
            case u'a':
                ++position;
                if (get(position) == u'm') {
                    suffix = u"mp;";
                    value = u'&';
                } else if (get(position) == u'p') {
                    suffix = u"pos;";
                    value = u'\'';
                } else if (position < s_.input.size()) {
                    invalidate();
                    return -1;
                }
                break;
            case u'g':
                suffix = u"gt;";
                value = u'>';
                break;
            case u'l':
                suffix = u"lt;";
                value = u'<';
                break;
            case u'q':
                suffix = u"quot;";
                value = u'"';
                break;
            default:
                invalidate();
                return -1;
            }
            if (s_.input.size() - std::min(position, s_.input.size()) < suffix.size() || suffix.empty()) {
                if (s_.read_data() != 0) {
                    invalidate();
                    continue;
                }
                invalidate();
                return -1;
            }
            if (std::u16string_view(s_.input).substr(position, suffix.size()) != suffix) {
                invalidate();
                return -1;
            }
            auto end = position + suffix.size();
            if (expand) {
                if (builder)
                    builder->append(s_.input, s_.cursor, end - s_.cursor);
                s_.input[end - 1] = value;
                s_.cursor = end - 1;
                s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(s_.cursor) - line_start_);
            }
            invalidate();
            return relative(end);
        }
    }
    void parse_processing_instruction(std::u16string* builder) override {
        sync();
        exposed_ = false;
        s_.current.line_number = s_.line;
        s_.current.line_position = s_.column;
        auto target_start = s_.cursor;
        if ((nini_xml_character_properties(s_.peek()) & 4) == 0 && !s_.ensure(2))
            s_.error(u"Unexpected end of file while parsing Name has occurred.");
        auto target = s_.name(false);
        bool reserved = target.size() == 3 && (target[0] == u'x' || target[0] == u'X') &&
                        (target[1] == u'm' || target[1] == u'M') && (target[2] == u'l' || target[2] == u'L');
        if (reserved) {
            s_.cursor = target_start;
            s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(target_start) - line_start_);
            s_.error(
                target == u"xml"
                    ? u"Unexpected XML declaration. The XML declaration must be the first node in the document, and no whitespace characters are allowed to appear before it."
                    : u"'" + target + u"' is an invalid name for processing instructions.");
        }
        if (builder)
            *builder += target;
        bool separated = false;
        while (!s_.at_end() && s_.white(s_.peek())) {
            separated = true;
            auto before = s_.cursor;
            auto lone_cr = s_.peek() == u'\r' && s_.peek(1) != u'\n';
            auto value = s_.read_character();
            if (lone_cr && !s_.entity_eol_normalized)
                s_.input[before] = u'\n';
            if (builder)
                *builder += value;
        }
        if (!separated && !s_.starts(u"?>")) {
            auto a = character_arguments(s_.peek(), s_.peek(1));
            s_.error(u"The '" + a.first + u"' character, hexadecimal value " + a.second +
                     u", cannot be included in a name.");
        }
        invalidate();
        scan_markup(true, builder);
    }
    void scan_markup(bool pi, std::u16string* builder) {
        sync();
        exposed_ = false;
        std::u16string value;
        try {
            for (;;) {
                if (s_.input.size() - s_.cursor < (pi ? 2u : 3u) && s_.read_data() == 0) {
                    if (pi) {
                        s_.cursor = s_.input.size();
                        s_.column =
                            static_cast<std::int32_t>(static_cast<std::int64_t>(s_.cursor) - line_start_);
                    }
                    s_.error(pi ? u"Unexpected end of file while parsing PI has occurred."
                                : u"Unexpected end of file while parsing Comment has occurred.",
                             s_.line,
                             pi ? static_cast<std::int32_t>(static_cast<std::int64_t>(s_.input.size()) -
                                                            line_start_)
                                : s_.column,
                             "Xml_UnexpectedEOF");
                }
                auto begin = s_.cursor, i = begin;
                std::size_t removed = 0, shift = 0;
                bool finished = false;
                auto get = [&](std::size_t n) { return n < s_.input.size() ? s_.input[n] : char16_t{}; };
                auto bad = [&](std::u16string message, std::size_t position) {
                    s_.cursor = position;
                    s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(position) - line_start_);
                    s_.error(std::move(message));
                };
                for (;;) {
                    auto c = get(i);
                    if (pi && c == u'?') {
                        if (get(i + 1) == u'>') {
                            finished = true;
                            break;
                        }
                        if (i + 1 == s_.input.size())
                            break;
                        ++i;
                        continue;
                    }
                    if (!pi && c == u'-') {
                        if (get(i + 1) == u'-') {
                            if (get(i + 2) == u'>') {
                                finished = true;
                                break;
                            }
                            if (i + 2 == s_.input.size())
                                break;
                            bad(u"An XML comment cannot contain '--', and '-' cannot be the last character.",
                                i);
                        } else if (i + 1 == s_.input.size())
                            break;
                        ++i;
                        continue;
                    }
                    if (c == u'\n') {
                        ++i;
                        s_.line = unchecked_add(s_.line, 1);
                        line_start_ = static_cast<std::int64_t>(i) - 1;
                        continue;
                    }
                    if (c == u'\r') {
                        if (get(i + 1) == u'\n') {
                            if (!s_.entity_eol_normalized) {
                                if (i - begin > 0) {
                                    if (removed == 0) {
                                        removed = 1;
                                        shift = i;
                                    } else {
                                        std::move(s_.input.begin() +
                                                      static_cast<std::ptrdiff_t>(shift + removed),
                                                  s_.input.begin() + static_cast<std::ptrdiff_t>(i),
                                                  s_.input.begin() + static_cast<std::ptrdiff_t>(shift));
                                        shift = i - removed;
                                        ++removed;
                                    }
                                } else {
                                    ++begin;
                                    s_.cursor = begin;
                                }
                            }
                            i += 2;
                        } else {
                            if (i + 1 >= s_.input.size() && !s_.char_eof)
                                break;
                            if (!s_.entity_eol_normalized)
                                s_.input[i] = u'\n';
                            ++i;
                        }
                        s_.line = unchecked_add(s_.line, 1);
                        line_start_ = static_cast<std::int64_t>(i) - 1;
                        continue;
                    }
                    if (i == s_.input.size())
                        break;
                    if ((nini_xml_character_properties(c) & 16) != 0) {
                        ++i;
                        continue;
                    }
                    if (c >= 0xd800 && c <= 0xdbff) {
                        if (i + 1 == s_.input.size())
                            break;
                        ++i;
                        if (get(i) >= 0xdc00 && get(i) <= 0xdfff) {
                            ++i;
                            continue;
                        }
                    }
                    auto a = character_arguments(get(i), get(i + 1));
                    bad(u"'" + a.first + u"', hexadecimal value " + a.second + u", is an invalid character.",
                        i);
                }
                if (removed)
                    std::move(s_.input.begin() + static_cast<std::ptrdiff_t>(shift + removed),
                              s_.input.begin() + static_cast<std::ptrdiff_t>(i),
                              s_.input.begin() + static_cast<std::ptrdiff_t>(shift));
                value.append(s_.input, begin, i - removed - begin);
                s_.cursor = i + (finished ? (pi ? 2 : 3) : 0);
                s_.column = static_cast<std::int32_t>(static_cast<std::int64_t>(s_.cursor) - line_start_);
                if (finished) {
                    if (builder)
                        *builder += value;
                    break;
                }
            }
        } catch (const NiniXmlDtdError& e) {
            if (pi || e.resource_name != "Xml_UnexpectedEOF" || !s_.current_entity)
                throw;
            s_.parsing_error = false;
        }
        invalidate();
    }
    void parse_comment(std::u16string* builder) override {
        scan_markup(false, builder);
    }
    bool push_entity(const std::shared_ptr<NiniXmlDtdEntity>& entity, std::int32_t& id) override {
        sync();
        exposed_ = false;
        auto result = s_.push_dtd_entity(entity, id);
        invalidate();
        return result;
    }
    bool pop_entity(std::shared_ptr<NiniXmlDtdEntity>& old, std::int32_t& id) override {
        sync();
        auto result = s_.pop_dtd_entity(old, id);
        if (result)
            invalidate();
        return result;
    }
    bool push_external_subset(NiniWriterText, NiniWriterText) override {
        return false;
    }
    void push_internal_dtd(NiniWriterText base, NiniWriterText subset) override {
        sync();
        exposed_ = false;
        s_.push_string_input(base, subset, false, true);
        s_.current_entity.reset();
        s_.current_entity_id = 0;
        invalidate();
    }
    void on_system_id(NiniWriterText text, NiniXmlDtdLine keyword, NiniXmlDtdLine literal) override {
        id_attribute(u"SYSTEM", std::move(text), keyword, literal);
    }
    void on_public_id(NiniWriterText text, NiniXmlDtdLine keyword, NiniXmlDtdLine literal) override {
        id_attribute(u"PUBLIC", std::move(text), keyword, literal);
    }
    void validation_event(const NiniXmlProviderError&, std::int32_t) override {
    }
    [[noreturn]] void throw_error(NiniXmlDtdError e) override {
        s_.state = NiniXmlReadState::Error;
        s_.parsing_error = true;
        s_.current.line_number = e.line_number.value_or(0);
        s_.current.line_position = e.line_position.value_or(0);
        throw e;
    }
};
std::unique_ptr<NiniXmlDtdAdapter>
native_nini_xml_dtd_adapter(const std::shared_ptr<NativeNiniXmlReader>& reader) {
    if (!reader)
        throw std::invalid_argument("Native DTD adapter requires a reader");
    return std::make_unique<NativeNiniXmlReaderDtdAdapter>(reader);
}
bool NativeNiniXmlReader::is_default() const {
    auto& s = *state_;
    return s.attribute >= 0 && !s.attribute_value &&
           s.element.attributes[static_cast<std::size_t>(s.attribute)].is_default;
}
std::shared_ptr<NiniXmlDtdInfo> NativeNiniXmlReader::dtd_info() const {
    return state_->dtd_info;
}
bool NativeNiniXmlReader::read() {
    const auto lifetime = shared_from_this();
    auto result = state_->read();
    state_->remember_node();
    return result;
}
bool NativeNiniXmlReader::move_to_first_attribute() {
    if (state_->element.attributes.empty())
        return false;
    state_->finish_attribute_value();
    state_->attribute = -1;
    return move_to_next_attribute();
}
bool NativeNiniXmlReader::move_to_next_attribute() {
    auto& s = *state_;
    s.finish_attribute_value();
    if ((s.element.node_type != 1 && s.element.node_type != 17 && s.element.node_type != 10) ||
        s.attribute + 1 >= static_cast<std::ptrdiff_t>(s.element.attributes.size()))
        return false;
    ++s.attribute;
    s.attribute_value = false;
    auto& a = s.element.attributes[static_cast<std::size_t>(s.attribute)];
    s.current = s.element;
    s.current.node_type = 2;
    s.current.depth = s.element.depth + 1;
    s.current.name = a.name;
    s.current.prefix = a.prefix;
    s.current.local_name = a.local_name;
    s.current.namespace_uri = a.namespace_uri;
    s.current.value = a.value;
    s.current.line_number = a.line_number;
    s.current.line_position = a.line_position;
    s.current.is_empty_element = false;
    return true;
}
bool NativeNiniXmlReader::move_to_element() {
    auto& s = *state_;
    if (s.attribute < 0)
        return false;
    s.finish_attribute_value();
    s.attribute = -1;
    s.attribute_value = false;
    s.current = s.element;
    return true;
}
bool NativeNiniXmlReader::read_attribute_value() {
    return state_->next_attribute_value();
}
void NativeNiniXmlReader::resolve_entity() {
    auto& s = *state_;
    if (s.current.node_type != 5)
        throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509,
                                   u"Operation is not valid due to the current state of the object.");
    auto entity = s.lookup_general(s.current.name, s.current.line_position, s.attribute_value);
    auto ref_line = s.current.line_number, ref_pos = s.current.line_position;
    s.enter_general(entity, true);
    s.reader_entities.back().reference_line = ref_line;
    s.reader_entities.back().reference_position = ref_pos;
    if (!s.attribute_value)
        s.after_resolve_content = true;
}
void NativeNiniXmlReader::close() {
    const auto lifetime = shared_from_this();
    auto& s = *state_;
    if (s.state == NiniXmlReadState::Closed)
        return;
    while (!s.entity_inputs.empty()) {
        if (s.stream)
            s.stream->close();
        if (s.text_input)
            s.text_input->close();
        auto previous = std::move(s.entity_inputs.back());
        s.entity_inputs.pop_back();
        s.restore_input(std::move(previous));
    }
    if (s.stream && s.settings.close_input)
        s.stream->close();
    if (s.text_input && s.settings.close_input)
        s.text_input->close();
    s.current = {};
    s.element = {};
    s.attribute = -1;
    s.state = NiniXmlReadState::Closed;
}
std::vector<NiniXmlReadAttribute> native_nini_xml_declaration_attributes(Text value) {
    auto wrapped = nini_writer_text(u"<?xml " + (value ? *value : u"") + u"?>");
    auto reader = NativeNiniXmlReader::from_text(wrapped, NiniXmlReaderSettings::legacy_document());
    try {
        reader->read();
        return reader->token().attributes;
    } catch (const NiniXmlProviderError& original) {
        auto line = original.line_number.value_or(0), position = original.line_position.value_or(0);
        auto message = original.message;
        auto suffix = u" Line " + decimal(line) + u", position " + decimal(position) + u".";
        if (line != 0 && std::u16string_view(message).ends_with(suffix))
            message.resize(message.size() - suffix.size());
        position -= 6;
        if (line != 0)
            message += u" Line " + decimal(line) + u", position " + decimal(position) + u".";
        NiniXmlProviderError e(original.source_type, original.hresult, std::move(message),
                               original.parameter);
        e.source_uri = original.source_uri;
        e.line_number = line;
        e.line_position = position;
        throw e;
    }
}
void native_nini_xml_load(NiniXmlDom& document, const std::shared_ptr<NativeNiniXmlReader>& reader,
                          bool preserve_whitespace) {
    auto collection = document.defer_collection();
    document.clear();
    if (!reader)
        throw NiniXmlProviderError("System.NullReferenceException", 0x80004003,
                                   u"Object reference not set to an instance of an object.");
    using Node = std::shared_ptr<NiniXmlDomNode>;
    auto text = [](const std::u16string& value) { return nini_writer_text(value); };
    // XmlLoader uses reader attribute navigation/value iteration. Besides entity
    // chunks, those calls affect the source reader's reusable NodeData slots.
    auto load_attribute = [&]() -> Node {
        auto token = reader->token();
        bool is_default = reader->is_default();
        auto attribute =
            document.create_attribute(text(token.prefix), text(token.local_name), text(token.namespace_uri));
        Node parent = attribute;
        while (reader->read_attribute_value()) {
            auto value = reader->token();
            if (value.node_type == 3)
                parent->append_child_for_load(document.create_text(text(value.value)));
            else if (value.node_type == 5) {
                auto reference = document.create_entity_reference(text(value.name));
                reader->resolve_entity();
                parent->append_child_for_load(reference);
                parent = reference;
            } else if (value.node_type == 16) {
                if (parent->node_type() != 5)
                    throw std::logic_error("EndEntity attribute parent mismatch");
                auto children = parent->children();
                if (!children->move_next())
                    parent->append_child_for_load(document.create_text(text(u"")));
                parent = parent->parent();
            } else
                throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509,
                                           u"Unexpected XmlNodeType.");
        }
        attribute->specified_for_load(!is_default);
        return attribute;
    };
    auto load_document_type = [&]() -> Node {
        auto token = reader->token();
        Text pub, sys;
        while (reader->move_to_next_attribute()) {
            auto& attr = reader->token();
            if (attr.name == u"PUBLIC")
                pub = text(attr.value);
            else if (attr.name == u"SYSTEM")
                sys = text(attr.value);
        }
        return document.create_document_type_for_load(text(token.local_name), std::move(pub), std::move(sys),
                                                      text(token.value), reader->dtd_info());
    };
    auto load_declaration = [&]() -> Node {
        Text version, encoding, standalone;
        while (reader->move_to_next_attribute()) {
            const auto& a = reader->token();
            if (a.name == u"version")
                version = text(a.value);
            else if (a.name == u"encoding")
                encoding = text(a.value);
            else if (a.name == u"standalone")
                standalone = text(a.value);
        }
        return document.create_declaration(version, encoding, standalone);
    };
    // Pinned XmlLoader.LoadNode uses parent links, not recursive descent. A valid
    // deeply nested document therefore does not consume the native call stack.
    auto load_node = [&]() -> Node {
        Node parent;
        do {
            auto token = reader->token();
            Node node;
            switch (token.node_type) {
            case 1: {
                auto element = document.create_element(text(token.prefix), text(token.local_name),
                                                       text(token.namespace_uri));
                element->is_empty_element(token.is_empty_element);
                if (reader->move_to_first_attribute()) {
                    do {
                        element->append_attribute(load_attribute());
                    } while (reader->move_to_next_attribute());
                    reader->move_to_element();
                }
                if (!token.is_empty_element) {
                    if (parent)
                        parent->append_child_for_load(element);
                    parent = std::move(element);
                    continue;
                }
                node = std::move(element);
                break;
            }
            case 15: {
                if (!parent)
                    return {};
                auto upper = parent->parent();
                if (!upper)
                    return parent;
                parent = std::move(upper);
                continue;
            }
            case 16: {
                if (!parent)
                    return {};
                if (parent->node_type() != 5)
                    throw std::logic_error("EndEntity content parent mismatch");
                auto children = parent->children();
                if (!children->move_next())
                    parent->append_child_for_load(document.create_text(text(u"")));
                auto upper = parent->parent();
                if (!upper)
                    return parent;
                parent = std::move(upper);
                continue;
            }
            case 2:
                node = load_attribute();
                break;
            case 3:
                node = document.create_text(text(token.value));
                break;
            case 4:
                node = document.create_cdata(text(token.value));
                break;
            case 7:
                node = document.create_processing_instruction(text(token.name), text(token.value));
                break;
            case 8:
                node = document.create_comment(text(token.value));
                break;
            case 13:
                if (!preserve_whitespace)
                    continue;
                node = document.create_whitespace(text(token.value));
                break;
            case 14:
                node = document.create_whitespace(text(token.value), true);
                break;
            case 17:
                node = load_declaration();
                break;
            case 5: {
                auto reference = document.create_entity_reference(text(token.name));
                reader->resolve_entity();
                if (parent)
                    parent->append_child_for_load(reference);
                parent = std::move(reference);
                continue;
            }
            case 10:
                node = load_document_type();
                break;
            default:
                throw NiniXmlProviderError("System.InvalidOperationException", 0x80131509,
                                           u"Unexpected XmlNodeType.");
            }
            if (parent) {
                parent->append_child_for_load(node);
                continue;
            }
            return node;
        } while (reader->read());
        if (parent)
            while (auto upper = parent->parent())
                parent = std::move(upper);
        return parent;
    };
    if (reader->read_state() != NiniXmlReadState::Interactive && !reader->read())
        return;
    do {
        if (reader->token().node_type == 13 && !preserve_whitespace)
            continue;
        auto node = load_node();
        if (!node)
            return;
        document.document_node()->append_child(node);
    } while (reader->read());
}
}
