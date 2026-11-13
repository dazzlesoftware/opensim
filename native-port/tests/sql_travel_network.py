"""Exercise home-agent travel persistence against an isolated SQL database."""

import gzip
import http.server
import json
import subprocess
import threading
import urllib.request
import uuid
import xmlrpc.client

from asset_endpoint_network import server


class Gatekeeper(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        body = self.rfile.read(int(self.headers["Content-Length"]))
        if body.startswith(b"\x1f\x8b"):
            body = gzip.decompress(body)
        self.server.received = json.loads(body)
        response = json.dumps({"success": self.server.accept, "reason": "fixture denial"}).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def log_message(self, *args):
        pass


def launch_home_agent(url, gate, agent, session, accepted):
    gate.accept = accepted
    gate.received = None
    destination = f"http://127.0.0.1:{gate.server_port}/"
    payload = {
        "agent_id": agent, "session_id": session, "secure_session_id": str(uuid.uuid4()),
        "circuit_code": "123", "first_name": "Travel", "last_name": "Fixture",
        "caps_path": "travel-fixture", "client_ip": "127.0.0.1", "start_pos": "<128,128,25>",
        "gatekeeper_serveruri": destination, "destination_serveruri": destination,
        "destination_uuid": str(uuid.uuid4()),
    }
    request = urllib.request.Request(
        f"{url}/homeagent/{agent}/", json.dumps(payload).encode(),
        {"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(request, timeout=10) as response:
        reply = json.load(response)
    assert reply.get("success", False) is accepted, reply
    assert gate.received and gate.received["session_id"] == session, gate.received
    return gate.received["service_session_id"]


def check_travel(native, work, query, port, provider):
    work = work / "travel"
    work.mkdir()
    config = work / "travel.ini"
    username = "postgres" if provider == "PGSQL" else "root"
    config.write_text(
        "[NativeServer]\nListenAddress=127.0.0.1\n"
        "[Network]\nhttp_listener_port=0\n"
        "[UserAgentService]\n"
        "LocalServiceModule=OpenSim.Services.HypergridService.dll:UserAgentService\n"
        f"StorageProvider=OpenSim.Data.{provider}.dll\n"
        f'ConnectionString="Server=127.0.0.1;Port={port};Database=travel;User ID={username};Password=;"\n',
        encoding="utf-8",
    )
    command = [native, "--config", str(config)]
    subprocess.run([*command, "--init-db"], check=True, timeout=30)
    created = subprocess.run(
        [*command, "--create-user", "Travel", "Fixture"],
        input="fixture-password\n", text=True, capture_output=True, check=True, timeout=30,
    )
    agent = created.stdout.rsplit("Created user ", 1)[1].strip()
    with server(native, config):
        pass

    expired_session = str(uuid.uuid4())
    delimiter = '"' if provider == "PGSQL" else chr(96)
    columns = ",".join(
        f"{delimiter}{column}{delimiter}" for column in ("SessionID", "UserID", "ServiceToken", "TMStamp")
    )
    query("travel", f"INSERT INTO hg_traveling_data ({columns}) VALUES "
                    f"('{expired_session}','{agent}','expired-token','2000-01-01')")

    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Gatekeeper) as gate:
        thread = threading.Thread(target=gate.serve_forever, daemon=True)
        thread.start()
        try:
            destination = f"http://127.0.0.1:{gate.server_port}/"
            with server(native, config) as native_port:
                url = f"http://127.0.0.1:{native_port}"

                def launch(session, accepted):
                    return launch_home_agent(url, gate, agent, session, accepted)

                first, second = (str(uuid.uuid4()) for _ in range(2))
                with xmlrpc.client.ServerProxy(url + "/") as rpc:
                    def verify(session, token, expected):
                        result = rpc.verify_agent({"sessionID": session, "token": token})
                        assert result == {"result": expected}, result

                    verify(expired_session, "expired-token", "False")
                    original = launch(first, True)
                    verify(first, original, "True")
                    verify(second, launch(second, False), "False")
                    verify(first, original, "True")
                    verify(first, launch(first, False), "False")
                    verify(first, original, "True")
                    replacement = launch(first, True)
                    verify(first, original, "False")
                    concurrent = launch(second, True)
                    verify(first, replacement, "True")
                    verify(second, concurrent, "True")
                    assert rpc.locate_user({"userID": agent}) == {"URL": destination}
                    assert rpc.logout_agent({"userID": agent, "sessionID": first}) == {"result": "true"}
                    verify(first, replacement, "False")
                    verify(second, concurrent, "True")
        finally:
            gate.shutdown()
            thread.join()
    assert query("travel", "SELECT COUNT(*) FROM hg_traveling_data") == "1"
    with server(native, config) as native_port:
        with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{native_port}/") as rpc:
            assert rpc.verify_agent({"sessionID": second, "token": concurrent}) == {"result": "True"}
    print(f"PASS: {provider} travel launch, rollback, multiple sessions, expiration, logout, and restart")
