// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
#include "opensim/network/script_smtp.hpp"
#include "opensim/framework/uuid.hpp"
#include <httplib.h>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace opensim::network {
namespace {
struct MailboxAddress {
    std::string_view address;
    std::string name;
};

std::string_view trim_mailbox(std::string_view value) {
    const auto begin = value.find_first_not_of(" \t");
    return begin == value.npos ? std::string_view{} :
        value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
}

MailboxAddress parse_mailbox(std::string_view value) {
    if (value.find_first_of("\r\n") != value.npos)
        throw std::invalid_argument("Invalid SMTP envelope address");
    value = trim_mailbox(value);
    MailboxAddress mailbox{value, {}};
    if (value.ends_with('>')) {
        const auto angle = value.rfind('<');
        if (angle == value.npos)
            throw std::invalid_argument("Invalid SMTP envelope address");
        mailbox.address = trim_mailbox(value.substr(angle + 1, value.size() - angle - 2));
        auto name = trim_mailbox(value.substr(0, angle));
        if (name.size() >= 2 && name.front() == '"' && name.back() == '"') {
            name.remove_prefix(1);
            name.remove_suffix(1);
            for (std::size_t i = 0; i < name.size(); ++i) {
                if (name[i] == '\\' && i + 1 < name.size())
                    ++i;
                mailbox.name += name[i];
            }
        } else {
            mailbox.name = name;
        }
    }
    const auto separator = mailbox.address.find('@');
    if (separator == value.npos || separator == 0 || separator + 1 == mailbox.address.size() ||
        mailbox.address.find('@', separator + 1) != value.npos ||
        !std::ranges::all_of(mailbox.address, [](unsigned char c) {
            return c > 32 && c < 127 && c != '<' && c != '>';
        }))
        throw std::invalid_argument("Invalid SMTP envelope address");
    return mailbox;
}

std::string encode_header(std::string_view value) {
    std::string encoded;
    while (!value.empty()) {
        auto count = std::min<std::size_t>(42, value.size());
        while (count != 0 && count < value.size() && (static_cast<unsigned char>(value[count]) & 0xc0) == 0x80)
            --count;
        if (count == 0)
            throw std::invalid_argument("Invalid UTF-8 in SMTP header");
        if (!encoded.empty())
            encoded += "\r\n ";
        encoded += "=?UTF-8?B?" + httplib::detail::base64_encode(std::string(value.substr(0, count))) + "?=";
        value.remove_prefix(count);
    }
    return encoded;
}

std::string mailbox_header(const MailboxAddress& mailbox) {
    auto name = encode_header(mailbox.name);
    if (!name.empty())
        name += ' ';
    return name + "<" + std::string(mailbox.address) + ">";
}

enum class BodyEncoding { SevenBit, EightBit, Base64 };

std::string smtp_body(std::string_view body, bool supports_eight_bit, BodyEncoding& encoding) {
    std::string canonical;
    std::size_t line_length = 0;
    encoding = BodyEncoding::SevenBit;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const auto c = static_cast<unsigned char>(body[i]);
        if (c == '\r' || c == '\n') {
            if (c == '\r' && i + 1 < body.size() && body[i + 1] == '\n')
                ++i;
            canonical += "\r\n";
            line_length = 0;
        } else {
            ++line_length;
            if ((c < 32 && c != '\t') || c == 127 || line_length > 998 ||
                (c >= 128 && !supports_eight_bit)) {
                encoding = BodyEncoding::Base64;
            } else if (c >= 128 && encoding == BodyEncoding::SevenBit) {
                encoding = BodyEncoding::EightBit;
            }
            canonical += static_cast<char>(c);
        }
    }
    if (!canonical.empty() && !canonical.ends_with("\r\n"))
        canonical += "\r\n";
    if (encoding == BodyEncoding::Base64)
        return canonical;

    std::string stuffed;
    bool line_start = true;
    for (const auto c : canonical) {
        if (line_start && c == '.')
            stuffed += '.';
        stuffed += c;
        line_start = c == '\n';
    }
    return stuffed;
}
}

