// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#pragma once
#include <memory>
#include <functional>
#include <optional>
#include <unordered_map>
#include <string>
#include <vector>
namespace opensim::framework {
class NiniXmlDom;
class NiniXmlDomNode;
struct ConsoleCommandDescription {
    std::optional<std::string> module;
    std::string command;
    std::optional<std::string> usage, description, descriptive_help;
    bool shared = false;
};
// Original Commands tree, callbacks, nullable metadata, help, completion and
// XML exchange. Registration precedes concurrent console reads.
class ConsoleCommandResolver {
public:
    using Callback =
        std::shared_ptr<std::function<void(const std::optional<std::string>&, std::vector<std::string>&)>>;

private:
    struct Node;
    std::unique_ptr<Node> root_;
    struct Callbacks {
        std::vector<Callback> values;
        std::size_t version = 0;
    };
    std::unordered_map<const ConsoleCommandDescription*, Callbacks> callbacks_;
    std::shared_ptr<ConsoleCommandDescription> resolve_target(std::vector<std::string>&) const;
    std::vector<std::shared_ptr<ConsoleCommandDescription>> registrations_;

public:
    ConsoleCommandResolver();
    ~ConsoleCommandResolver();
    void add(const std::string& command);
    void add(ConsoleCommandDescription command);
    // Reuse a Callback handle to represent the same original delegate identity.
    void add(ConsoleCommandDescription command, Callback callback);
    bool has_command(const std::string& command) const;
    std::shared_ptr<NiniXmlDomNode> get_xml(NiniXmlDom* document);
    void from_xml(std::shared_ptr<NiniXmlDomNode> root, Callback callback);
    // Success aliases the caller array; failure/null handlers have no result.
    std::optional<std::reference_wrapper<std::vector<std::string>>>
    invoke(std::vector<std::string>& command) const;
    std::vector<std::string> next_options(const std::vector<std::string>& command, bool terminated) const;
    std::vector<std::optional<std::string>> help(const std::vector<std::string>& command) const;
    // Original result aliases cmd: even failed resolution may expand earlier words.
    bool resolve(std::vector<std::string>& command) const;
};
}
