/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include "opensim/framework/identity.hpp"
#include "opensim/network/rpc.hpp"
#include "opensim/server/login_application.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <set>
#include <sqlite3.h>
#ifdef OPENSIM_TEST_TLS
#include "../src/http_tls_certificate.hpp"
#include "opensim/network/http_server.hpp"
#include <openssl/ssl.h>
#endif
using namespace opensim;
using network::Value;

namespace {
int failures = 0;

void check(bool okay, const char *name) {
    if (!okay) {
        std::cerr << "FAIL: " << name << '\n';
        ++failures;
    }
}

class StatisticsUsers final : public services::GridUserStore {
  public:
    std::vector<std::shared_ptr<services::GridUserData>> rows;

    std::shared_ptr<services::GridUserData> get(const std::string &) override {
        return {};
    }

    std::vector<std::shared_ptr<services::GridUserData>> get_all(const std::string &) override {
        return rows;
    }

    bool store(const std::shared_ptr<services::GridUserData> &) override {
        return false;
    }
};

template <class F> bool throws(F &&call) {
    try {
        call();
    } catch (const std::exception &) {
        return true;
    }
    return false;
}

class RecordingLogin final : public services::ILoginService {
  public:
    services::LoginRequest request;
    int calls = 0, level = 0;

    Value login(const services::LoginRequest &value) override {
        request = value;
        ++calls;
        return services::login_failure("GridProblem");
    }

