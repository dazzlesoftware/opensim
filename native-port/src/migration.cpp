// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/services/migration.hpp"
#include "opensim/framework/culture_prefix.hpp"
#include "opensim/framework/culture_sort.hpp"
#include "opensim/framework/text.hpp"
#include "opensim/services/user_account.hpp"
#include <algorithm>
#include <exception>
namespace opensim::services {
namespace {
template <class T> T& required(const std::shared_ptr<T>& value) {
    if (!value)
        throw std::invalid_argument("Null migration reference");
    return *value;
}
template <class T, class F> void using_dispose(std::shared_ptr<T> value, F action) {
    std::exception_ptr error;
    try {
        action();
    } catch (...) {
        error = std::current_exception();
    }
    try {
        if (value)
            value->dispose();
    } catch (...) {
        error = std::current_exception();
    }
    if (error)
        std::rethrow_exception(error);
}
std::string name(const std::optional<std::string>& value) {
    return value.value_or("");
}
std::string resource_text(const std::shared_ptr<MigrationResources>& source, const std::string& resource) {
    auto bytes = required(source).bytes(resource);
    if (!bytes)
        throw std::invalid_argument("Missing migration resource stream");
    return framework::stream_reader_text(*bytes);
}
#ifdef _WIN32
constexpr auto newline = "\r\n";
#else
constexpr auto newline = "\n";
#endif
}
Migration::Migration(std::shared_ptr<MigrationDatabase> database,
                     std::shared_ptr<MigrationResources> resources, std::optional<std::string> type,
                     MigrationLog log)
    : Migration(std::move(database), std::move(resources), std::string{}, std::move(type), std::move(log)) {
}
Migration::Migration(std::shared_ptr<MigrationDatabase> database,
                     std::shared_ptr<MigrationResources> resources, std::optional<std::string> subtype,
                     std::optional<std::string> type, MigrationLog log)
    : log_(std::move(log)) {
    initialize(std::move(database), std::move(resources), std::move(type), std::move(subtype));
}
void Migration::initialize(std::shared_ptr<MigrationDatabase> database,
                           std::shared_ptr<MigrationResources> resources, std::optional<std::string> type,
                           std::optional<std::string> subtype) {
    type_ = std::move(type);
    database_ = std::move(database);
    resources_ = std::move(resources);
    old_ =
        std::make_shared<framework::UnicodeRegex>(name(subtype) + "\\.(\\d\\d\\d)_" + name(type_) + "\\.sql");
    auto part = !subtype || subtype->empty() ? name(type_) : name(type_) + "\\." + *subtype;
    merged_ =
        std::make_shared<framework::UnicodeRegex>("\\." + part + "\\.migrations(?:\\.(?<ver>\\d+)$|.*)");
}
void Migration::execute_script(std::shared_ptr<MigrationDatabase> database,
                               const std::vector<std::string>& scripts) {
    auto command = required(database).command();
    using_dispose(command, [&] {
        required(command).timeout(0);
        for (auto& sql : scripts) {
            command->text(sql);
            try {
                command->execute();
            } catch (const std::exception& e) {
                throw std::runtime_error(std::string(e.what()) + " in SQL: " + sql);
            }
        }
    });
}
void Migration::execute(const std::string& sql) {
    execute_script(database_, {sql});
}
void Migration::execute(const std::vector<std::string>& sql) {
    execute_script(database_, sql);
}
int32_t Migration::find_version(std::shared_ptr<MigrationDatabase> database,
                                const std::optional<std::string>& type) {
    int32_t value = 0;
    auto command = required(database).command();
    using_dispose(command, [&] {
        try {
            required(command).text("select version from migrations where name='" + name(type) +
                                   "' order by version desc");
            auto reader = command->reader();
            using_dispose(reader, [&] {
                if (required(reader).read())
                    value = reader->version_value();
                reader->close();
            });
        } catch (...) {
            value = -1;
        }
    });
    return value;
}
void Migration::insert_version(const std::optional<std::string>& type, int32_t value) {
    if (log_.info)
        log_.info("[MIGRATIONS]: Creating " + name(type) + " at version " + std::to_string(value));
    execute("insert into migrations(name, version) values('" + name(type) + "', " + std::to_string(value) +
            ")");
}
void Migration::update_version(const std::optional<std::string>& type, int32_t value) {
    if (log_.info)
        log_.info("[MIGRATIONS]: Updating " + name(type) + " to version " + std::to_string(value));
    execute("update migrations set version=" + std::to_string(value) + " where name='" + name(type) + "'");
}
void Migration::init_migrations_table() {
    auto v = find_version(database_, std::string("migrations"));
    if (v <= 0) {
        if (v < 0)
            execute("create table migrations(name varchar(100), version int)");
        insert_version(std::string("migrations"), 1);
    }
}
int32_t Migration::version() {
    return find_version(database_, type_);
}
void Migration::version(int32_t value) {
    if (version() < 1)
        insert_version(type_, value);
    else
        update_version(type_, value);
}
void Migration::update() {
    init_migrations_table();
    int32_t current = find_version(database_, type_);
    auto scripts = migrations_after(current);
    if (scripts.empty())
        return;
    if (log_.info) {
        log_.info("[MIGRATIONS]: Upgrading " + name(type_) + " to latest revision " +
                  std::to_string(scripts.rbegin()->first) + ".");
        log_.info("[MIGRATIONS]: NOTE - this may take a while, don't interrupt this process!");
    }
    for (auto& [next, batches] : scripts) {
        try {
            execute(batches);
        } catch (const std::exception& e) {
            if (log_.debug) {
                std::string message = e.what();
                std::replace(message.begin(), message.end(), '\n', ' ');
                log_.debug("[MIGRATIONS]: Cmd was " + message);
                log_.debug(
                    "[MIGRATIONS]: An error has occurred in the migration.  If you're running OpenSim for the first time "
                    "then you can probably safely ignore this, since certain migration commands attempt to fetch data "
                    "out of old tables.  However, if you're using an existing database and you see database related "
                    "errors while running OpenSim then you will need to fix these problems manually. Continuing.");
            }
            execute("ROLLBACK;");
        }
        if (current == 0)
            insert_version(type_, next);
        else
            update_version(type_, next);
        current = next;
    }
}
Migration::Scripts Migration::migrations_after(int32_t after) {
    Scripts migrations;
    auto names = required(resources_).names();
    if (names.empty())
        return migrations;
    std::sort(names.begin(), names.end(),
              [](const auto& a, const auto& b) { return framework::invariant_compare(a, b) < 0; });
    std::optional<std::string> file;
    std::optional<std::vector<std::string>> match;
    for (auto it = names.rbegin(); it != names.rend(); ++it) {
        auto value = required(merged_).match(*it);
        if (value) {
            file = *it;
            match = std::move(value);
            break;
        }
    }
    if (file && !file->empty()) {
        int32_t suffix = 0;
        bool skip = match->size() > 1 && parse_account_integer((*match)[1], suffix) && suffix <= after;
        if (!skip) {
            std::string buffer;
            int32_t version = -1;
            std::vector<std::string> script;
            auto flush = [&] {
                if (!buffer.empty()) {
                    script.push_back(buffer);
                    buffer.clear();
                }
                if (version > 0 && version > after && !script.empty() && !migrations.contains(version))
                    migrations[version] = script;
                script.clear();
            };
            auto text = resource_text(resources_, *file);
            size_t at = 0;
            int32_t line_number = 0;
            while (at < text.size()) {
                auto end = text.find_first_of("\r\n", at);
                auto line = text.substr(at, end == text.npos ? end : end - at);
                if (end == text.npos)
                    at = text.size();
                else {
                    at = end + 1;
                    if (text[end] == '\r' && at < text.size() && text[at] == '\n')
                        ++at;
                }
                ++line_number;
                if (line.empty() || framework::invariant_starts_with(line, "#"))
                    continue;
                if (framework::invariant_equals_ignore_case(framework::trim_utf8(line), ":GO")) {
                    if (buffer.empty())
                        continue;
                    if (version > after)
                        script.push_back(buffer);
                    buffer.clear();
                    continue;
                }
                if (framework::invariant_starts_with_ignore_case(line, ":VERSION ")) {
                    flush();
                    auto comment = line.find('#');
                    if (comment != line.npos)
                        line.resize(comment);
                    auto units = framework::utf16_replacing(line);
                    if (units.size() < 9)
                        throw std::out_of_range("Migration version substring");
                    auto number = framework::trim_utf8(framework::utf8(std::u16string_view(units).substr(9)));
                    if (!parse_account_integer(number, version)) {
                        if (log_.error)
                            log_.error("[MIGRATIONS]: invalid version marker at " + *file + ": line " +
                                       std::to_string(line_number) + ". Migration failed!");
                        break;
                    }
                } else
                    buffer += line + newline;
            }
            flush();
            if (after < version)
                after = version;
        }
    }
    for (auto& resource : names) {
        auto value = required(old_).match(resource);
        if (value) {
            if (value->size() < 2)
                throw std::out_of_range("Migration regex group");
            int32_t version;
            if (!parse_account_integer((*value)[1], version))
                throw std::invalid_argument("Invalid old migration version");
            if (version > after && !migrations.contains(version))
                migrations[version] = {resource_text(resources_, resource)};
        }
    }
    if (migrations.empty() && log_.debug)
        log_.debug("[MIGRATIONS]: " + name(type_) + " data tables already up to date at revision " +
                   std::to_string(after));
    return migrations;
}
}
