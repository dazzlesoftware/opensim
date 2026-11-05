// Port of bundled Nini.Config; copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
#include "opensim/framework/nini_config.hpp"
#include "opensim/framework/text.hpp"
#include <algorithm>
#include <bit>
#include <limits>
namespace opensim::framework {
namespace {
using T = NiniWriterText;
T txt(std::u16string v) {
    return v.empty() ? runtime_empty_text() : std::make_shared<const std::u16string>(std::move(v));
}
std::u16string val(const T& p) {
    return p ? *p : u"";
}
bool same(const T& a, const T& b) {
    return (!a && !b) || (a && b && *a == *b);
}
[[noreturn]] void fail(NiniConfigError::Kind k, std::u16string s, std::optional<std::u16string> p = {}) {
    throw NiniConfigError(k, std::move(s), std::move(p));
}
[[noreturn]] void nullref() {
    fail(NiniConfigError::Kind::NullReference, u"Object reference not set to an instance of an object.");
}
void key(const T& k) {
    if (!k)
        fail(NiniConfigError::Kind::ArgumentNull, u"Value cannot be null. (Parameter 'key')", u"key");
}
void index_error() {
    fail(
        NiniConfigError::Kind::OutOfRange,
        u"Index was out of range. Must be non-negative and less than the size of the collection. (Parameter 'index')",
        u"index");
}
void changed() {
    fail(NiniConfigError::Kind::InvalidOperation,
         u"Collection was modified; enumeration operation may not execute.");
}
std::int32_t increment(std::int32_t v) {
    return std::bit_cast<std::int32_t>(std::uint32_t(v) + 1);
}
NiniConfig* config_cast(NiniConfigObject* object) {
    if (!object)
        return nullptr;
    if (auto p = dynamic_cast<NiniConfig*>(object))
        return p;
    fail(NiniConfigError::Kind::InvalidCast, u"Unable to cast object of type '" +
                                                 val(object->managed_type()) +
                                                 u"' to type 'Nini.Config.IConfig'.");
}
template <class X> std::shared_ptr<X> handle(X* p) {
    return p ? p->arena()->handle(p) : nullptr;
}
}
NiniConfigError::NiniConfigError(Kind k, std::u16string s, std::optional<std::u16string> p)
    : std::runtime_error(utf8(s)), kind(k), message(std::move(s)), parameter(std::move(p)) {
}
const char* NiniConfigError::source_type() const noexcept {
    switch (kind) {
    case Kind::Argument:
        return "System.ArgumentException";
    case Kind::ArgumentNull:
        return "System.ArgumentNullException";
    case Kind::OutOfRange:
        return "System.ArgumentOutOfRangeException";
    case Kind::NullReference:
        return "System.NullReferenceException";
    case Kind::InvalidOperation:
        return "System.InvalidOperationException";
    case Kind::InvalidCast:
        return "System.InvalidCastException";
    case Kind::Format:
        return "System.FormatException";
    case Kind::Overflow:
        return "System.OverflowException";
    case Kind::IndexOutOfRange:
        return "System.IndexOutOfRangeException";
    case Kind::Exception:
        return "System.Exception";
    }
    return "System.Exception";
}
std::uint32_t NiniConfigError::hresult() const noexcept {
    switch (kind) {
    case Kind::Argument:
        return 0x80070057u;
    case Kind::ArgumentNull:
    case Kind::NullReference:
        return 0x80004003u;
    case Kind::OutOfRange:
        return 0x80131502u;
    case Kind::InvalidOperation:
        return 0x80131509u;
    case Kind::InvalidCast:
        return 0x80004002u;
    case Kind::Format:
        return 0x80131537u;
    case Kind::Overflow:
        return 0x80131516u;
    case Kind::IndexOutOfRange:
        return 0x80131508u;
    case Kind::Exception:
        return 0x80131500u;
    }
    return 0x80131500u;
}
NiniWriterText NiniConfigObject::to_string() {
    return managed_type();
}
struct NiniConfigArena::State {
    std::shared_ptr<NiniConfigBackend> backend;
    std::vector<std::unique_ptr<NiniConfigObject>> objects;
    std::vector<std::shared_ptr<NiniConfigObject>> foreign;
};
NiniConfigArena::NiniConfigArena(std::shared_ptr<NiniConfigBackend> b) : state_(new State) {
    if (!b || !b->capture_number_format || !b->insensitive_table || !b->lower_current || !b->index_of_current)
        throw std::invalid_argument(
            "Nini configuration requires culture capture, alias comparer, casing and substring-search providers");
    state_->backend = std::move(b);
}
NiniConfigArena::~NiniConfigArena() = default;
std::shared_ptr<NiniConfigBackend> NiniConfigArena::backend() const {
    return state_->backend;
}
void NiniConfigArena::retain(const std::shared_ptr<NiniConfigObject>& object) {
    if (object && object->arena() != this)
        state_->foreign.push_back(object);
}
std::shared_ptr<NiniConfigObject> NiniConfigArena::adopt(std::unique_ptr<NiniConfigObject> object) {
    if (!object || object->arena() != this)
        throw std::invalid_argument("Nini arena adoption requires a nonnull node owned by this arena");
    auto pointer = object.get();
    state_->objects.emplace_back(std::move(object));
    return handle(pointer);
}
namespace {
class ConfigValue final : public NiniConfigObject {
    T type_;
    std::function<T()> string_;
    std::function<bool(const NiniConfigObject*)> equal_;

