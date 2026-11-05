// Source: bundled Nini.dll ArgvParser/ArgvConfigSource; Nini MIT license.
#include "opensim/framework/nini_argv.hpp"
namespace opensim::framework {
namespace {
using Text = NiniWriterText;
[[noreturn]] void nr() {
    throw NiniConfigError(NiniConfigError::Kind::NullReference,
                          u"Object reference not set to an instance of an object.");
}
[[noreturn]] void null(std::u16string p) {
    throw NiniConfigError(NiniConfigError::Kind::ArgumentNull,
                          u"Value cannot be null. (Parameter '" + p + u"')", p);
}
Text trim(std::u16string_view value) {
    std::size_t a = 0, b = value.size();
    while (a < b && (value[a] == u'\'' || value[a] == u'"'))
        ++a;
    while (b > a && (value[b - 1] == u'\'' || value[b - 1] == u'"'))
        --b;
    return nini_writer_text(std::u16string(value.substr(a, b - a)));
}
Text trim_argument(Text value) {
    auto& s = *value;
    bool needs =
        !s.empty() && (s.front() == u'\'' || s.front() == u'"' || s.back() == u'\'' || s.back() == u'"');
    return needs ? trim(s) : value;
}
void validate(const std::shared_ptr<NiniArgvBackend>& b) {
    if (!b || !b->regex_word || !b->regex_space || !b->lower_invariant)
        throw std::invalid_argument(
            "Argv requires Unicode regex word/space and invariant lowercase providers");
}
}
Text NiniArgvParser::key(Text value) const {
    if (!value)
        null(u"key");
    return backend_->lower_invariant(value);
}
NiniArgvParser::NiniArgvParser(NiniArgvArguments args, std::shared_ptr<NiniArgvBackend> backend)
    : backend_(std::move(backend)), parameters_(std::in_place) {
    validate(backend_);
    if (!args)
        nr();
    Text current;
    for (std::size_t index = 0; index < args->size(); ++index) {
        auto input = (*args)[index];
        if (!input)
            null(u"input");
        auto& s = *input;
        std::size_t start = 0;
        if (!s.empty() && (s[0] == u'/' || s[0] == u'-'))
            start = 1;
        std::size_t end = start;
        while (end < s.size() && backend_->regex_word(s[end]))
            ++end;
        // Regex alternation backtracks from '-' to '--' when the first name fails.
        if (end == start && s.size() >= 2 && s[0] == u'-' && s[1] == u'-') {
            start = 2;
            end = start;
            while (end < s.size() && backend_->regex_word(s[end]))
                ++end;
        }
        bool match = start > 0 && end > start;
        std::size_t tail = end;
        if (tail < s.size() && (s[tail] == u':' || s[tail] == u'='))
            ++tail;
        if (match) {
            auto lf = s.find(u'\n', tail);
            match = lf == std::u16string::npos || lf == s.size() - 1;
        }
        if (!match) {
            if (current) {
                auto k = key(current);
                for (auto& item : *parameters_)
                    if (*item.first == *k) {
                        item.second = trim_argument(input);
                        break;
                    }
            }
            continue;
        }
        current = nini_writer_text(s.substr(start, end - start));
        auto k = key(current);
        for (auto& item : *parameters_)
            if (*item.first == *k)
                throw NiniConfigError(NiniConfigError::Kind::Argument,
                                      u"Item has already been added. Key in dictionary: '" + *k +
                                          u"'  Key being added: '" + *k + u"'");
        auto limit = s.size();
        if (limit > tail && s.back() == u'\n')
            --limit;
        parameters_->emplace_back(std::move(k), trim(std::u16string_view(s).substr(tail, limit - tail)));
    }
}
NiniArgvParser::NiniArgvParser(Text args, std::shared_ptr<NiniArgvBackend> backend)
    : backend_(std::move(backend)) {
    validate(backend_);
    if (!args)
        null(u"input");
    bool any = false;
    for (auto c : *args)
        if (!backend_->regex_space(c)) {
            any = true;
            break;
        }
    if (!any)
        throw NiniConfigError(NiniConfigError::Kind::Overflow,
                              u"Arithmetic operation resulted in an overflow.");
}
Text NiniArgvParser::get(Text param) const {
    if (!parameters_)
        nr();
    auto k = key(std::move(param));
    for (auto& item : *parameters_)
        if (*item.first == *k)
            return item.second;
    return {};
}
NiniArgvConfigSource::NiniArgvConfigSource(NiniConfigArena* a, NiniArgvArguments args,
                                           std::shared_ptr<NiniArgvBackend> b)
    : NiniConfigSourceBase(a, nini_writer_text(u"Nini.Config.ArgvConfigSource")), parser_(args, std::move(b)),
      arguments_(std::move(args)) {
}
std::shared_ptr<NiniArgvConfigSource> NiniArgvConfigSource::create(std::shared_ptr<NiniConfigArena> arena,
                                                                   NiniArgvArguments args,
                                                                   std::shared_ptr<NiniArgvBackend> b) {
    if (!arena)
        throw std::invalid_argument("Argv source requires arena");
    auto value = std::unique_ptr<NiniArgvConfigSource>(
        new NiniArgvConfigSource(arena.get(), std::move(args), std::move(b)));
    auto pointer = value.get();
    arena->adopt(std::move(value));
    return arena->handle(pointer);
}
void NiniArgvConfigSource::save() {
    throw NiniConfigError(NiniConfigError::Kind::Argument, u"Source is read only");
}
void NiniArgvConfigSource::reload() {
    throw NiniConfigError(NiniConfigError::Kind::Argument, u"Source cannot be reloaded");
}
void NiniArgvConfigSource::add_switch(Text name, Text long_name, Text short_name) {
    auto config = configs()->at(name);
    if (!config) {
        config = arena_->create_config(name, arena_->handle(this));
        configs()->add(config);
    }
    if (short_name && (short_name->size() < 1 || short_name->size() > 2))
        throw NiniConfigError(NiniConfigError::Kind::Argument, u"Short name may only be 1 or 2 characters");
    if (parser_.get(long_name))
        config->set_text(long_name, parser_.get(long_name));
    else if (short_name && parser_.get(short_name))
        config->set_text(long_name, parser_.get(short_name));
}
NiniArgvArguments NiniArgvConfigSource::get_arguments() const {
    if (!arguments_)
        nr();
    return std::make_shared<NiniArgvArgumentArray>(*arguments_);
}
void setup_opensim_application_argv(const std::shared_ptr<NiniArgvConfigSource>& source) {
    if (!source)
        nr();
    source->add_switch(nini_writer_text(u"Startup"), nini_writer_text(u"logconfig"));
    for (auto name : {u"On", u"Off", u"True", u"False", u"Yes", u"No"})
        source->alias()->add_alias(nini_writer_text(name), name == std::u16string_view(u"On") ||
                                                               name == std::u16string_view(u"True") ||
                                                               name == std::u16string_view(u"Yes"));
    for (auto name : {u"background", u"inifile", u"inimaster", u"inidirectory", u"physics", u"gui",
                      u"console", u"save_crashes", u"crash_dir"})
        source->add_switch(nini_writer_text(u"Startup"), nini_writer_text(name));
    source->add_config(nini_writer_text(u"StandAlone"));
    source->add_config(nini_writer_text(u"Network"));
}
}
