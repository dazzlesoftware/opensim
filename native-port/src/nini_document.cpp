// Port of Nini.Ini document classes; copyright (c) 2006 Brent R. Matzelle.
// MIT license: ../third_party/nini/LICENSE.
#include "opensim/framework/nini_document.hpp"
#include "opensim/framework/text.hpp"
#include <bit>
#include <unordered_map>
namespace opensim::framework {
namespace {
std::u16string nini_number(std::int32_t n) {
    auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
std::u16string nini_newline() {
#ifdef _WIN32
    return u"\r\n";
#else
    return u"\n";
#endif
}
std::u16string val(const NiniWriterText& s) {
    return s ? *s : std::u16string{};
}
[[noreturn]] void nullref() {
    throw NiniDocumentError(NiniDocumentError::Kind::NullReference,
                            u"Object reference not set to an instance of an object.");
}
void keycheck(const NiniWriterText& key) {
    if (!key)
        throw NiniDocumentError(NiniDocumentError::Kind::ArgumentNull,
                                u"Value cannot be null. (Parameter 'key')", u"key");
}
[[noreturn]] void indexerror() {
    throw NiniDocumentError(
        NiniDocumentError::Kind::OutOfRange,
        u"Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')",
        u"index");
}
[[noreturn]] void duplicate(const NiniWriterText& key) {
    throw NiniDocumentError(NiniDocumentError::Kind::Argument,
                            u"Item has already been added. Key in dictionary: '" + val(key) +
                                u"'  Key being added: '" + val(key) + u"'");
}
}
NiniDocumentError::NiniDocumentError(Kind k, std::u16string m, std::optional<std::u16string> p)
    : runtime_error(utf8(m)), kind(k), message(std::move(m)), parameter(std::move(p)) {
}
const char* NiniDocumentError::source_type() const noexcept {
    switch (kind) {
    case Kind::Argument:
        return "System.ArgumentException";
    case Kind::ArgumentNull:
        return "System.ArgumentNullException";
    case Kind::OutOfRange:
        return "System.ArgumentOutOfRangeException";
    case Kind::InvalidOperation:
        return "System.InvalidOperationException";
    case Kind::NullReference:
        return "System.NullReferenceException";
    case Kind::IndexOutOfRange:
        return "System.IndexOutOfRangeException";
    default:
        return "System.InvalidCastException";
    }
}
std::int32_t NiniDocumentError::hresult() const noexcept {
    std::uint32_t v;
    switch (kind) {
    case Kind::Argument:
        v = 0x80070057u;
        break;
    case Kind::ArgumentNull:
    case Kind::NullReference:
        v = 0x80004003u;
        break;
    case Kind::OutOfRange:
        v = 0x80131502u;
        break;
    case Kind::InvalidOperation:
        v = 0x80131509u;
        break;
    case Kind::IndexOutOfRange:
        v = 0x80131508u;
        break;
    default:
        v = 0x80004002u;
        break;
    }
    return std::bit_cast<std::int32_t>(v);
}
struct NiniSection::State {
    NiniWriterText name, comment;
    std::int32_t comments = 0;
    std::vector<std::pair<NiniWriterText, std::shared_ptr<NiniItem>>> items;
    std::unordered_map<std::u16string, std::shared_ptr<NiniItem>> table;
};
NiniSection::NiniSection(NiniWriterText n, NiniWriterText c) : state_(std::make_shared<State>()) {
    state_->name = std::move(n);
    state_->comment = std::move(c);
}
NiniWriterText NiniSection::name() const {
    return state_->name;
}
NiniWriterText NiniSection::comment() const {
    return state_->comment;
}
std::int32_t NiniSection::item_count() const {
    return static_cast<std::int32_t>(state_->items.size());
}
bool NiniSection::contains(const NiniWriterText& k) const {
    keycheck(k);
    return state_->table.contains(*k);
}
NiniWriterText NiniSection::get_value(const NiniWriterText& k) const {
    if (!contains(k))
        return {};
    return state_->table.at(*k)->value();
}
std::shared_ptr<NiniItem> NiniSection::get_item(std::int32_t i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= state_->items.size())
        indexerror();
    return state_->items[static_cast<std::size_t>(i)].second;
}
std::vector<NiniWriterText> NiniSection::get_keys() const {
    std::vector<NiniWriterText> keys;
    for (auto& [k, item] : state_->items)
        if (item->type() == NiniType::Key)
            keys.push_back(item->name());
    return keys;
}
void NiniSection::set(NiniWriterText k, NiniWriterText v, NiniWriterText c) {
    if (contains(k)) {
        auto item = state_->table.at(*k);
        item->value(std::move(v));
        item->comment(std::move(c));
    } else {
        auto item = std::make_shared<NiniItem>(k, std::move(v), NiniType::Key, std::move(c));
        state_->table.emplace(*k, item);
        state_->items.emplace_back(std::move(k), std::move(item));
    }
}
void NiniSection::set(NiniWriterText c) {
    auto number = std::to_string(state_->comments);
    auto k = nini_writer_text(u"#comment" + std::u16string(number.begin(), number.end()));
    auto item = std::make_shared<NiniItem>(k, nullptr, NiniType::Empty, std::move(c));
    if (state_->table.contains(*k))
        duplicate(k);
    state_->table.emplace(*k, item);
    state_->items.emplace_back(k, std::move(item));
    state_->comments = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(state_->comments) + 1);
}
void NiniSection::set() {
    set(NiniWriterText{});
}
void NiniSection::remove(const NiniWriterText& k) {
    if (!contains(k))
        return;
    state_->table.erase(*k);
    for (auto i = state_->items.begin(); i != state_->items.end(); ++i)
        if (*i->first == *k) {
            state_->items.erase(i);
            return;
        }
}
struct NiniSectionCollection::State {
    std::vector<NiniSectionEntry> entries;
    std::unordered_map<std::u16string, std::shared_ptr<NiniSection>> table;
};
NiniSectionCollection::NiniSectionCollection() : state_(std::make_shared<State>()) {
}
std::shared_ptr<NiniSection> NiniSectionCollection::at(std::int32_t i) const {
    if (i < 0 || static_cast<std::size_t>(i) >= state_->entries.size())
        indexerror();
    return state_->entries[static_cast<std::size_t>(i)].value;
}
std::shared_ptr<NiniSection> NiniSectionCollection::at(const NiniWriterText& k) const {
    keycheck(k);
    auto i = state_->table.find(*k);
    return i == state_->table.end() ? nullptr : i->second;
}
std::int32_t NiniSectionCollection::count() const {
    return static_cast<std::int32_t>(state_->entries.size());
}
std::shared_ptr<const void> NiniSectionCollection::sync_root() const {
    return state_;
}
void NiniSectionCollection::add(std::shared_ptr<NiniSection> s) {
    if (!s)
        keycheck(nullptr);
    auto k = s->name();
    keycheck(k);
    if (state_->table.contains(*k))
        duplicate(k);
    state_->table.emplace(*k, s);
    state_->entries.push_back({k, std::move(s)});
}
void NiniSectionCollection::remove(const NiniWriterText& k) {
    keycheck(k);
    state_->table.erase(*k);
    for (auto i = state_->entries.begin(); i != state_->entries.end(); ++i)
        if (*i->key == *k) {
            state_->entries.erase(i);
            return;
        }
    indexerror();
}
NiniSectionCollection::Enumerator NiniSectionCollection::get_enumerator() const {
    return Enumerator(state_);
}
bool NiniSectionCollection::Enumerator::move_next() {
    index_ = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(index_) + 1);
    return index_ < static_cast<std::int32_t>(state_->entries.size());
}
void NiniSectionCollection::Enumerator::reset() {
    index_ = -1;
}
NiniSectionEntry NiniSectionCollection::Enumerator::current() const {
    if (index_ < 0 || static_cast<std::size_t>(index_) >= state_->entries.size())
        throw NiniDocumentError(NiniDocumentError::Kind::InvalidOperation,
                                u"Operation is not valid due to the current state of the object.");
    return state_->entries[static_cast<std::size_t>(index_)];
}
NiniEntryArray::NiniEntryArray(std::int32_t n) : values_(static_cast<std::size_t>(n)) {
}
void NiniEntryArray::set(std::int32_t i, const NiniSectionEntry& e) {
    values_.at(static_cast<std::size_t>(i)) = e;
}
void NiniSectionCollection::copy_to(const std::shared_ptr<NiniSectionArray>& a, std::int32_t index) const {
    if (!a)
        throw NiniDocumentError(NiniDocumentError::Kind::ArgumentNull,
                                u"Value cannot be null. (Parameter 'array')", u"array");
    if (a->rank() != 1)
        throw NiniDocumentError(
            NiniDocumentError::Kind::Argument,
            u"Only single dimensional arrays are supported for the requested action. (Parameter 'array')",
            u"array");
    if (index < 0)
        throw NiniDocumentError(NiniDocumentError::Kind::OutOfRange,
                                u"arrayIndex ('" + nini_number(index) +
                                    u"') must be a non-negative value. (Parameter 'arrayIndex')" +
                                    nini_newline() + u"Actual value was " + nini_number(index) + u".",
                                u"arrayIndex");
    if (a->length() - index < count())
        throw NiniDocumentError(
            NiniDocumentError::Kind::Argument,
            u"Destination array is not long enough to copy all the items in the collection. Check array index and length.");
    for (auto& [k, s] : state_->table)
        a->set(index++, {s->name(), s});
}
void NiniSectionCollection::copy_to(const std::shared_ptr<std::vector<std::shared_ptr<NiniSection>>>& values,
                                    std::int32_t index) const {
    class SectionArray final : public NiniSectionArray {
        std::shared_ptr<std::vector<std::shared_ptr<NiniSection>>> values_;

      public:
        explicit SectionArray(std::shared_ptr<std::vector<std::shared_ptr<NiniSection>>> v)
            : values_(std::move(v)) {
        }
        std::int32_t rank() const override {
            return 1;
        }
        std::int32_t lower_bound() const override {
            return 0;
        }
        std::int32_t length() const override {
            return static_cast<std::int32_t>(values_->size());
        }
        void set(std::int32_t, const NiniSectionEntry&) override {
            throw NiniDocumentError(NiniDocumentError::Kind::InvalidCast,
                                    u"Object cannot be stored in an array of this type.");
        }
    };
    std::shared_ptr<NiniSectionArray> array;
    if (values)
        array = std::make_shared<SectionArray>(values);
    copy_to(array, index);
}