  public:
    ConfigValue(NiniConfigArena* a, T t, std::function<T()> f, std::function<bool(const NiniConfigObject*)> e)
        : NiniConfigObject(a), type_(std::move(t)), string_(std::move(f)), equal_(std::move(e)) {
    }
    T managed_type() const override {
        return type_;
    }
    T to_string() override {
        return string_ ? string_() : type_;
    }
    bool equals(const NiniConfigObject* p) const override {
        return equal_ ? equal_(p) : this == p;
    }
};
}
std::shared_ptr<NiniConfigObject>
NiniConfigArena::object(T type, std::function<T()> string,
                        std::function<bool(const NiniConfigObject*)> equal) {
    auto p = new ConfigValue(this, std::move(type), std::move(string), std::move(equal));
    state_->objects.emplace_back(p);
    return handle(p);
}
std::shared_ptr<NiniConfigObject> NiniConfigArena::text_object(T value) {
    if (!value)
        return {};
    return object(txt(u"System.String"), [value] { return value; });
}
struct NiniAliasText::State {
    struct IntTable {
        T key;
        std::shared_ptr<NiniInsensitiveTable> compare;
        std::vector<std::pair<T, std::int32_t>> values;
    };
    std::shared_ptr<NiniConfigBackend> backend;
    std::shared_ptr<NiniInsensitiveTable> ints, booleans;
    std::vector<IntTable> integer;
    std::vector<std::pair<T, bool>> boolean;
};
namespace {
std::shared_ptr<NiniInsensitiveTable> capture_table(const std::shared_ptr<NiniConfigBackend>& b) {
    auto t = b->insensitive_table();
    if (!t || !t->equal)
        throw std::invalid_argument("Nini alias table requires captured CurrentCulture comparer");
    return t;
}
}
NiniAliasText::NiniAliasText(std::shared_ptr<NiniConfigBackend> b) : state_(new State) {
    state_->backend = std::move(b);
    state_->ints = capture_table(state_->backend);
    state_->booleans = capture_table(state_->backend);
    add_alias(txt(u"true"), true);
    add_alias(txt(u"false"), false);
}
void NiniAliasText::add_alias(T k, T alias, std::int32_t v) {
    key(k);
    for (auto& t : state_->integer)
        if (state_->ints->equal(t.key, k)) {
            key(alias);
            for (auto& item : t.values)
                if (t.compare->equal(item.first, alias)) {
                    item.second = v;
                    return;
                }
            t.values.emplace_back(std::move(alias), v);
            return;
        }
    State::IntTable t{k, capture_table(state_->backend), {}};
    key(alias);
    t.values.emplace_back(std::move(alias), v);
    state_->integer.push_back(std::move(t));
}
void NiniAliasText::add_alias(T alias, bool value) {
    key(alias);
    for (auto& item : state_->boolean)
        if (state_->booleans->equal(item.first, alias)) {
            item.second = value;
            return;
        }
    state_->boolean.emplace_back(std::move(alias), value);
}
void NiniAliasText::add_alias(T k, const std::shared_ptr<NiniEnumReflection>& e) {
    if (!e)
        nullref();
    if (!e->names || !e->int_values)
        throw std::invalid_argument(
            "Nini enum aliases require actual enum names and int-array reflection providers");
    auto names = e->names();
    auto values = e->int_values();
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i >= values.size())
            fail(NiniConfigError::Kind::IndexOutOfRange, u"Index was outside the bounds of the array.");
        add_alias(k, names[i], values[i]);
    }
}
bool NiniAliasText::contains_boolean(const T& k) const {
    key(k);
    for (auto& v : state_->boolean)
        if (state_->booleans->equal(v.first, k))
            return true;
    return false;
}
bool NiniAliasText::contains_int(const T& k, const T& a) const {
    key(k);
    for (auto& t : state_->integer)
        if (state_->ints->equal(t.key, k)) {
            key(a);
            for (auto& v : t.values)
                if (t.compare->equal(v.first, a))
                    return true;
            return false;
        }
    return false;
}
bool NiniAliasText::get_boolean(const T& k) const {
    if (!contains_boolean(k))
        fail(NiniConfigError::Kind::Argument, u"Alias does not exist for text");
    for (auto& v : state_->boolean)
        if (state_->booleans->equal(v.first, k))
            return v.second;
    return false;
}
std::int32_t NiniAliasText::get_int(const T& k, const T& a) const {
    key(k);
    for (auto& t : state_->integer)
        if (state_->ints->equal(t.key, k)) {
            key(a);
            for (auto& v : t.values)
                if (t.compare->equal(v.first, a))
                    return v.second;
            fail(NiniConfigError::Kind::Argument, u"Config value does not match a supplied alias");
        }
    fail(NiniConfigError::Kind::Argument, u"Alias does not exist for key");
}
struct NiniConfigCollection::State {
    NiniConfigSourceBase* owner;
    std::shared_ptr<std::vector<NiniConfigObject*>> items =
        std::make_shared<std::vector<NiniConfigObject*>>();
    std::int32_t size = 0, version = 0;
    void grow() {
        if (size == static_cast<std::int32_t>(items->size())) {
            auto next = std::make_shared<std::vector<NiniConfigObject*>>(size ? std::size_t(size) * 2 : 4);
            std::copy_n(items->begin(), size, next->begin());
            items = std::move(next);
        }
    }
    void append(NiniConfigObject* p) {
        grow();
        (*items)[size++] = p;
        version = increment(version);
    }
    void erase(std::int32_t i) {
        if (i < 0 || i >= size)
            index_error();
        --size;
        for (auto n = i; n < size; ++n)
            (*items)[n] = (*items)[n + 1];
        (*items)[size] = nullptr;
        version = increment(version);
    }
};
NiniConfigCollection::NiniConfigCollection(NiniConfigSourceBase* o) : state_(new State) {
    state_->owner = o;
}
std::int32_t NiniConfigCollection::count() const {
    return state_->size;
}
std::shared_ptr<const void> NiniConfigCollection::sync_root() const {
    return std::shared_ptr<const void>(state_->owner->arena()->shared_from_this(), this);
}
std::shared_ptr<NiniConfigObject> NiniConfigCollection::list_at(std::int32_t i) const {
    if (i < 0 || i >= count())
        index_error();
    return handle((*state_->items)[i]);
}
std::shared_ptr<NiniConfig> NiniConfigCollection::at(std::int32_t i) const {
    return handle(config_cast(list_at(i).get()));
}
struct NiniConfigCollection::Enumerator::Cursor {
    std::shared_ptr<State> state;
    std::shared_ptr<NiniConfigArena> owner;
    std::int32_t version, index = -1;
    NiniConfigObject* current = nullptr;
    bool valid = false;
};
NiniConfigCollection::Enumerator::Enumerator(std::shared_ptr<Cursor> c) : cursor_(std::move(c)) {
}
NiniConfigCollection::Enumerator NiniConfigCollection::get_enumerator() const {
    auto c = std::make_shared<Enumerator::Cursor>();
    c->state = state_;
    c->owner = state_->owner->arena()->shared_from_this();
    c->version = state_->version;
    return Enumerator(c);
}
bool NiniConfigCollection::Enumerator::move_next() {
    auto& c = *cursor_;
    if (c.version != c.state->version)
        changed();
    if (c.index < c.state->size - 1) {
        c.current = (*c.state->items)[++c.index];
        c.valid = true;
        return true;
    }
    c.index = c.state->size;
    c.current = nullptr;
    c.valid = false;
    return false;
}
void NiniConfigCollection::Enumerator::reset() {
    auto& c = *cursor_;
    if (c.version != c.state->version)
        changed();
    c.index = -1;
    c.current = nullptr;
    c.valid = false;
}
std::shared_ptr<NiniConfigObject> NiniConfigCollection::Enumerator::current() const {
    if (!cursor_->valid)
        fail(NiniConfigError::Kind::InvalidOperation, cursor_->index < 0
                                                          ? u"Enumeration has not started. Call MoveNext."
                                                          : u"Enumeration already finished.");
    return handle(cursor_->current);
}
std::shared_ptr<NiniConfig> NiniConfigCollection::at(const T& name) const {
    auto e = get_enumerator();
    while (e.move_next()) {
        auto config = config_cast(e.current().get());
        if (!config)
            nullref();
        if (same(config->name(), name))
            return handle(config);
    }
    return {};
}
std::int32_t NiniConfigCollection::index_of(const std::shared_ptr<NiniConfigObject>& obj) const {
    auto items = state_->items;
    auto size = state_->size;
    for (std::int32_t i = 0; i < size; ++i) {
        auto p = (*items)[i];
        if (!obj) {
            if (!p)
                return i;
        } else if (p && p->equals(obj.get()))
            return i;
    }
    return -1;
}
bool NiniConfigCollection::contains(const std::shared_ptr<NiniConfigObject>& obj) const {
    return index_of(obj) >= 0;
}
void NiniConfigCollection::add(std::shared_ptr<NiniConfig> config) {
    if (contains(config))
        fail(NiniConfigError::Kind::Argument, u"IConfig already exists");
    if (!config)
        nullref();
    auto existing = at(config->name());
    if (existing) {
        auto keys = config->get_keys();
        for (auto& k : keys)
            existing->set_text(k, config->get(k));
    } else {
        state_->owner->arena()->retain(config);
        state_->append(config.get());
        config_added.invoke(*this, std::make_shared<NiniConfigArgs>(NiniConfigArgs{config}));
    }
}
std::int32_t NiniConfigCollection::list_add(std::shared_ptr<NiniConfigObject> obj) {
    auto config = dynamic_cast<NiniConfig*>(obj.get());
    if (!config)
        fail(NiniConfigError::Kind::Exception, u"Must be an IConfig");
    add(handle(config));
    return index_of(obj);
}
std::shared_ptr<NiniConfig> NiniConfigCollection::add(T name) {
    if (at(name))
        fail(NiniConfigError::Kind::Argument, u"An IConfig of that name already exists");
    auto config = state_->owner->arena()->create_config(std::move(name), handle(state_->owner));
    state_->append(config.get());
    config_added.invoke(*this, std::make_shared<NiniConfigArgs>(NiniConfigArgs{config}));
    return config;
}
void NiniConfigCollection::remove(std::shared_ptr<NiniConfig> obj) {
    auto at = index_of(obj);
    if (at >= 0)
        state_->erase(at);
    config_removed.invoke(*this, std::make_shared<NiniConfigArgs>(NiniConfigArgs{obj}));
}
void NiniConfigCollection::remove_object(std::shared_ptr<NiniConfigObject> obj) {
    auto at = index_of(obj);
    if (at >= 0)
        state_->erase(at);
    auto p = config_cast(obj.get());
    config_removed.invoke(*this, std::make_shared<NiniConfigArgs>(NiniConfigArgs{handle(p)}));
}
void NiniConfigCollection::remove_at(std::int32_t i) {
    auto obj = at(i);
    state_->erase(i);
    config_removed.invoke(*this, std::make_shared<NiniConfigArgs>(NiniConfigArgs{obj}));
}
void NiniConfigCollection::clear() {
    std::fill_n(state_->items->begin(), state_->size, nullptr);
    state_->size = 0;
    state_->version = increment(state_->version);
}
void NiniConfigCollection::insert(std::int32_t i, std::shared_ptr<NiniConfigObject> obj) {
    if (i < 0 || i > count())
        fail(
            NiniConfigError::Kind::OutOfRange,
            u"Insertion index was out of range. Must be non-negative and less than or equal to size. (Parameter 'index')",
            u"index");
    state_->owner->arena()->retain(obj);
    state_->grow();
    for (auto n = count(); n > i; --n)
        (*state_->items)[n] = (*state_->items)[n - 1];
    (*state_->items)[i] = obj.get();
    ++state_->size;
    state_->version = increment(state_->version);
}
void NiniConfigCollection::copy_to(const std::shared_ptr<NiniConfigArray>& array, std::int32_t i) const {
    if (!array)
        fail(NiniConfigError::Kind::ArgumentNull, u"Value cannot be null. (Parameter 'destinationArray')",
             u"destinationArray");
    std::vector<std::shared_ptr<NiniConfigObject>> values;
    for (std::int32_t n = 0; n < count(); ++n)
        values.push_back(handle((*state_->items)[n]));
    array->copy_from(values, i);
}
struct NiniConfig::State {
    T name;
    NiniConfigSourceBase* source = nullptr;
    NiniIniConfigSource* ini = nullptr;
    bool ini_type = false;
    std::shared_ptr<NiniNumberFormat> format;
    std::unique_ptr<NiniAliasText> alias;
    std::vector<std::pair<T, T>> ordered;
    std::vector<std::pair<T, T>> current;
};
NiniConfig::NiniConfig(NiniConfigArena* a, T name, NiniConfigSourceBase* source, bool ini)
    : NiniConfigObject(a), state_(new State) {
    state_->format = a->backend()->capture_number_format();
    if (!state_->format || !state_->format->to_int32 || !state_->format->to_int64 ||
        !state_->format->to_single || !state_->format->to_double)
        throw std::invalid_argument("Nini config requires captured NumberFormatInfo conversion providers");
    state_->name = std::move(name);
    state_->source = source;
    state_->alias = std::make_unique<NiniAliasText>(a->backend());
    state_->ini_type = ini;
    if (ini) {
        state_->ini = dynamic_cast<NiniIniConfigSource*>(source);
        if (source && !state_->ini)
            fail(NiniConfigError::Kind::InvalidCast, u"Unable to cast object of type '" +
                                                         val(source->managed_type()) +
                                                         u"' to type 'Nini.Config.IniConfigSource'.");
    }
}
T NiniConfig::managed_type() const {
    return txt(state_->ini_type ? u"Nini.Config.IniConfig" : u"Nini.Config.ConfigBase");
}
T NiniConfig::name() const {
    return state_->name;
}
void NiniConfig::name(T value) {
    if (!same(state_->name, value)) {
        auto source = config_source();
        if (!source)
            nullref();
        source->configs()->remove(handle(this));
        state_->name = std::move(value);
        source = config_source();
        source->configs()->add(handle(this));
    }
}
std::shared_ptr<NiniConfigSourceBase> NiniConfig::config_source() const {
    return handle(state_->source);
}
std::shared_ptr<NiniAliasText> NiniConfig::alias() const {
    return arena_->handle(state_->alias.get());
}
T NiniConfig::insensitive_key(T k) const {
    if (!k)
        nullref();
    auto target = arena_->backend()->lower_current(k);
    auto keys = get_keys();
    for (auto item : keys) {
        if (!item)
            nullref();
        if (same(arena_->backend()->lower_current(item), target))
            return item;
    }
    return k;
}
T NiniConfig::get(T k) {
    if (state_->ini_type) {
        if (!state_->ini)
            nullref();
        if (!state_->ini->case_sensitive())
            k = insensitive_key(k);
    }
    key(k);
    for (const auto& item : state_->current)
        if (same(item.first, k)) {
            if (!item.second)
                nullref();
            return item.second;
        }
    return {};
}
bool NiniConfig::contains(const T& k) {
    return bool(get(k));
}
T NiniConfig::get(const T& k, T fallback) {
    auto value = get(k);
    return value ? value : fallback;
}
T NiniConfig::get_expanded(const T& k) {
    auto source = config_source();
    if (!source)
        nullref();
    return source->get_expanded(handle(this), k);
}
std::vector<T> NiniConfig::get_keys() const {
    std::vector<T> out;
    for (const auto& item : state_->ordered)
        out.push_back(item.first);
    return out;
}
std::vector<T> NiniConfig::get_values() const {
    std::vector<T> out;
    for (const auto& item : state_->ordered)
        out.push_back(item.second);
    return out;
}
void NiniConfig::add(T k, T value) {
    key(k);
    for (const auto& item : state_->current)
        if (same(item.first, k))
            fail(NiniConfigError::Kind::Argument, u"Item has already been added. Key in dictionary: '" +
                                                      val(k) + u"'  Key being added: '" + val(k) + u"'");
    state_->current.emplace_back(k, value);
    state_->ordered.emplace_back(std::move(k), std::move(value));
}
void NiniConfig::set_text(T k, T value) {
    set(std::move(k), arena_->text_object(std::move(value)));
}
void NiniConfig::set(T k, std::shared_ptr<NiniConfigObject> value) {
    if (state_->ini_type) {
        if (!state_->ini)
            nullref();
        if (!state_->ini->case_sensitive())
            k = insensitive_key(k);
    }
    if (!value)
        fail(NiniConfigError::Kind::ArgumentNull, u"Value cannot be null. (Parameter 'Value cannot be null')",
             u"Value cannot be null");
    if (!get(k))
        add(k, value->to_string());
    else {
        auto text = value->to_string();
        bool found = false;
        for (auto& item : state_->current)
            if (same(item.first, k)) {
                item.second = text;
                found = true;
                break;
            }
        if (!found)
            add(k, text); /* OrderedList setter deliberately leaves ordered Values stale. */
    }
    auto source = config_source();
    if (!source)
        nullref();
    if (source->auto_save())
        source->save();
    auto args = std::make_shared<NiniConfigKeyArgs>(NiniConfigKeyArgs{k, value->to_string()});
    key_set.invoke(*this, std::move(args));
}
void NiniConfig::remove(T k) {
    if (state_->ini_type) {
        if (!state_->ini)
            nullref();
        if (!state_->ini->case_sensitive())
            k = insensitive_key(k);
    }
    if (!k)
        fail(NiniConfigError::Kind::ArgumentNull, u"Value cannot be null. (Parameter 'Key cannot be null')",
             u"Key cannot be null");
    if (get(k)) {
        T value;
        if (!key_set.empty())
            value = get(k);
        auto p = std::find_if(state_->current.begin(), state_->current.end(),
                              [&](auto& v) { return same(v.first, k); });
        if (p != state_->current.end())
            state_->current.erase(p);
        auto ordered = std::find_if(state_->ordered.begin(), state_->ordered.end(),
                                    [&](auto& v) { return same(v.first, k); });
        if (ordered == state_->ordered.end())
            index_error();
        state_->ordered.erase(ordered);
        key_removed.invoke(*this, std::make_shared<NiniConfigKeyArgs>(NiniConfigKeyArgs{k, value}));
    }
}
namespace {
T required_value(NiniConfig& config, const T& k) {
    auto v = config.get(k);
    if (!v)
        fail(NiniConfigError::Kind::Argument, u"Value not found: " + val(k));
    return v;
}
}
std::int32_t NiniConfig::get_int(const T& k) {
    return state_->format->to_int32(required_value(*this, k));
}
std::int32_t NiniConfig::get_int(const T& k, std::int32_t fallback) {
    auto v = get(k);
    return v ? state_->format->to_int32(v) : fallback;
}
std::int32_t NiniConfig::get_int(const T& k, bool from_alias) {
    if (!from_alias)
        return get_int(k);
    auto v = required_value(*this, k);
    if (alias()->contains_int(k, v))
        return alias()->get_int(k, v);
    auto source = config_source();
    if (!source)
        nullref();
    return source->alias()->get_int(k, v);
}
std::int32_t NiniConfig::get_int(const T& k, std::int32_t fallback, bool from_alias) {
    if (!from_alias)
        return get_int(k, fallback);
    auto v = get(k);
    if (!v)
        return fallback;
    if (alias()->contains_int(k, v))
        return alias()->get_int(k, v);
    auto source = config_source();
    if (!source)
        nullref();
    return source->alias()->get_int(k, v);
}
std::int64_t NiniConfig::get_long(const T& k) {
    return state_->format->to_int64(required_value(*this, k));
}
std::int64_t NiniConfig::get_long(const T& k, std::int64_t fallback) {
    auto v = get(k);
    return v ? state_->format->to_int64(v) : fallback;
}
bool NiniConfig::get_boolean(const T& k) {
    auto v = required_value(*this, k);
    if (alias()->contains_boolean(v))
        return alias()->get_boolean(v);
    auto source = config_source();
    if (!source)
        nullref();
    if (source->alias()->contains_boolean(v))
        return source->alias()->get_boolean(v);
    fail(NiniConfigError::Kind::Argument,
         u"Alias value not found: " + val(v) + u". Add it to the Alias property.");
}
bool NiniConfig::get_boolean(const T& k, bool fallback) {
    auto v = get(k);
    if (!v)
        return fallback;
    if (alias()->contains_boolean(v))
        return alias()->get_boolean(v);
    auto source = config_source();
    if (!source)
        nullref();
    if (source->alias()->contains_boolean(v))
        return source->alias()->get_boolean(v);
    fail(NiniConfigError::Kind::Argument,
         u"Alias value not found: " + val(v) + u". Add it to the Alias property.");
}
float NiniConfig::get_float(const T& k) {
    return state_->format->to_single(required_value(*this, k));
}
float NiniConfig::get_float(const T& k, float f) {
    auto v = get(k);
    return v ? state_->format->to_single(v) : f;
}
double NiniConfig::get_double(const T& k) {
    return state_->format->to_double(required_value(*this, k));
}
double NiniConfig::get_double(const T& k, double f) {
    auto v = get(k);
    return v ? state_->format->to_double(v) : f;
}