    Value set_level(std::string_view first, std::string_view last, std::string_view password, int value) override {
        request.first = first;
        request.last = last;
        request.password = password;
        level = value;
        return Value::object({{"success", Value(true)}});
    }
};

void identity_tests() {
    for (const auto &[input, expected] :
         std::map<std::string, std::string>{{"", "d41d8cd98f00b204e9800998ecf8427e"},
                                            {"a", "0cc175b9c0f1b6a831c399e269772661"},
                                            {"abc", "900150983cd24fb0d6963f7d28e17f72"},
                                            {"message digest", "f96b697d7cb7938d525a2f31aaf161d0"},
                                            {"abcdefghijklmnopqrstuvwxyz", "c3fcd3d76192e4007dfb496cca67e13b"}})
        check(framework::md5(input) == expected, "RFC MD5 vector");
    check(framework::md5(std::string(1000000, 'a')) == "7707d6ae4e027c70eea2a935c2296f21", "million-byte MD5 vector");
    std::set<std::string> ids;
    for (int i = 0; i < 1000; ++i) {
        auto uuid = framework::random_uuid();
        check(uuid.size() == 36 && uuid[14] == '4' && std::string("89ab").find(uuid[19]) != std::string::npos,
              "UUID version and variant");
        ids.insert(uuid);
    }
    check(ids.size() == 1000, "OS random UUID distinctness");
    check(framework::normalize_uuid("{ABCDEF01-2345-6789-ABCD-EF0123456789}") == "abcdef01-2345-6789-abcd-ef0123456789",
          "UUID normalization");
    check(throws([] { framework::normalize_uuid("bad-uuid"); }), "invalid UUID rejected");
    check(framework::constant_time_equal("abc", "abc") && !framework::constant_time_equal("abc", "abd"),
          "credential comparison");
}

void codec_tests() {
    auto call = network::parse_xml_rpc(
        "<?xml version=\"1.0\"?><methodCall><methodName>login_to_simulator</methodName><params><param><value><struct>"
        "<member><name>first</name><value><string>A &amp; B</string></value></member>"
        "<member><name>options</name><value><array><data>"
        "<value><string>inventory-root</string></value>"
        "</data></array></value></member>"
        "</struct></value></param></params></methodCall>");
    check(call.method == "login_to_simulator" && call.parameters[0].get("first").text == "A & B",
          "XML-RPC decoding and entities");
    check(call.parameters[0].get("options").array[0].text == "inventory-root", "XML-RPC options array");
    auto fallback =
        network::parse_xml_rpc("<methodCall><methodName>test</methodName><params>"
                               "<param><value/></param>"
                               "<param><value><extension>first<!-- separator -->last</extension></value></param>"
                               "<param><value><string>first<![CDATA[ignored]]>last</string></value></param>"
                               "<param><value><string><![CDATA[ignored]]></string></value></param>"
                               "</params></methodCall>");
    check(fallback.parameters.size() == 4 && fallback.parameters[0].kind == Value{}.kind &&
              fallback.parameters[1].kind == Value::Kind::String && fallback.parameters[1].text == "last" &&
              fallback.parameters[2].kind == Value::Kind::String && fallback.parameters[2].text == "last" &&
              fallback.parameters[3].kind == Value{}.kind,
          "XML-RPC strings use the last text node and ignore CDATA");
    const auto containers =
        network::parse_xml_rpc("<methodCall><methodName>test</methodName><params>"
                               "<param><value><array/></value></param><param><value><array></array></value></param>"
                               "<param><value><struct/></value></param><param><value><struct></struct></value></param>"
                               "<param><value><array><value>A</value><data><value/><value>B</value></data>"
                               "<data><value>C</value></data></array></value></param></params></methodCall>");
    check(containers.parameters.size() == 5 && containers.parameters[0].kind == Value::Kind::Undefined &&
              containers.parameters[1].kind == Value::Kind::Array && containers.parameters[1].array.empty() &&
              containers.parameters[2].kind == Value::Kind::Undefined &&
              containers.parameters[3].kind == Value::Kind::Map && containers.parameters[3].map.empty(),
          "XML-RPC distinguishes self-closing and explicitly empty containers");
    const auto &values = containers.parameters[4].array;
    check(values.size() == 3 && values[0].text == "A" && values[1].text == "B" && values[2].text == "C",
          "XML-RPC arrays collect direct values and values across data elements");
    const auto slots = network::parse_xml_rpc("<methodCall><methodName>test</methodName><params><param/><param></param>"
                                              "<param><value>A</value><value>B</value></param>"
                                              "<param><value>A</value><value/></param></params></methodCall>");
    check(slots.parameters.size() == 3 && slots.parameters[0].kind == Value::Kind::Undefined &&
              slots.parameters[1].text == "B" && slots.parameters[2].kind == Value::Kind::Undefined,
          "XML-RPC skips self-closing parameters and retains explicitly closed slots");
    const auto repeated =
        network::parse_xml_rpc("<methodCall><methodName>first</methodName><methodName>last</methodName><methodName/>"
                               "<params><param><value>A</value></param></params>"
                               "<params><param><value>B</value></param></params></methodCall>");
    check(repeated.method == "last" && repeated.parameters.size() == 2 && repeated.parameters[0].text == "A" &&
              repeated.parameters[1].text == "B",
          "XML-RPC retains the last method text and collects parameters from every block");
    auto map =
        Value::object({{"x<&", Value("value<&\"")}, {"number", Value(42)}, {"flag", Value(true)}, {"nil", Value()}});
    auto xml = network::llsd_xml(map);
    auto parsed = network::parse_llsd_xml(xml);
    check(parsed.get("x<&").text == "value<&\"" && parsed.get("number").integer == 42 && parsed.get("flag").boolean,
          "LLSD typed round trip and escaping");
    check(throws([] { network::parse_llsd_xml("<llsd><map><key>a</key></map></llsd>"); }),
          "incomplete LLSD map rejected");
    check(throws([] {
              network::parse_xml_rpc("<!DOCTYPE x [<!ENTITY e SYSTEM "
                                     "'file:///secret'>]><methodCall><methodName>&e;</methodName></methodCall>");
          }),
          "DTDs rejected without external entity expansion");
    check(throws([] { network::parse_llsd_xml("<llsd><map/></llsd><other/>"); }), "multiple document roots rejected");
    std::string deep = "<llsd>";
    for (int i = 0; i < 70; ++i)
        deep += "<array>";
    deep += "<undef/>";
    for (int i = 0; i < 70; ++i)
        deep += "</array>";
    deep += "</llsd>";
    check(throws([&] { network::parse_llsd_xml(deep); }), "RPC recursion bounded");
}

void handler_tests() {
    RecordingLogin recording;
    services::LoginHandlers handlers(recording);
    auto input = Value::object({{"first", Value("Test")}, {"last", Value("User")}, {"passwd", Value("password")}});
    handlers.xml_rpc_login(input, "127.0.0.1");
    check(recording.calls == 1 && recording.request.version == "Unknown" &&
              recording.request.scope == framework::ZeroUuid,
          "XML-RPC defaults");
    handlers.llsd_login(input, "::1");
    check(recording.request.version.empty() && recording.request.remote_ip == "::1", "LLSD defaults and IPv6 peer");
    input.map.erase("passwd");
    input.map["web_login_key"] = Value("12345678-1234-1234-1234-123456789012");
    handlers.xml_rpc_login(input, "127.0.0.1");
    check(recording.request.password == "$1$12345678-1234-1234-1234-123456789012",
          "web key translated to legacy credential prefix");
    input.map["passwd"] = Value();
    const auto before = recording.calls;
    check(handlers.xml_rpc_login(input, "127.0.0.1").get("message").text ==
                  services::login_failure("IncompleteXMLRPC").get("message").text &&
              recording.calls == before,
          "explicit null password does not fall back to web key");
    check(handlers.llsd_login(Value::object({}), "::1").get("message").text ==
              services::login_failure("InvalidLLSD").get("message").text,
          "LLSD missing credential failure");
}

void config_tests() {
    auto path = std::filesystem::temp_directory_path() / ("opensim-native-config-" + framework::random_uuid() + ".ini");

    struct Remove {
        std::filesystem::path path;

        ~Remove() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } remove{path};

    const std::vector<std::pair<std::string, std::string>> cases = {
        {"URI=file:accounts.db,version=3", "accounts.db,version=3"},
        {"URI=FILE://accounts.db;Version=3", "accounts.db"},
        {"Data Source=accounts.db;URI=file:wrong.db", "accounts.db"},
        {"DataSource=accounts.db;Data_Source=wrong.db", "accounts.db"},
        {"Data Source='semi\\;colon.db';Pooling=False", "semi;colon.db"},
        {"URI=file::MEMORY:", ":memory:"}};
    for (auto &[connection, expected] : cases) {
        {
            std::ofstream file(path);
            file << "[AuthenticationService]\nConnectionString = \"" << connection << "\"\n";
        }
        auto config = server::IniConfig::load(path);
        const auto actual = server::database_path(config, "AuthenticationService", "AuthenticationDatabase");
        check(actual ==
                  (expected == ":memory:" ? expected : (path.parent_path() / expected).lexically_normal().string()),
              "connection parsing and configuration-relative database path");
    }
    {
        std::ofstream file(path);
        file << "[NativeServer]\nAuthenticationDatabase = native-auth.db\n";
    }
    auto config = server::IniConfig::load(path);
    check(std::filesystem::path(server::database_path(config, "AuthenticationService", "AuthenticationDatabase")) ==
              path.parent_path() / "native-auth.db",
          "native plain-path fallback");
    {
        std::ofstream file(path);
        file << "[AuthenticationService]\nConnectionString = \"URI=file:a;URI=file:b\"\n";
    }
    config = server::IniConfig::load(path);
    check(throws([&] { server::database_path(config, "AuthenticationService", "AuthenticationDatabase"); }),
          "duplicate connection properties fail before database selection");
}

void source_authentication_tests() {
    auto path = std::filesystem::temp_directory_path() / ("opensim-native-auth-" + framework::random_uuid() + ".db");

    struct Remove {
        std::filesystem::path path;

        ~Remove() {
            std::error_code ec;
            std::filesystem::remove(path, ec);
        }
    } remove{path};

    const auto file = path.string();
    std::string principal;
    {
        services::AccountStore accounts(file, file, true, true);
        principal = accounts.create_user("Source", "User", "secret", framework::ZeroUuid);
    }
    auto sql = [&](const std::string &statement) {
        sqlite3 *db = nullptr;
        if (sqlite3_open(file.c_str(), &db) != SQLITE_OK)
            throw std::runtime_error("observer open");
        char *error = nullptr;
        auto rc = sqlite3_exec(db, statement.c_str(), nullptr, nullptr, &error);
        std::string message = error ? error : "";
        sqlite3_free(error);
        sqlite3_close(db);
        if (rc != SQLITE_OK)
            throw std::runtime_error(message);
    };
    sql("ALTER TABLE auth RENAME TO AuthRealm");
    services::AccountStore accounts(file, file, false, true, "UserAccounts", "AuthRealm");
    auto token = accounts.authenticate(principal, framework::md5("secret"), 30);
    check(token && accounts.verify(principal, token.token, 30),
          "source authentication facade issues and verifies tokens");
    check(accounts.release(principal, token.token) && !accounts.verify(principal, token.token, 30),
          "source authentication facade releases tokens");
    auto info = accounts.get_auth_info(framework::Uuid(principal));
    check(info && info->principal.str() == principal && info->account_type == "UserAccount",
          "source GetAuthInfo reads original authentication fields");
    info->account_type = "CustomInfo";
    check(accounts.set_auth_info(info) &&
              accounts.get_auth_info(framework::Uuid(principal))->account_type == "CustomInfo",
          "source SetAuthInfo persists supplied fields");
    check(!accounts.get_auth_info(framework::Uuid::random()),
          "source GetAuthInfo returns null for missing authentication");
    check(throws([&] { accounts.set_auth_info({}); }), "source SetAuthInfo rejects null info before store");
    auto independent = std::make_shared<services::AuthInfo>(*info);
    independent->principal = framework::Uuid::random();
    check(accounts.set_auth_info(independent) &&
              accounts.get_auth_info(independent->principal)->account_type == "CustomInfo",
          "service SetAuthInfo can create credentials independently of an account record");
    sql("DELETE FROM AuthRealm WHERE UUID='" + independent->principal.str() + "'");
    auto key = framework::random_uuid();
    sql("UPDATE AuthRealm SET webLoginKey='" + key + "',accountType='Custom'");
    check(accounts.set_password(principal, std::string("changed")), "source SetPassword updates configured realm");
    check(static_cast<bool>(accounts.authenticate(principal, framework::md5("changed"), 30)),
          "source password replacement authenticates");
    // The original web-key service consumes its key before token issuance;
    // a token-storage failure must not roll back that preceding write.
    sql("DROP TABLE tokens");
    check(throws([&] { accounts.authenticate(principal, key, 30); }), "source web-key token failure propagates");
    check(!accounts.authenticate(principal, key, 30), "failed token issuance still consumes web key");
    sql("CREATE TABLE tokens(UUID,token,validity DATETIME NOT NULL)");
    check(static_cast<bool>(accounts.authenticate(principal, framework::md5("changed"), 30)),
          "authentication recovers after token storage repair");
    check(throws([&] { accounts.authenticate(principal, framework::md5("changed"), -1); }),
          "source negative lifetime preserves SQLite null-validity failure");
}

void store_tests() {
    services::AccountStore accounts(":memory:", ":memory:", true);
    auto principal = accounts.create_user("Test", "User", "secret", framework::ZeroUuid);
    check(accounts.find(framework::ZeroUuid, "Test", "User")->principal_id == principal, "account retrieval");
    check(!accounts.find(framework::ZeroUuid, "test", "User"),
          "SQLite name comparison matches source case sensitivity");
    check(throws([&] { accounts.create_user("Test", "User", "other", framework::ZeroUuid); }),
          "duplicate account creation rolls back");
    check(!accounts.authenticate(principal, framework::md5("wrong"), 30), "wrong password rejected");
    auto authentication = accounts.authenticate(principal, framework::md5("secret"), 30);
    check(static_cast<bool>(authentication) && accounts.verify(principal, authentication.token, 30),
          "salted authentication and token refresh");
    check(!accounts.verify(principal, framework::random_uuid(), 30), "unknown token rejected");
    check(accounts.release(principal, authentication.token) && !accounts.verify(principal, authentication.token, 30),
          "token release");
    auto scope = framework::random_uuid();
    auto scoped = accounts.create_user("Scoped", "User", "secret", scope);
    check(accounts.find(scope, "Scoped", "User")->principal_id == scoped, "scoped account lookup");
    check(!accounts.find(framework::random_uuid(), "Scoped", "User"), "other scope cannot retrieve account");
    check(accounts.find(framework::random_uuid(), "Test", "User")->principal_id == principal,
          "unscoped account fallback");
    server::IniConfig defaults;
    services::LoginService service(accounts, defaults);
    services::LoginRequest request{"Test", "User", "secret",  "last",    std::string(framework::ZeroUuid),
                                   "1",    "Test", "Unknown", "Unknown", "127.0.0.1"};
    check(service.login(request).get("message").text == services::login_failure("InventoryProblem").get("message").text,
          "authenticated account respects required inventory default");
    request.password = "$1$" + framework::md5("secret");
    check(service.login(request).get("message").text == services::login_failure("InventoryProblem").get("message").text,
          "viewer MD5-prefixed password");
    request.password = "wrong";
    check(service.login(request).get("message").text == services::login_failure("UserProblem").get("message").text,
          "service authentication failure");
    check(service.set_level("Test", "User", framework::md5("secret"), 200).get("success").text == "false",
          "remote set level disabled by default");
    RecordingLogin recording;
    services::NullRegionStore statistics_regions;
    services::GridService statistics_grid(statistics_regions, defaults);
    StatisticsUsers statistics_users;
    const auto resident = std::make_shared<services::GridUserData>();
    resident->user_id = "local";
    const auto missing_id = std::make_shared<services::GridUserData>();
    const auto missing_fields = std::make_shared<services::GridUserData>();
    missing_fields->user_id = "local-without-fields";
    missing_fields->data.reset();
    for (const auto &[incomplete, expected_residents] :
         std::vector<std::pair<std::shared_ptr<services::GridUserData>, int>>{
             {nullptr, 1}, {missing_id, 1}, {missing_fields, 2}}) {
        statistics_users.rows = {resident, incomplete, resident};
        server::LoginApplication statistics_app(recording, defaults);
        statistics_app.set_grid_statistics(&statistics_grid, &statistics_users);
        const auto statistics = statistics_app.handle({"GET", "/get_grid_stats"});
        check(statistics.status == 200 && statistics.body == "<gridstats><residents>" +
                                                                 std::to_string(expected_residents) +
                                                                 "</residents><active_users>0</active_users>"
                                                                 "<region_count>0</region_count></gridstats>",
              "grid statistics stop at an incomplete record and publish partial counts");
    }
    server::LoginApplication app(recording, defaults);
    app.set_default_login_uri("http://localhost:9000/");
    check(app.handle({"GET", "/health"}).status == 200 && app.handle({"GET", "/ready"}).status == 503,
          "liveness differs from simulator readiness");
    check(app.handle({"GET", "/"}).status == 405 && app.handle({"GET", "/missing"}).status == 404,
          "HTTP method and path routing");
    const auto credentials = Value::object({{"first", "Test"}, {"last", "User"}, {"passwd", "secret"}});
    network::HttpRequest forwarded{"POST", "/", network::xml_rpc_request({"login_to_simulator", {credentials}}),
                                   "192.0.2.9", "text/xml"};
    for (const auto &pair :
         std::vector<std::pair<std::string, std::string>>{{"203.0.113.7, 198.51.100.1", "203.0.113.7"},
                                                          {"::1, 198.51.100.1", "::1"},
                                                          {"127.1", "127.0.0.1"},
                                                          {"invalid,203.0.113.7", "192.0.2.9"},
                                                          {" 203.0.113.7", "192.0.2.9"},
                                                          {"", "192.0.2.9"}}) {
        forwarded.headers["x-forwarded-for"] = pair.first;
        app.handle(forwarded);
        check(recording.request.remote_ip == pair.second, "XML-RPC first forwarded address or peer fallback");
    }
    forwarded.content_type = "application/llsd+xml";
    forwarded.body = network::llsd_xml(credentials);
    forwarded.headers["x-forwarded-for"] = "203.0.113.7";
    app.handle(forwarded);
    check(recording.request.remote_ip == "192.0.2.9", "LLSD preserves transport peer despite XFF header");
}
} // namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char **argv) {
#ifdef OPENSIM_TEST_TLS
    {
        const std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
        network::HttpServerOptions options;
        const auto fixtures = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("fixtures");
        options.certificate = (fixtures / "localhost-cert.pem").string();
        options.private_key = (fixtures / "localhost-key.pem").string();
        const bool loaded = context && network::load_http_pem_certificate(context.get(), options);
        check(loaded, "native TLS loads the configured PEM certificate and matching key");
        if (loaded) {
            const auto *certificate = SSL_CTX_get0_certificate(context.get());
            const std::unique_ptr<BIO, decltype(&BIO_free)> input(BIO_new_file(options.certificate.c_str(), "r"),
                                                                  BIO_free);
            const std::unique_ptr<X509, decltype(&X509_free)> expected(
                input ? PEM_read_bio_X509(input.get(), nullptr, nullptr, nullptr) : nullptr, X509_free);
            check(expected && certificate && X509_cmp(certificate, expected.get()) == 0,
                  "native TLS context presents the exact configured leaf certificate");
            const std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> client_context(SSL_CTX_new(TLS_client_method()),
                                                                                   SSL_CTX_free);
            const bool trusted = expected && client_context &&
                                 X509_STORE_add_cert(SSL_CTX_get_cert_store(client_context.get()), expected.get()) == 1;
            check(trusted, "test client trusts the fixture certificate");
            if (trusted) {
                SSL_CTX_set_verify(client_context.get(), SSL_VERIFY_PEER, nullptr);
                const std::unique_ptr<SSL, decltype(&SSL_free)> client(SSL_new(client_context.get()), SSL_free);
                const std::unique_ptr<SSL, decltype(&SSL_free)> server(SSL_new(context.get()), SSL_free);
                BIO *client_io = nullptr;
                BIO *server_io = nullptr;
                if (client && server && BIO_new_bio_pair(&client_io, 0, &server_io, 0) == 1) {
                    SSL_set_bio(client.get(), client_io, client_io);
                    SSL_set_bio(server.get(), server_io, server_io);
                    SSL_set_connect_state(client.get());
                    SSL_set_accept_state(server.get());
                    const bool hostname_set = SSL_set1_host(client.get(), "localhost") == 1;
                    const auto advance = [](SSL *peer) {
                        const int result = SSL_do_handshake(peer);
                        if (result == 1)
                            return true;
                        const int error = SSL_get_error(peer, result);
                        return error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE;
                    };
                    for (int attempt = 0; hostname_set && attempt < 32; ++attempt) {
                        if (!advance(client.get()) || !advance(server.get()))
                            break;
                        if (SSL_is_init_finished(client.get()) && SSL_is_init_finished(server.get()))
                            break;
                    }
                    check(SSL_is_init_finished(client.get()) && SSL_is_init_finished(server.get()) &&
                              SSL_get_verify_result(client.get()) == X509_V_OK,
                          "native TLS completes a verified localhost handshake");
                } else {
                    check(false, "create in-memory TLS peers");
                }
            }
        }
    }
#endif
    try {
        identity_tests();
        codec_tests();
        handler_tests();
        config_tests();
        store_tests();
        source_authentication_tests();
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    if (failures)
        return 1;
    std::cout << "Native identity, protocol, login and persistence checks passed\n";
}