void send_script_smtp(const std::string& host, int port, std::string_view from, std::string_view to,
                      std::string_view subject, std::string_view body,
                      std::string_view owner, std::string_view object,
                      const ScriptSmtpSecurity& security) {
    const auto sender = parse_mailbox(from);
    const auto recipient = parse_mailbox(to);
    if (owner.find_first_of("\r\n") != owner.npos || object.find_first_of("\r\n") != object.npos)
        throw std::invalid_argument("Invalid SMTP envelope address");
    const auto now = std::time(nullptr);
    std::tm utc{};
#ifdef _WIN32
    if (gmtime_s(&utc, &now) != 0)
#else
    if (!gmtime_r(&now, &utc))
#endif
        throw std::runtime_error("Could not format the SMTP message date");
    std::ostringstream date;
    date.imbue(std::locale::classic());
    date << std::put_time(&utc, "%a, %d %b %Y %H:%M:%S +0000");
    const auto message_id = framework::Uuid::random().str() + "@" +
        std::string(sender.address.substr(sender.address.find('@') + 1));

    httplib::Error error = httplib::Error::Success;
    const auto socket = httplib::detail::create_client_socket(
        host, "", port, AF_UNSPEC, true, false, nullptr, 10, 0, 10, 0, 10, 0, "", error);
    if (socket == INVALID_SOCKET)
        throw std::runtime_error("Could not connect to SMTP server");
    struct Connection {
        socket_t socket;
        ~Connection() { httplib::detail::close_socket(socket); }
    } connection{socket};
    httplib::detail::SocketStream plain_stream(socket, 10, 0, 10, 0);
    plain_stream.opensim_disable_read_buffer();
    httplib::Stream* stream = &plain_stream;
    const auto write = [&](std::string_view data) {
        while (!data.empty()) {
            const auto size = stream->write(data.data(), data.size());
            if (size <= 0)
                throw std::runtime_error("SMTP write failed");
            data.remove_prefix(static_cast<std::size_t>(size));
        }
    };
    struct Reply {
        std::string code;
        std::vector<std::string> lines;
    };
    const auto read_reply = [&] {
        Reply reply;
        for (int lines = 0; lines < 100; ++lines) {
            std::string line;
            char character;
            while (line.size() < 2048 && stream->read(&character, 1) == 1) {
                line += character;
                if (character == '\n')
                    break;
            }
            if (line.size() < 5 || !line.ends_with("\r\n") ||
                !std::ranges::all_of(std::string_view(line).substr(0, 3), [](char c) {
                    return c >= '0' && c <= '9';
                }))
                break;
            const auto code = line.substr(0, 3);
            if (!reply.code.empty() && reply.code != code)
                break;
            reply.code = code;
            if (line.size() == 5)
                return reply;
            reply.lines.push_back(line.substr(4, line.size() - 6));
            if (line[3] == ' ')
                return reply;
            if (line[3] != '-')
                break;
        }
        throw std::runtime_error("Invalid SMTP multiline reply");
    };
    const auto expect = [&](std::string_view code, std::string_view alternate = {}) {
        const auto reply = read_reply();
        if (reply.code != code && reply.code != alternate)
            throw std::runtime_error("SMTP server rejected a command with status " + reply.code);
    };
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    namespace tls = httplib::tls;
    std::unique_ptr<void, decltype(&tls::free_context)> context(nullptr, tls::free_context);
    std::unique_ptr<void, decltype(&tls::free_session)> session(nullptr, tls::free_session);
    std::unique_ptr<httplib::detail::SSLSocketStream> encrypted;
    const auto upgrade = [&] {
        context.reset(tls::create_client_context());
        if (!context || (security.verify_chain && !tls::load_system_certs(context.get())))
            throw std::runtime_error("Could not initialize SMTP TLS trust");
        tls::set_verify_client(context.get(), security.verify_chain);
        session.reset(tls::create_session(context.get(), socket));
        if (!session || (!httplib::detail::is_ip_address(host) &&
                         !tls::set_sni(session.get(), host.c_str(), false)))
            throw std::runtime_error("Could not initialize SMTP TLS session");
        tls::TlsError tls_error;
        if (!tls::connect_nonblocking(session.get(), socket, 10, 0, &tls_error))
            throw std::runtime_error("SMTP TLS handshake failed");
        std::unique_ptr<void, decltype(&tls::free_cert)> certificate(
            tls::get_peer_cert(session.get()), tls::free_cert);
        if (!certificate || (security.verify_name && !tls::verify_hostname(certificate.get(), host.c_str())))
            throw std::runtime_error("SMTP TLS server name verification failed");
        encrypted = std::make_unique<httplib::detail::SSLSocketStream>(socket, session.get(), 10, 0, 10, 0);
        stream = encrypted.get();
    };
#else
    const auto upgrade = [] { throw std::runtime_error("SMTP TLS requires a TLS-enabled build"); };
#endif
    const bool implicit_tls = security.tls == ScriptSmtpTls::Implicit;
    if (implicit_tls)
        upgrade();
    expect("220");
    const auto ehlo = [&] {
        write("EHLO localhost\r\n");
        auto reply = read_reply();
        if (reply.code != "250") {
            write("HELO localhost\r\n");
            expect("250");
            reply.lines.clear();
        }
        return reply.lines;
    };
    auto capabilities = ehlo();
    if (!implicit_tls) {
        const bool offers_start_tls = std::ranges::any_of(capabilities, [](const auto& capability) {
            return httplib::detail::case_ignore::equal(capability, "STARTTLS");
        });
        if (security.tls == ScriptSmtpTls::StartTls && !offers_start_tls)
            throw std::runtime_error("SMTP server does not advertise STARTTLS");
        if (offers_start_tls) {
            write("STARTTLS\r\n");
            expect("220");
            upgrade();
            capabilities = ehlo();
        }
    }
    if (!security.login.empty() && !security.password.empty()) {
        bool plain = false;
        bool login = false;
        for (const auto& capability : capabilities) {
            std::istringstream words(capability);
            std::string word;
            if (!(words >> word) || !httplib::detail::case_ignore::equal(word, "AUTH"))
                continue;
            while (words >> word) {
                plain |= httplib::detail::case_ignore::equal(word, "PLAIN");
                login |= httplib::detail::case_ignore::equal(word, "LOGIN");
            }
        }
        if (plain) {
            const auto credentials = '\0' + security.login + '\0' + security.password;
            write("AUTH PLAIN " + httplib::detail::base64_encode(credentials) + "\r\n");
        } else if (login) {
            write("AUTH LOGIN\r\n");
            expect("334");
            write(httplib::detail::base64_encode(security.login) + "\r\n");
            expect("334");
            write(httplib::detail::base64_encode(security.password) + "\r\n");
        } else {
            throw std::runtime_error("SMTP server does not advertise a supported authentication mechanism");
        }
        expect("235");
    }
    const bool supports_eight_bit = std::ranges::any_of(capabilities, [](const auto& capability) {
        return httplib::detail::case_ignore::equal(capability, "8BITMIME");
    });
    BodyEncoding body_encoding;
    const auto wire_body = smtp_body(body, supports_eight_bit, body_encoding);
    const auto body_parameter = body_encoding == BodyEncoding::EightBit ? " BODY=8BITMIME" : "";
    write("MAIL FROM:<" + std::string(sender.address) + ">" + body_parameter + "\r\n");
    expect("250");
    write("RCPT TO:<" + std::string(recipient.address) + ">\r\n");
    expect("250", "251");
    write("DATA\r\n");
    expect("354");
    std::string data = "From: " + mailbox_header(sender) + "\r\nTo: " + mailbox_header(recipient) +
        "\r\nDate: " + date.str() + "\r\nMessage-ID: <" + message_id + ">\r\nSubject: " + encode_header(subject);
    data += "\r\nX-Owner-ID: " + std::string(owner) + "\r\nX-Task-ID: " + std::string(object) +
        "\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=utf-8\r\n";
    if (body_encoding == BodyEncoding::EightBit)
        data += "Content-Transfer-Encoding: 8bit\r\n";
    if (body_encoding != BodyEncoding::Base64) {
        data += "\r\n" + wire_body;
    } else {
        data += "Content-Transfer-Encoding: base64\r\n\r\n";
        const auto encoded = httplib::detail::base64_encode(wire_body);
        for (std::size_t offset = 0; offset < encoded.size(); offset += 76)
            data += encoded.substr(offset, 76) + "\r\n";
    }
    write(data + ".\r\n");
    expect("250");
    write("QUIT\r\n");
}
}
