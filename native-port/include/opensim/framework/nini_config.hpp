// Port of bundled Nini.Config; copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
#pragma once
#include "opensim/framework/nini_document.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <vector>
namespace opensim::framework {
class NiniConfigError : public std::runtime_error {
  public:
    enum class Kind {
        Argument,
        ArgumentNull,
        OutOfRange,
        NullReference,
        InvalidOperation,
        InvalidCast,
        Format,
        Overflow,
        IndexOutOfRange,
        Exception
    };
    NiniConfigError(Kind, std::u16string, std::optional<std::u16string> parameter = {});
    Kind kind;
    std::u16string message;
    std::optional<std::u16string> parameter;
    const char* source_type() const noexcept;
    std::uint32_t hresult() const noexcept;
};
struct NiniNumberFormat {
    std::function<std::int32_t(const NiniWriterText&)> to_int32;
    std::function<std::int64_t(const NiniWriterText&)> to_int64;
    std::function<float(const NiniWriterText&)> to_single;
    std::function<double(const NiniWriterText&)> to_double;
};
struct NiniInsensitiveTable {
    // Captures the source CaseInsensitiveHashCodeProvider/Comparer cultures at
    // Hashtable construction. Equality must implement that captured comparer.
    std::function<bool(const NiniWriterText&, const NiniWriterText&)> equal;
};
struct NiniConfigBackend {
    std::function<std::shared_ptr<NiniNumberFormat>()> capture_number_format;
    std::function<std::shared_ptr<NiniInsensitiveTable>()> insensitive_table;
    std::function<NiniWriterText(const NiniWriterText&)> lower_current;
    std::function<std::int32_t(const NiniWriterText&, const NiniWriterText&, std::int32_t)> index_of_current;
    std::function<std::shared_ptr<NiniTextReader>(const NiniWriterText&)> file_reader;
    std::function<std::shared_ptr<NiniTextReader>(std::shared_ptr<NiniWriterStream>)> stream_reader;
    NiniDocumentFactory document;
};
// Every event invocation snapshots the multicast list while retaining callable
// identity/state. Removal removes the last matching delegate sequence.
template <class Sender, class Args> class NiniConfigEvent {
  public:
    using Callback = std::function<void(Sender&, std::shared_ptr<Args>)>;
    struct Entry {
        std::uint64_t identity;
        std::shared_ptr<Callback> callback;
    };
    void add(std::uint64_t id, Callback fn) {
        if (fn)
            handlers_.push_back({id, std::make_shared<Callback>(std::move(fn))});
    }
    void add(const std::vector<Entry>& list) {
        handlers_.insert(handlers_.end(), list.begin(), list.end());
    }
    void remove(std::uint64_t id) {
        remove(std::vector<std::uint64_t>{id});
    }
    void remove(const std::vector<std::uint64_t>& ids) {
        if (ids.empty() || ids.size() > handlers_.size())
            return;
        for (std::size_t n = handlers_.size() - ids.size() + 1; n-- > 0;) {
            bool match = true;
            for (std::size_t i = 0; i < ids.size(); ++i)
                match = match && handlers_[n + i].identity == ids[i];
            if (match) {
                handlers_.erase(handlers_.begin() + n, handlers_.begin() + n + ids.size());
                return;
            }
        }
    }
    bool empty() const noexcept {
        return handlers_.empty();
    }
    std::size_t count() const noexcept {
        return handlers_.size();
    }
    void invoke(Sender& sender, std::shared_ptr<Args> args) {
        auto list = handlers_;
        for (const auto& item : list)
            (*item.callback)(sender, args);
    }
    void clear() {
        handlers_.clear();
    }

  private:
    std::vector<Entry> handlers_;
};
class NiniConfigArena;
class NiniConfig;
class NiniConfigSourceBase;
class NiniIniConfigSource;
class NiniConfigCollection;
struct NiniConfigKeyArgs {
    const NiniWriterText key, value;
};
struct NiniConfigArgs {
    const std::shared_ptr<NiniConfig> config;
};
struct NiniConfigEventArgs {};
class NiniConfigObject {
  protected:
    NiniConfigArena* arena_;
    explicit NiniConfigObject(NiniConfigArena* a) : arena_(a) {
    }

  public:
    virtual ~NiniConfigObject() = default;
    NiniConfigArena* arena() const noexcept {
        return arena_;
    }
    virtual NiniWriterText to_string();
    virtual NiniWriterText managed_type() const = 0;
    virtual bool equals(const NiniConfigObject* other) const {
        return this == other;
    }
};
struct NiniEnumReflection {
    std::function<std::vector<NiniWriterText>()> names;
    // Owns actual Enum.GetValues followed by int[] cast; byte/long enums fail here.
    std::function<std::vector<std::int32_t>()> int_values;
};
class NiniAliasText {
    struct State;
    std::shared_ptr<State> state_;

