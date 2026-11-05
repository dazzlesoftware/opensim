// Port of Nini.Ini document classes; copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
#pragma once
#include "opensim/framework/nini_reader.hpp"
#include "opensim/framework/nini_writer.hpp"
#include <vector>
namespace opensim::framework {
enum class NiniFileType { Standard, PythonStyle, SambaStyle, MysqlStyle, WindowsStyle };
class NiniDocumentError : public std::runtime_error {
  public:
    enum class Kind {
        Argument,
        ArgumentNull,
        OutOfRange,
        InvalidOperation,
        NullReference,
        IndexOutOfRange,
        InvalidCast
    };
    NiniDocumentError(Kind, std::u16string, std::optional<std::u16string> parameter = {});
    Kind kind;
    std::u16string message;
    std::optional<std::u16string> parameter;
    const char* source_type() const noexcept;
    std::int32_t hresult() const noexcept;
};
class NiniItem {
    NiniWriterText name_, value_, comment_;
    NiniType type_;

  public:
    NiniItem(NiniWriterText name, NiniWriterText value, NiniType type, NiniWriterText comment)
        : name_(std::move(name)), value_(std::move(value)), comment_(std::move(comment)), type_(type) {
    }
    NiniWriterText name() const {
        return name_;
    }
    NiniWriterText value() const {
        return value_;
    }
    void value(NiniWriterText v) {
        value_ = std::move(v);
    }
    NiniWriterText comment() const {
        return comment_;
    }
    void comment(NiniWriterText v) {
        comment_ = std::move(v);
    }
    NiniType type() const {
        return type_;
    }
    void type(NiniType v) {
        type_ = v;
    }
};
class NiniSection {
    struct State;
    std::shared_ptr<State> state_;

  public:
    explicit NiniSection(NiniWriterText name, NiniWriterText comment = {});
    NiniWriterText name() const;
    NiniWriterText comment() const;
    std::int32_t item_count() const;
    NiniWriterText get_value(const NiniWriterText&) const;
    std::shared_ptr<NiniItem> get_item(std::int32_t) const;
    std::vector<NiniWriterText> get_keys() const;
    bool contains(const NiniWriterText&) const;
    void set(NiniWriterText key, NiniWriterText value, NiniWriterText comment = {});
    void set(NiniWriterText comment);
    void set();
    void remove(const NiniWriterText&);
};
struct NiniSectionEntry {
    NiniWriterText key;
    std::shared_ptr<NiniSection> value;
};
// CLR Array validation surface; set receives boxed DictionaryEntry, never an
// IniSection value. Concrete arrays retain values and expose managed rank/bounds.
class NiniSectionArray {
  public:
    virtual ~NiniSectionArray() = default;
    virtual std::int32_t rank() const = 0;
    virtual std::int32_t lower_bound() const = 0;
    virtual std::int32_t length() const = 0;
    virtual void set(std::int32_t, const NiniSectionEntry&) = 0;
};
class NiniEntryArray final : public NiniSectionArray {
    std::vector<std::optional<NiniSectionEntry>> values_;

  public:
    explicit NiniEntryArray(std::int32_t length);
    std::int32_t rank() const override {
        return 1;
    }
    std::int32_t lower_bound() const override {
        return 0;
    }
    std::int32_t length() const override {
        return static_cast<std::int32_t>(values_.size());
    }
    void set(std::int32_t, const NiniSectionEntry&) override;
    const auto& values() const {
        return values_;
    }
};
class NiniSectionCollection {
    struct State;
    std::shared_ptr<State> state_;

  public:
    class Enumerator {
        std::shared_ptr<State> state_;
        std::int32_t index_ = -1;
        friend class NiniSectionCollection;
        explicit Enumerator(std::shared_ptr<State> state) : state_(std::move(state)) {
        }

      public:
        bool move_next();
        void reset();
        NiniSectionEntry current() const;
        NiniSectionEntry entry() const {
            return current();
        }
        NiniWriterText key() const {
            return current().key;
        }
        std::shared_ptr<NiniSection> value() const {
            return current().value;
        }
    };
    NiniSectionCollection();
    std::shared_ptr<NiniSection> at(std::int32_t) const;
    std::shared_ptr<NiniSection> at(const NiniWriterText&) const;
    std::int32_t count() const;
    std::shared_ptr<const void> sync_root() const;
    bool is_synchronized() const {
        return false;
    }
    void add(std::shared_ptr<NiniSection>);
    void remove(const NiniWriterText&);
    void copy_to(const std::shared_ptr<NiniSectionArray>&, std::int32_t) const;
    void copy_to(const std::shared_ptr<std::vector<std::shared_ptr<NiniSection>>>&, std::int32_t) const;
    Enumerator get_enumerator() const;
};
struct NiniDocumentFactory {
    std::function<std::shared_ptr<NiniTextReader>(const NiniWriterText&)> file_reader;
    std::function<std::shared_ptr<NiniTextReader>(std::shared_ptr<NiniWriterStream>)> stream_reader;
    std::function<std::shared_ptr<NiniTextWriter>(const NiniWriterText&)> file_writer;
    std::function<std::shared_ptr<NiniTextWriter>(std::shared_ptr<NiniWriterStream>)> stream_writer;
};
class NiniDocument {
    struct State;
    std::shared_ptr<State> state_;

  public:
    NiniDocument();
    explicit NiniDocument(std::shared_ptr<NiniTextReader>, NiniFileType = NiniFileType::Standard);
    explicit NiniDocument(std::shared_ptr<NiniReader>);
    NiniDocument(NiniWriterText, const NiniDocumentFactory&, NiniFileType = NiniFileType::Standard);
    NiniDocument(std::shared_ptr<NiniWriterStream>, const NiniDocumentFactory&,
                 NiniFileType = NiniFileType::Standard);
    NiniFileType file_type() const;
    void file_type(NiniFileType);
    NiniSectionCollection sections() const;
    void load(std::shared_ptr<NiniTextReader>);
    void load(std::shared_ptr<NiniReader>);
    void load(NiniWriterText, const NiniDocumentFactory&);
    void load(std::shared_ptr<NiniWriterStream>, const NiniDocumentFactory&);
    // Internally constructed readers and Save-created writers retire here to avoid synchronous finalization on failure.
    // Hosts can dispatch finalizers explicitly; arbitrary CLR GC timing is separate.
    std::size_t retired_writer_count() const;
    void finalize_retired_writers();
    std::size_t retired_reader_count() const;
    void finalize_retired_readers();
    void save(std::shared_ptr<NiniTextWriter>);
    void save(NiniWriterText, const NiniDocumentFactory&);
    void save(std::shared_ptr<NiniWriterStream>, const NiniDocumentFactory&);
};
}
