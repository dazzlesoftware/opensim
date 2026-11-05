// Port of bundled Nini.Config.XmlConfigSource; copyright (c) 2006 Brent R. Matzelle.
// MIT license: native-port/third_party/nini/LICENSE.
#include "opensim/framework/nini_xml_config.hpp"
#include "opensim/framework/text.hpp"
namespace opensim::framework {
namespace {
using Text = NiniWriterText;
using Node = std::shared_ptr<NiniXmlNode>;
const Text root_name = nini_writer_text(u"Nini"), section_name = nini_writer_text(u"Section"),
           key_name = nini_writer_text(u"Key"), name_attribute = nini_writer_text(u"Name"),
           value_attribute = nini_writer_text(u"Value");
bool same(const Text& a, const Text& b) {
    return (!a && !b) || (a && b && *a == *b);
}
[[noreturn]] void nullref() {
    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
[[noreturn]] void argument(std::u16string message) {
    throw NiniConfigError(NiniConfigError::Kind::Argument, std::move(message));
}
template <class T> const std::shared_ptr<T>& required(const std::shared_ptr<T>& value) {
    if (!value)
        nullref();
    return value;
}
bool element(const Node& node, const Text& name) {
    return required(node)->node_type() == 1 && same(node->name(), name);
}
}
NiniXmlProviderError::NiniXmlProviderError(std::string type, std::uint32_t code, std::u16string text,
                                           std::optional<std::u16string> name)
    : runtime_error(utf8(text)), source_type(std::move(type)), hresult(code), message(std::move(text)),
      parameter(std::move(name)) {
}
NiniXmlConfigSource::NiniXmlConfigSource(NiniConfigArena* arena,
                                         std::shared_ptr<NiniXmlConfigBackend> backend)
    : NiniConfigSourceBase(arena, nini_writer_text(u"Nini.Config.XmlConfigSource")),
      backend_(std::move(backend)) {
}
std::shared_ptr<NiniXmlConfigSource>
NiniXmlConfigSource::allocate(const std::shared_ptr<NiniConfigArena>& arena,
                              const std::shared_ptr<NiniXmlConfigBackend>& backend) {
    if (!arena || !backend || !backend->create_document || !backend->string_writer)
        throw std::invalid_argument(
            "XmlConfigSource requires a retained arena and complete XML DOM/writer provider");
    auto retained = arena;
    auto dependencies = backend;
    return std::dynamic_pointer_cast<NiniXmlConfigSource>(retained->adopt(
        std::unique_ptr<NiniConfigObject>(new NiniXmlConfigSource(retained.get(), dependencies))));
}
std::shared_ptr<NiniXmlConfigSource>
NiniXmlConfigSource::create(const std::shared_ptr<NiniConfigArena>& arena,
                            const std::shared_ptr<NiniXmlConfigBackend>& backend) {
    auto source = allocate(arena, backend);
    source->document_ = source->backend_->create_document();
    auto receiver = source->document_;
    required(receiver)->load_xml(nini_writer_text(u"<Nini/>"));
    source->perform_load(source->document_);
    return source;
}
std::shared_ptr<NiniXmlConfigSource>
NiniXmlConfigSource::create(const std::shared_ptr<NiniConfigArena>& arena,
                            const std::shared_ptr<NiniXmlConfigBackend>& backend, Text path) {
    auto source = allocate(arena, backend);
    source->load(std::move(path));
    return source;
}
std::shared_ptr<NiniXmlConfigSource>
NiniXmlConfigSource::create(const std::shared_ptr<NiniConfigArena>& arena,
                            const std::shared_ptr<NiniXmlConfigBackend>& backend,
                            std::shared_ptr<NiniXmlReader> reader) {
    auto source = allocate(arena, backend);
    source->load(std::move(reader));
    return source;
}
void NiniXmlConfigSource::load(Text path) {
    const auto lifetime = arena()->handle(this);
    save_path_ = path;
    document_ = backend_->create_document();
    auto receiver = document_;
    required(receiver)->load_path(path);
    perform_load(document_);
}
void NiniXmlConfigSource::load(std::shared_ptr<NiniXmlReader> reader) {
    const auto lifetime = arena()->handle(this);
    document_ = backend_->create_document();
    auto receiver = document_;
    required(receiver)->load_reader(reader);
    perform_load(document_);
}
void NiniXmlConfigSource::save() {
    const auto lifetime = arena()->handle(this);
    if (!save_path_ || !document_)
        argument(u"Source cannot be saved in this state");
    merge_configs_into_document();
    auto receiver = document_;
    required(receiver)->save_path(save_path_);
    NiniConfigSourceBase::save();
}
void NiniXmlConfigSource::save(Text path) {
    const auto lifetime = arena()->handle(this);
    save_path_ = std::move(path);
    save();
}
void NiniXmlConfigSource::save(std::shared_ptr<NiniTextWriter> writer) {
    const auto lifetime = arena()->handle(this);
    merge_configs_into_document();
    auto receiver = document_;
    required(receiver)->save_writer(writer);
    save_path_.reset();
    on_saved();
}
void NiniXmlConfigSource::save(std::shared_ptr<NiniWriterStream> stream) {
    const auto lifetime = arena()->handle(this);
    merge_configs_into_document();
    auto receiver = document_;
    required(receiver)->save_stream(stream);
    save_path_.reset();
    on_saved();
}
void NiniXmlConfigSource::reload() {
    const auto lifetime = arena()->handle(this);
    if (!save_path_)
        argument(u"Error reloading: You must have the loaded the source from a file");
    document_ = backend_->create_document();
    auto receiver = document_;
    required(receiver)->load_path(save_path_);
    merge_document_into_configs();
    NiniConfigSourceBase::reload();
}
Text NiniXmlConfigSource::to_string() {
    const auto lifetime = arena()->handle(this);
    merge_configs_into_document();
    auto writer = backend_->string_writer();
    auto receiver = document_;
    required(receiver)->save_writer(writer);
    return required(writer)->to_string();
}
void NiniXmlConfigSource::perform_load(std::shared_ptr<NiniXmlDocument> document) {
    configs()->clear();
    merge(arena()->handle(this));
    if (!same(required(required(document)->document_element())->name(), root_name))
        argument(u"Did not find Nini XML root node");
    load_sections(document->document_element());
}
void NiniXmlConfigSource::load_sections(Node root) {
    auto e = required(root)->children();
    while (e->move_next()) {
        auto node = e->current();
        if (element(node, section_name)) {
            auto config = arena()->create_config(required(node->attribute(name_attribute))->value(),
                                                 arena()->handle(this));
            configs()->add(config);
            load_keys(node, config);
        }
    }
}
void NiniXmlConfigSource::load_keys(Node node, std::shared_ptr<NiniConfig> config) {
    auto e = required(node)->children();
    while (e->move_next()) {
        auto child = e->current();
        if (element(child, key_name)) {
            auto name = required(child->attribute(name_attribute))->value();
            auto value = required(child->attribute(value_attribute))->value();
            config->add(name, value);
        }
    }
}
void NiniXmlConfigSource::merge_configs_into_document() {
    remove_sections();
    auto e = configs()->get_enumerator();
    while (e.move_next()) {
        auto config = std::dynamic_pointer_cast<NiniConfig>(e.current());
        required(config);
        auto keys = config->get_keys();
        auto node = get_section(config->name());
        if (!node) {
            node = section_node(config->name());
            required(required(document_receiver())->document_element())->append_child(node);
        }
        remove_keys(config->name());
        for (const auto& key : keys)
            set_key(node, key, config->get(key));
    }
}
void NiniXmlConfigSource::remove_sections() {
    auto root = required(required(document_receiver())->document_element());
    auto e = root->children();
    while (e->move_next()) {
        auto node = e->current();
        if (element(node, section_name)) {
            auto attribute = node->attribute(name_attribute);
            if (!attribute)
                argument(u"Section name attribute not found");
            if (!configs()->at(attribute->value()))
                required(required(document_receiver())->document_element())->remove_child(node);
        }
    }
}
void NiniXmlConfigSource::remove_keys(Text section) {
    auto node = get_section(section);
    if (!node)
        return;
    auto e = node->children();
    while (e->move_next()) {
        auto child = e->current();
        if (element(child, key_name)) {
            auto attribute = child->attribute(name_attribute);
            if (!attribute)
                argument(u"Name attribute not found in key");
            if (!required(configs()->at(section))->get(attribute->value()))
                node->remove_child(child);
        }
    }
}
Node NiniXmlConfigSource::get_section(Text name) {
    auto root = required(required(document_receiver())->document_element());
    auto e = root->children();
    while (e->move_next()) {
        auto node = e->current();
        if (element(node, section_name) && same(required(node->attribute(name_attribute))->value(), name))
            return node;
    }
    return {};
}
Node NiniXmlConfigSource::get_key(Node section, Text name) {
    auto e = required(section)->children();
    while (e->move_next()) {
        auto node = e->current();
        if (element(node, key_name) && same(required(node->attribute(name_attribute))->value(), name))
            return node;
    }
    return {};
}
Node NiniXmlConfigSource::section_node(Text name) {
    auto node = required(document_receiver())->create_element(section_name);
    auto attribute = required(document_receiver())->create_attribute(name_attribute);
    required(attribute)->value(std::move(name));
    required(node)->append_attribute(attribute);
    return node;
}
void NiniXmlConfigSource::create_key(Node section, Text key, Text value) {
    auto node = required(document_receiver())->create_element(key_name);
    auto name = required(document_receiver())->create_attribute(name_attribute);
    auto data = required(document_receiver())->create_attribute(value_attribute);
    required(name)->value(std::move(key));
    required(data)->value(std::move(value));
    required(node)->append_attribute(name);
    node->append_attribute(data);
    required(section)->append_child(node);
}
void NiniXmlConfigSource::set_key(Node section, Text key, Text value) {
    auto node = get_key(section, key);
    if (!node)
        create_key(section, std::move(key), std::move(value));
    else
        required(node->attribute(value_attribute))->value(std::move(value));
}
void NiniXmlConfigSource::merge_document_into_configs() {
    remove_configs();
    auto root = required(required(document_receiver())->document_element());
    auto e = root->children();
    while (e->move_next()) {
        auto node = e->current();
        if (element(node, section_name)) {
            auto name = required(node->attribute(name_attribute))->value();
            auto config = configs()->at(name);
            if (!config) {
                config = arena()->create_config(name, arena()->handle(this));
                configs()->add(config);
            }
            remove_config_keys(config);
        }
    }
}
void NiniXmlConfigSource::remove_configs() {
    for (auto i = configs()->count() - 1; i >= 0; --i) {
        auto config = configs()->at(i);
        if (!get_section(required(config)->name()))
            configs()->remove(config);
    }
}
void NiniXmlConfigSource::remove_config_keys(std::shared_ptr<NiniConfig> config) {
    auto section = get_section(required(config)->name());
    auto keys = config->get_keys();
    for (const auto& key : keys)
        if (!get_key(section, key))
            config->remove(key);
    auto e = required(section)->children();
    while (e->move_next()) {
        auto node = e->current();
        if (element(node, key_name)) {
            auto name = required(node->attribute(name_attribute))->value();
            auto value = required(node->attribute(value_attribute))->value();
            config->set_text(name, value);
        }
    }
}
}