  public:
    explicit NiniAliasText(std::shared_ptr<NiniConfigBackend>);
    void add_alias(NiniWriterText key, NiniWriterText alias, std::int32_t);
    void add_alias(NiniWriterText alias, bool);
    void add_alias(NiniWriterText key, const std::shared_ptr<NiniEnumReflection>&);
    bool contains_boolean(const NiniWriterText&) const;
    bool contains_int(const NiniWriterText&, const NiniWriterText&) const;
    bool get_boolean(const NiniWriterText&) const;
    std::int32_t get_int(const NiniWriterText&, const NiniWriterText&) const;
};
class NiniConfig : public NiniConfigObject {
    friend class NiniConfigArena;

  protected:
    struct State;
    std::shared_ptr<State> state_;
    NiniConfig(NiniConfigArena*, NiniWriterText, NiniConfigSourceBase*, bool ini);
    NiniWriterText insensitive_key(NiniWriterText) const;

  public:
    NiniWriterText managed_type() const override;
    NiniWriterText name() const;
    void name(NiniWriterText);
    std::shared_ptr<NiniConfigSourceBase> config_source() const;
    std::shared_ptr<NiniAliasText> alias() const;
    NiniConfigEvent<NiniConfig, NiniConfigKeyArgs> key_set, key_removed;
    bool contains(const NiniWriterText& key);
    virtual NiniWriterText get(NiniWriterText key);
    NiniWriterText get(const NiniWriterText&, NiniWriterText fallback);
    NiniWriterText get_expanded(const NiniWriterText&);
    NiniWriterText get_string(const NiniWriterText& key) {
        return get(key);
    }
    NiniWriterText get_string(const NiniWriterText& key, NiniWriterText fallback) {
        return get(key, std::move(fallback));
    }
    std::int32_t get_int(const NiniWriterText&);
    std::int32_t get_int(const NiniWriterText&, bool from_alias);
    std::int32_t get_int(const NiniWriterText&, std::int32_t fallback);
    std::int32_t get_int(const NiniWriterText&, std::int32_t fallback, bool from_alias);
    std::int64_t get_long(const NiniWriterText&);
    std::int64_t get_long(const NiniWriterText&, std::int64_t fallback);
    bool get_boolean(const NiniWriterText&);
    bool get_boolean(const NiniWriterText&, bool fallback);
    float get_float(const NiniWriterText&);
    float get_float(const NiniWriterText&, float fallback);
    double get_double(const NiniWriterText&);
    double get_double(const NiniWriterText&, double fallback);
    virtual std::vector<NiniWriterText> get_keys() const;
    virtual std::vector<NiniWriterText> get_values() const;
    void add(NiniWriterText key, NiniWriterText value);
    virtual void set(NiniWriterText key, std::shared_ptr<NiniConfigObject> value);
    void set_text(NiniWriterText key, NiniWriterText value);
    virtual void remove(NiniWriterText key);
};
using NiniConfigBase = NiniConfig;
class NiniIniConfig : public NiniConfig {
    friend class NiniConfigArena;
    NiniIniConfig(NiniConfigArena* a, NiniWriterText name, NiniConfigSourceBase* source)
        : NiniConfig(a, std::move(name), source, true) {
    }
};
class NiniConfigArray {
  public:
    virtual ~NiniConfigArray() = default;
    // CLR Array.Copy owns rank/lower-bound/element-type checks, including partial copies.
    virtual void copy_from(const std::vector<std::shared_ptr<NiniConfigObject>>&, std::int32_t index) = 0;
};
class NiniConfigCollection {
    friend class NiniConfigArena;
    friend class NiniConfigSourceBase;
    struct State;
    std::shared_ptr<State> state_;
    explicit NiniConfigCollection(NiniConfigSourceBase*);

  public:
    class Enumerator {
        struct Cursor;
        std::shared_ptr<Cursor> cursor_;
        friend class NiniConfigCollection;
        explicit Enumerator(std::shared_ptr<Cursor>);

      public:
        bool move_next();
        void reset();
        std::shared_ptr<NiniConfigObject> current() const;
    };
    std::int32_t count() const;
    bool is_fixed_size() const {
        return false;
    }
    bool is_read_only() const {
        return false;
    }
    bool is_synchronized() const {
        return false;
    }
    std::shared_ptr<const void> sync_root() const;
    std::shared_ptr<NiniConfig> at(std::int32_t) const;
    std::shared_ptr<NiniConfig> at(const NiniWriterText&) const;
    std::shared_ptr<NiniConfigObject> list_at(std::int32_t) const;
    void list_set(std::int32_t, std::shared_ptr<NiniConfigObject>) {
    }
    NiniConfigEvent<NiniConfigCollection, NiniConfigArgs> config_added, config_removed;
    void add(std::shared_ptr<NiniConfig>);
    std::int32_t list_add(std::shared_ptr<NiniConfigObject>);
    std::shared_ptr<NiniConfig> add(NiniWriterText name);
    void remove(std::shared_ptr<NiniConfig>);
    void remove_object(std::shared_ptr<NiniConfigObject>);
    void remove_at(std::int32_t);
    void clear();
    bool contains(const std::shared_ptr<NiniConfigObject>&) const;
    std::int32_t index_of(const std::shared_ptr<NiniConfigObject>&) const;
    void insert(std::int32_t, std::shared_ptr<NiniConfigObject>);
    void copy_to(const std::shared_ptr<NiniConfigArray>&, std::int32_t) const;
    Enumerator get_enumerator() const;
};
class NiniConfigSourceBase : public NiniConfigObject {
    friend class NiniConfigArena;

