"""Verify SMTP TLS modes and certificate policy against a loopback relay."""

import base64
import socket
import ssl
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from email import policy
from email.parser import BytesParser
from pathlib import Path


def relay(listener, context, accept_message, mode):
    implicit_tls = mode == "implicit"
    legacy = mode in ("legacy", "required-legacy")
    eight_bit = mode == "eight-bit"
    connection, _ = listener.accept()
    with connection:
        connection.settimeout(10)
        if not implicit_tls:
            connection.sendall(b"220 SMTP fixture\r\n")
            with connection.makefile("rb") as commands:
                assert commands.readline() == b"EHLO localhost\r\n"
                if legacy:
                    connection.sendall(b"502 EHLO unavailable\r\n")
                    assert commands.readline() == b"HELO localhost\r\n"
                    connection.sendall(b"250\r\n")
                else:
                    connection.sendall(b"250-fixture\r\n250 STARTTLS\r\n")
                    assert commands.readline() == b"STARTTLS\r\n"
            if not legacy:
                connection.sendall(b"220 begin TLS\r\n")
        try:
            transport = connection if legacy else context.wrap_socket(connection, server_side=True)
        except (ssl.SSLError, ConnectionError):
            assert not accept_message
            return
        with transport, transport.makefile("rb") as commands:
            try:
                if implicit_tls:
                    transport.sendall(b"220 SMTP fixture\r\n")
                first_command = commands.readline()
            except ConnectionError:
                assert not accept_message
                return
            if not accept_message:
                assert first_command == b"", first_command
                return
            if not legacy:
                assert first_command == b"EHLO localhost\r\n"
                capabilities = b"250-fixture\r\n"
                if eight_bit:
                    capabilities += b"250-8BITMIME\r\n"
                transport.sendall(capabilities + b"250 AUTH PLAIN\r\n")
                expected = base64.b64encode(b"\0fixture-user\0fixture-password")
                assert commands.readline() == b"AUTH PLAIN " + expected + b"\r\n"
                transport.sendall(b"235 authenticated\r\n")
                first_command = commands.readline()
            for command, response in (
                (b"MAIL FROM:<from@example.invalid>" + (b" BODY=8BITMIME" if eight_bit else b"") + b"\r\n",
                 b"250 sender accepted\r\n"),
                (b"RCPT TO:<to@example.invalid>\r\n", b"250 recipient accepted\r\n"),
                (b"DATA\r\n", b"354 send data\r\n"),
            ):
                assert first_command == command
                transport.sendall(response)
                if command != b"DATA\r\n":
                    first_command = commands.readline()
            data = bytearray()
            for line in commands:
                if line == b".\r\n":
                    message = BytesParser(policy=policy.default).parsebytes(data)
                    assert message["From"].addresses[0].display_name == "Simulátor, Native"
                    assert message["To"].addresses[0].display_name == "Fixture Recipient"
                    assert message["Date"].datetime.utcoffset().total_seconds() == 0
                    assert str(message["Message-ID"]).endswith("@example.invalid>")
                    encoding = message["Content-Transfer-Encoding"]
                    if eight_bit:
                        assert encoding == "8bit"
                        assert data.endswith("\r\n\r\nfirst\r\n..café\r\nlast\r\n".encode())
                    elif mode == "encoded":
                        assert encoding == "base64"
                        assert message.get_payload(decode=True) == "first\r\n.café\r\nlast\r\n".encode()
                    else:
                        assert encoding is None
                        assert data.endswith(b"\r\n\r\nfirst\r\n..second\r\nlast\r\n")
                    transport.sendall(b"250 queued\r\n")
                    assert commands.readline() == b"QUIT\r\n"
                    return
                data.extend(line)
            raise AssertionError("SMTP DATA terminator missing")


def main():
    fixtures = Path(__file__).resolve().parent / "fixtures"
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "localhost-cert.pem", fixtures / "localhost-key.pem")
    cases = (
        ("127.0.0.1", 1, 1, 0, "SMTP TLS handshake failed"),
        ("127.0.0.1", 1, 0, 1, None),
        ("127.0.0.1", 0, 0, 1, None),
        ("127.0.0.1", 0, 1, 1, "SMTP TLS handshake failed"),
        ("::ffff:127.0.0.1", 1, 0, 1, "SMTP TLS server name verification failed"),
        ("127.0.0.1", "implicit", 0, 1, None),
        ("127.0.0.1", "implicit", 1, 1, "SMTP TLS handshake failed"),
        ("127.0.0.1", "legacy", 1, 1, None),
        ("127.0.0.1", "required-legacy", 1, 1, "SMTP server does not advertise STARTTLS"),
        ("127.0.0.1", "eight-bit", 0, 1, None),
        ("127.0.0.1", "encoded", 0, 1, None),
    )
    for host, mode, verify_chain, verify_name, expected_error in cases:
        accept_message = expected_error is None
        with socket.socket() as listener, ThreadPoolExecutor(max_workers=1) as worker:
            listener.bind(("127.0.0.1", 0))
            listener.listen()
            listener.settimeout(10)
            relay_result = worker.submit(relay, listener, context, accept_message, mode)
            result = subprocess.run(
                [sys.argv[1], "--smtp-probe", host, str(listener.getsockname()[1]),
                 str(mode), str(verify_chain), str(verify_name)],
                capture_output=True, text=True, timeout=15,
            )
            relay_result.result(timeout=15)
            assert (result.returncode == 0) == accept_message, result.stderr
            if not accept_message:
                assert expected_error in result.stderr, result.stderr


if __name__ == "__main__":
    main()