struct NiniDocument::State {
    NiniFileType type = NiniFileType::Standard;
    NiniSectionCollection sections;
    std::vector<NiniWriterText> initial;
    std::uint64_t version = 0;
    std::vector<std::shared_ptr<NiniWriter>> writers;
    std::vector<std::shared_ptr<NiniReader>> readers;
};
NiniDocument::NiniDocument() : state_(std::make_shared<State>()) {
}
NiniDocument::NiniDocument(std::shared_ptr<NiniTextReader> r, NiniFileType t) : NiniDocument() {
    file_type(t);
    load(std::move(r));
}
NiniDocument::NiniDocument(std::shared_ptr<NiniReader> r) : NiniDocument() {
    load(std::move(r));
}
NiniDocument::NiniDocument(NiniWriterText p, const NiniDocumentFactory& f, NiniFileType t) : NiniDocument() {
    file_type(t);
    load(std::move(p), f);
}
NiniDocument::NiniDocument(std::shared_ptr<NiniWriterStream> p, const NiniDocumentFactory& f, NiniFileType t)
    : NiniDocument() {
    file_type(t);
    load(std::move(p), f);
}
NiniFileType NiniDocument::file_type() const {
    return state_->type;
}
void NiniDocument::file_type(NiniFileType t) {
    state_->type = t;
}
NiniSectionCollection NiniDocument::sections() const {
    return state_->sections;
}
void NiniDocument::load(std::shared_ptr<NiniTextReader> input) {
    auto reader = std::make_shared<NiniReader>(std::move(input));
    switch (state_->type) {
    case NiniFileType::PythonStyle:
        reader->accept_comment_after_key(false);
        reader->comment_delimiters(std::make_shared<std::u16string>(u";#"));
        reader->assign_delimiters(std::make_shared<std::u16string>(u":"));
        break;
    case NiniFileType::SambaStyle:
        reader->accept_comment_after_key(false);
        reader->comment_delimiters(std::make_shared<std::u16string>(u";#"));
        reader->line_continuation(true);
        break;
    case NiniFileType::MysqlStyle:
        reader->accept_comment_after_key(false);
        reader->accept_no_assignment(true);
        reader->comment_delimiters(std::make_shared<std::u16string>(u"#"));
        reader->assign_delimiters(std::make_shared<std::u16string>(u":="));
        break;
    case NiniFileType::WindowsStyle:
        reader->consume_all_key_text(true);
        break;
    default:
        break;
    }
    state_->readers.push_back(reader);
    load(std::move(reader));
}
void NiniDocument::load(std::shared_ptr<NiniReader> r) {
    auto s = state_;
    if (!r)
        nullref();
    r->ignore_comments(false);
    std::shared_ptr<NiniSection> section;
    bool found = false;
    try {
        while (r->read())
            switch (r->type()) {
            case NiniType::Empty:
                if (!found) {
                    s->initial.push_back(nini_writer_text(r->comment()));
                    ++s->version;
                } else
                    section->set(nini_writer_text(r->comment()));
                break;
            case NiniType::Section: {
                found = true;
                auto name = nini_writer_text(r->name());
                if (s->sections.at(name))
                    s->sections.remove(name);
                section = std::make_shared<NiniSection>(name, nini_writer_text(r->comment()));
                s->sections.add(section);
                break;
            }
            case NiniType::Key:
                if (!section)
                    nullref();
                if (!section->get_value(nini_writer_text(r->name())))
                    section->set(nini_writer_text(r->name()), nini_writer_text(r->value()),
                                 nini_writer_text(r->comment()));
                break;
            }
    } catch (...) {
        r->close();
        throw;
    }
    r->close();
}
void NiniDocument::load(NiniWriterText p, const NiniDocumentFactory& f) {
    if (!f.file_reader)
        throw std::invalid_argument("Missing Nini file reader provider");
    load(f.file_reader(p));
}
void NiniDocument::load(std::shared_ptr<NiniWriterStream> p, const NiniDocumentFactory& f) {
    if (!f.stream_reader)
        throw std::invalid_argument("Missing Nini stream reader provider");
    load(f.stream_reader(std::move(p)));
}
std::size_t NiniDocument::retired_reader_count() const {
    return state_->readers.size();
}
void NiniDocument::finalize_retired_readers() {
    auto s = state_;
    while (!s->readers.empty()) {
        auto reader = s->readers.front();
        s->readers.erase(s->readers.begin());
        reader->dispose();
    }
}
std::size_t NiniDocument::retired_writer_count() const {
    return state_->writers.size();
}
void NiniDocument::finalize_retired_writers() {
    auto s = state_;
    while (!s->writers.empty()) {
        auto writer = s->writers.front();
        s->writers.erase(s->writers.begin());
        writer->finalize();
    }
}
void NiniDocument::save(std::shared_ptr<NiniTextWriter> output) {
    auto s = state_;
    auto w = std::make_shared<NiniWriter>(std::move(output));
    s->writers.push_back(w);
    switch (s->type) {
    case NiniFileType::PythonStyle:
        w->assign_delimiter(u':');
        w->comment_delimiter(u'#');
        break;
    case NiniFileType::SambaStyle:
    case NiniFileType::MysqlStyle:
        w->assign_delimiter(u'=');
        w->comment_delimiter(u'#');
        break;
    default:
        break;
    }
    auto version = s->version;
    std::size_t index = 0;
    for (;;) {
        if (version != s->version)
            throw NiniDocumentError(NiniDocumentError::Kind::InvalidOperation,
                                    u"Collection was modified; enumeration operation may not execute.");
        if (index >= s->initial.size())
            break;
        auto comment = s->initial[index++];
        w->write_empty(comment);
    }
    for (std::int32_t i = 0; i < s->sections.count(); ++i) {
        auto section = s->sections.at(i);
        w->write_section(section->name(), section->comment());
        for (std::int32_t j = 0; j < section->item_count(); ++j) {
            auto item = section->get_item(j);
            switch (item->type()) {
            case NiniType::Key:
                w->write_key(item->name(), item->value(), item->comment());
                break;
            case NiniType::Empty:
                w->write_empty(item->comment());
                break;
            default:
                break;
            }
        }
    }
    w->close();
}
void NiniDocument::save(NiniWriterText p, const NiniDocumentFactory& f) {
    if (!f.file_writer)
        throw std::invalid_argument("Missing Nini file writer provider");
    auto writer = f.file_writer(p);
    save(writer);
    if (!writer)
        nullref();
    writer->close();
}
void NiniDocument::save(std::shared_ptr<NiniWriterStream> p, const NiniDocumentFactory& f) {
    if (!f.stream_writer)
        throw std::invalid_argument("Missing Nini stream writer provider");
    save(f.stream_writer(std::move(p)));
}
}