  protected:
    struct State;
    std::shared_ptr<State> state_;
    NiniConfigSourceBase(NiniConfigArena*, NiniWriterText managed_type);
    void on_saved();
    void on_reloaded();

  public:
    NiniWriterText managed_type() const override;
    std::shared_ptr<NiniConfigCollection> configs() const;
    virtual bool auto_save() const;
    virtual void auto_save(bool);
    std::shared_ptr<NiniAliasText> alias() const;
    NiniConfigEvent<NiniConfigSourceBase, NiniConfigEventArgs> saved, reloaded;
    virtual void save();
    virtual void reload();
    virtual std::shared_ptr<NiniConfig> add_config(NiniWriterText);
    void merge(std::shared_ptr<NiniConfigSourceBase>);
    class Expansion {
        struct State;
        std::shared_ptr<State> state_;
        friend class NiniConfigSourceBase;
        explicit Expansion(std::shared_ptr<State>);

      public:
        bool step();
        NiniWriterText result() const;
    };
    Expansion expansion(std::shared_ptr<NiniConfig>, NiniWriterText, bool set_value = false);
    NiniWriterText get_expanded(std::shared_ptr<NiniConfig>, NiniWriterText);
    void expand_key_values();
    void replace_key_values() {
        expand_key_values();
    }
};
class NiniIniConfigSource : public NiniConfigSourceBase {
    friend class NiniConfigArena;
    struct IniState;
    std::shared_ptr<IniState> ini_;
    explicit NiniIniConfigSource(NiniConfigArena*);
    void load_document();
    void merge_configs_into_document();
    void merge_document_into_configs();

  public:
    bool case_sensitive() const;
    void case_sensitive(bool);
    NiniWriterText save_path() const;
    void load(NiniWriterText path);
    void load(std::shared_ptr<NiniTextReader>);
    void load(std::shared_ptr<NiniDocument>);
    void load(std::shared_ptr<NiniWriterStream>);
    void save() override;
    void save(NiniWriterText path);
    void save(std::shared_ptr<NiniTextWriter>);
    void save(std::shared_ptr<NiniWriterStream>);
    void reload() override;
    NiniWriterText to_string() override;
    std::shared_ptr<NiniDocument> document() const;
};
// Intrinsic config/source/merge/event-argument references share an arena owner;
// they are never weakened to break cycles. Nodes retire until arena release;
// arbitrary CLR GC timing and opaque provider capture cycles remain host-owned.
class NiniConfigArena : public std::enable_shared_from_this<NiniConfigArena> {
    struct State;
    std::unique_ptr<State> state_;

  public:
    explicit NiniConfigArena(std::shared_ptr<NiniConfigBackend>);
    ~NiniConfigArena();
    std::shared_ptr<NiniConfigBackend> backend() const;
    template <class T> std::shared_ptr<T> handle(T* value) {
        return value ? std::shared_ptr<T>(shared_from_this(), value) : nullptr;
    }
    void retain(const std::shared_ptr<NiniConfigObject>&);
    // Own derived consumed config/source nodes before exposing arena alias handles.
    std::shared_ptr<NiniConfigObject> adopt(std::unique_ptr<NiniConfigObject>);
    std::shared_ptr<NiniConfigSourceBase> create_source(NiniWriterText managed_type = {});
    std::shared_ptr<NiniIniConfigSource> create_ini_source();
    std::shared_ptr<NiniIniConfigSource> create_ini_source(NiniWriterText);
    std::shared_ptr<NiniIniConfigSource> create_ini_source(std::shared_ptr<NiniTextReader>);
    std::shared_ptr<NiniIniConfigSource> create_ini_source(std::shared_ptr<NiniDocument>);
    std::shared_ptr<NiniIniConfigSource> create_ini_source(std::shared_ptr<NiniWriterStream>);
    std::shared_ptr<NiniConfig> create_config(NiniWriterText, std::shared_ptr<NiniConfigSourceBase>,
                                              bool ini = false);
    std::shared_ptr<NiniConfigObject> text_object(NiniWriterText);
    std::shared_ptr<NiniConfigObject> object(NiniWriterText type, std::function<NiniWriterText()> to_string,
                                             std::function<bool(const NiniConfigObject*)> equals = {});
};
}