struct NiniConfigSourceBase::State {
    T type;
    bool auto_save = false;
    std::unique_ptr<NiniConfigCollection> configs;
    std::unique_ptr<NiniAliasText> alias;
    std::vector<NiniConfigSourceBase*> sources;
};
NiniConfigSourceBase::NiniConfigSourceBase(NiniConfigArena* a, T type)
    : NiniConfigObject(a), state_(new State) {
    state_->alias = std::make_unique<NiniAliasText>(a->backend());
    state_->type = std::move(type);
    state_->configs.reset(new NiniConfigCollection(this));
}
T NiniConfigSourceBase::managed_type() const {
    return state_->type;
}
std::shared_ptr<NiniConfigCollection> NiniConfigSourceBase::configs() const {
    return arena_->handle(state_->configs.get());
}
bool NiniConfigSourceBase::auto_save() const {
    return state_->auto_save;
}
void NiniConfigSourceBase::auto_save(bool v) {
    state_->auto_save = v;
}
std::shared_ptr<NiniAliasText> NiniConfigSourceBase::alias() const {
    return arena_->handle(state_->alias.get());
}
void NiniConfigSourceBase::on_saved() {
    saved.invoke(*this, std::make_shared<NiniConfigEventArgs>());
}
void NiniConfigSourceBase::on_reloaded() {
    reloaded.invoke(*this, std::make_shared<NiniConfigEventArgs>());
}
void NiniConfigSourceBase::save() {
    on_saved();
}
void NiniConfigSourceBase::reload() {
    on_reloaded();
}
std::shared_ptr<NiniConfig> NiniConfigSourceBase::add_config(T name) {
    return configs()->add(std::move(name));
}
void NiniConfigSourceBase::merge(std::shared_ptr<NiniConfigSourceBase> source) {
    bool found = false;
    for (auto old : state_->sources) {
        if (!source) {
            if (!old) {
                found = true;
                break;
            }
        } else if (old && old->equals(source.get())) {
            found = true;
            break;
        }
    }
    if (!found) {
        arena_->retain(source);
        state_->sources.push_back(source.get());
    }
    if (!source)
        nullref();
    auto e = source->configs()->get_enumerator();
    while (e.move_next())
        configs()->add(handle(config_cast(e.current().get())));
}
struct NiniConfigSourceBase::Expansion::State {
    std::shared_ptr<NiniConfigSourceBase> source;
    std::shared_ptr<NiniConfig> config;
    T key, text;
    bool set = false, started = false, done = false;
};
NiniConfigSourceBase::Expansion::Expansion(std::shared_ptr<State> p) : state_(std::move(p)) {
}
NiniConfigSourceBase::Expansion NiniConfigSourceBase::expansion(std::shared_ptr<NiniConfig> config, T k,
                                                                bool set) {
    auto p = std::make_shared<Expansion::State>();
    p->source = handle(this);
    p->config = std::move(config);
    p->key = std::move(k);
    p->set = set;
    return Expansion(p);
}
bool NiniConfigSourceBase::Expansion::step() {
    auto& s = *state_;
    if (s.done)
        return true;
    if (!s.started) {
        s.started = true;
        if (!s.config)
            nullref();
        s.text = s.config->get(s.key);
        if (!s.text)
            fail(NiniConfigError::Kind::Argument,
                 u"[" + val(s.key) + u"] not found in [" + val(s.config->name()) + u"]");
    }
    auto backend = s.source->arena()->backend();
    static const auto open = txt(u"${"), close = txt(u"}");
    int begin = backend->index_of_current(s.text, open, 0), end = -1;
    if (begin != -1)
        end = backend->index_of_current(s.text, close, begin + 2);
    if (begin == -1 || end == -1) {
        if (s.set)
            s.config->set_text(s.key, s.text);
        s.done = true;
        return true;
    }
    if (begin < 0 || end < begin + 2 || std::size_t(end) > s.text->size())
        throw std::logic_error("CurrentCulture IndexOf provider returned an invalid UTF16 range");
    auto search = txt(s.text->substr(begin + 2, end - (begin + 2)));
    if (same(search, s.key))
        fail(NiniConfigError::Kind::Argument, u"Key cannot have a expand value of itself: " + val(s.key));
    auto pipe = search->find(u'|');
    T replacement;
    if (pipe != std::u16string::npos) {
        auto section = txt(search->substr(0, pipe));
        auto rest = search->substr(pipe + 1);
        auto next = rest.find(u'|');
        auto k = txt(rest.substr(0, next));
        auto config = s.source->configs()->at(section);
        if (!config)
            fail(NiniConfigError::Kind::Argument, u"Expand config not found: " + val(section));
        replacement = config->get(k);
        if (!replacement)
            fail(NiniConfigError::Kind::Argument, u"Expand key not found: " + val(k));
    } else {
        replacement = s.config->get(search);
        if (!replacement)
            fail(NiniConfigError::Kind::Argument, u"Key not found: " + val(search));
    }
    auto pattern = u"${" + *search + u"}";
    if (pattern != *replacement) {
        auto at = s.text->find(pattern);
        if (at != std::u16string::npos) {
            std::u16string result;
            std::size_t start = 0;
            do {
                result += s.text->substr(start, at - start);
                result += *replacement;
                start = at + pattern.size();
                at = s.text->find(pattern, start);
            } while (at != std::u16string::npos);
            result += s.text->substr(start);
            s.text = std::make_shared<const std::u16string>(std::move(result));
        }
    }
    return false;
}
T NiniConfigSourceBase::Expansion::result() const {
    return state_->text;
}
T NiniConfigSourceBase::get_expanded(std::shared_ptr<NiniConfig> config, T k) {
    auto current = expansion(std::move(config), std::move(k));
    while (!current.step()) {
    }
    return current.result();
}
void NiniConfigSourceBase::expand_key_values() {
    auto e = configs()->get_enumerator();
    while (e.move_next()) {
        auto config = config_cast(e.current().get());
        if (!config)
            nullref();
        auto keys = config->get_keys();
        for (auto& k : keys) {
            auto current = expansion(handle(config), k, true);
            while (!current.step()) {
            }
        }
    }
}
struct NiniIniConfigSource::IniState {
    std::shared_ptr<NiniDocument> document;
    T save_path;
    bool case_sensitive = true;
};
NiniIniConfigSource::NiniIniConfigSource(NiniConfigArena* a)
    : NiniConfigSourceBase(a, txt(u"Nini.Config.IniConfigSource")), ini_(new IniState) {
    ini_->document = std::make_shared<NiniDocument>();
}
bool NiniIniConfigSource::case_sensitive() const {
    return ini_->case_sensitive;
}
void NiniIniConfigSource::case_sensitive(bool v) {
    ini_->case_sensitive = v;
}
T NiniIniConfigSource::save_path() const {
    return ini_->save_path;
}
std::shared_ptr<NiniDocument> NiniIniConfigSource::document() const {
    return ini_->document;
}
void NiniIniConfigSource::load(T path) {
    auto f = arena_->backend()->file_reader;
    if (!f)
        throw std::invalid_argument("IniConfigSource.Load(file) requires actual StreamReader file provider");
    load(f(path));
    ini_->save_path = std::move(path);
}
void NiniIniConfigSource::load(std::shared_ptr<NiniWriterStream> stream) {
    auto f = arena_->backend()->stream_reader;
    if (!f)
        throw std::invalid_argument("IniConfigSource.Load(stream) requires actual StreamReader provider");
    load(f(std::move(stream)));
}
void NiniIniConfigSource::load(std::shared_ptr<NiniTextReader> reader) {
    auto document = std::make_shared<NiniDocument>(std::move(reader));
    load(std::move(document));
}
void NiniIniConfigSource::load(std::shared_ptr<NiniDocument> document) {
    configs()->clear();
    merge(handle(this));
    ini_->document = std::move(document);
    load_document();
}
void NiniIniConfigSource::load_document() {
    if (!ini_->document)
        nullref();
    for (std::int32_t i = 0; i < ini_->document->sections().count(); ++i) {
        auto section = ini_->document->sections().at(i);
        if (!section)
            nullref();
        auto config = arena_->create_config(section->name(), handle(this), true);
        for (std::int32_t j = 0; j < section->item_count(); ++j) {
            auto item = section->get_item(j);
            if (!item)
                nullref();
            if (item->type() == NiniType::Key)
                config->add(item->name(), item->value());
        }
        configs()->add(config);
    }
}
void NiniIniConfigSource::merge_configs_into_document() {
    if (!ini_->document)
        nullref();
    for (std::int32_t i = 0; i < ini_->document->sections().count(); ++i) {
        auto section = ini_->document->sections().at(i);
        if (!section)
            nullref();
        if (!configs()->at(section->name()))
            ini_->document->sections().remove(section->name());
    }
    auto e = configs()->get_enumerator();
    while (e.move_next()) {
        auto config = config_cast(e.current().get());
        if (!config)
            nullref();
        auto keys = config->get_keys();
        if (!ini_->document)
            nullref();
        if (!ini_->document->sections().at(config->name()))
            ini_->document->sections().add(std::make_shared<NiniSection>(config->name()));
        auto section = ini_->document->sections().at(config->name());
        if (section) {
            auto old = section->get_keys();
            for (auto& k : old) {
                auto live = configs()->at(config->name());
                if (!live)
                    nullref();
                if (!live->get(k))
                    section->remove(k);
            }
        }
        for (auto& k : keys) {
            if (!ini_->document)
                nullref();
            auto target = ini_->document->sections().at(config->name());
            auto value = config->get(k);
            if (!target)
                nullref();
            target->set(k, value);
        }
    }
}
void NiniIniConfigSource::save() {
    if (!ini_->save_path)
        fail(NiniConfigError::Kind::Argument, u"Source cannot be saved in this state");
    merge_configs_into_document();
    if (!ini_->document)
        nullref();
    ini_->document->save(ini_->save_path, arena_->backend()->document);
    NiniConfigSourceBase::save();
}
void NiniIniConfigSource::save(T path) {
    ini_->save_path = std::move(path);
    save();
}
void NiniIniConfigSource::save(std::shared_ptr<NiniTextWriter> writer) {
    merge_configs_into_document();
    if (!ini_->document)
        nullref();
    ini_->document->save(std::move(writer));
    ini_->save_path.reset();
    on_saved();
}
void NiniIniConfigSource::save(std::shared_ptr<NiniWriterStream> stream) {
    merge_configs_into_document();
    if (!ini_->document)
        nullref();
    ini_->document->save(std::move(stream), arena_->backend()->document);
    ini_->save_path.reset();
    on_saved();
}
T NiniIniConfigSource::to_string() {
    merge_configs_into_document();
    auto writer = std::make_shared<NiniStringWriter>();
    if (!ini_->document)
        nullref();
    ini_->document->save(writer);
    return writer->to_string();
}
void NiniIniConfigSource::reload() {
    if (!ini_->save_path)
        fail(NiniConfigError::Kind::Argument,
             u"Error reloading: You must have the loaded the source from a file");
    ini_->document = std::make_shared<NiniDocument>(ini_->save_path, arena_->backend()->document);
    merge_document_into_configs();
    NiniConfigSourceBase::reload();
}
void NiniIniConfigSource::merge_document_into_configs() {
    for (std::int32_t i = configs()->count() - 1; i >= 0; --i) {
        auto config = configs()->at(i);
        if (!config)
            nullref();
        if (!ini_->document)
            nullref();
        if (!ini_->document->sections().at(config->name()))
            configs()->remove(config);
    }
    for (std::int32_t i = 0; i < ini_->document->sections().count(); ++i) {
        auto section = ini_->document->sections().at(i);
        if (!section)
            nullref();
        auto config = configs()->at(section->name());
        if (!config) {
            config = arena_->create_config(section->name(), handle(this));
            configs()->add(config);
        }
        auto current = ini_->document->sections().at(config->name());
        if (!current)
            nullref();
        auto keys = config->get_keys();
        for (auto& k : keys)
            if (!current->contains(k))
                config->remove(k);
        auto after = current->get_keys();
        for (std::size_t j = 0; j < after.size(); ++j) {
            auto item = current->get_item(static_cast<std::int32_t>(j));
            if (!item)
                nullref();
            config->set_text(after[j], item->value());
        }
    }
}
std::shared_ptr<NiniConfigSourceBase> NiniConfigArena::create_source(T type) {
    if (!type)
        type = txt(u"BaseSource");
    auto p = new NiniConfigSourceBase(this, std::move(type));
    state_->objects.emplace_back(p);
    return handle(p);
}
std::shared_ptr<NiniIniConfigSource> NiniConfigArena::create_ini_source() {
    auto p = new NiniIniConfigSource(this);
    state_->objects.emplace_back(p);
    return handle(p);
}
std::shared_ptr<NiniConfig>
NiniConfigArena::create_config(T name, std::shared_ptr<NiniConfigSourceBase> source, bool ini) {
    retain(source);
    NiniConfig* p = ini ? static_cast<NiniConfig*>(new NiniIniConfig(this, std::move(name), source.get()))
                        : new NiniConfig(this, std::move(name), source.get(), false);
    state_->objects.emplace_back(p);
    return handle(p);
}
std::shared_ptr<NiniIniConfigSource> NiniConfigArena::create_ini_source(T path) {
    auto source = create_ini_source();
    source->load(std::move(path));
    return source;
}
std::shared_ptr<NiniIniConfigSource>
NiniConfigArena::create_ini_source(std::shared_ptr<NiniTextReader> reader) {
    auto source = create_ini_source();
    source->load(std::move(reader));
    return source;
}
std::shared_ptr<NiniIniConfigSource>
NiniConfigArena::create_ini_source(std::shared_ptr<NiniDocument> document) {
    auto source = create_ini_source();
    source->load(std::move(document));
    return source;
}
std::shared_ptr<NiniIniConfigSource>
NiniConfigArena::create_ini_source(std::shared_ptr<NiniWriterStream> stream) {
    auto source = create_ini_source();
    source->load(std::move(stream));
    return source;
}

}
