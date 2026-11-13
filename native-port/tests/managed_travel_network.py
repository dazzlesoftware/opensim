"""Round-trip a native SQLite travel record through the original managed provider."""

import argparse
import http.server
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import threading
import uuid
import xml.etree.ElementTree as ET
import xmlrpc.client

from asset_endpoint_network import server
from sql_travel_network import Gatekeeper, launch_home_agent

ROOT = Path(__file__).resolve().parents[2]


def build_probe(dotnet, work):
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    settings = ET.SubElement(project, "PropertyGroup")
    ET.SubElement(settings, "TargetFramework").text = "net8.0"
    ET.SubElement(settings, "OutputType").text = "Exe"
    items = ET.SubElement(project, "ItemGroup")
    ET.SubElement(items, "Compile", Include=str(Path(__file__).with_name("managed_travel_reference.cs")))
    for assembly in ("OpenSim.Data.SQLite", "OpenSim.Data", "OpenMetaverseTypes"):
        reference = ET.SubElement(items, "Reference", Include=assembly)
        ET.SubElement(reference, "HintPath").text = str(ROOT / "bin" / f"{assembly}.dll")
    project_path = work / "TravelProbe.csproj"
    ET.ElementTree(project).write(project_path, encoding="utf-8")
    subprocess.run([dotnet, "build", str(project_path), "--nologo"], check=True, timeout=100)
    output = work / "bin" / "Debug" / "net8.0"
    arm = platform.machine().lower() in {"arm64", "aarch64"}
    library, filename = {
        "Windows": ("e_sqlite3.dll", "e_sqlite3.dll"),
        "Linux": ("libe_sqlite3-arm64.so" if arm else "libe_sqlite3.so", "libe_sqlite3.so"),
        "Darwin": ("libe_sqlite3_OSX_arm64.dylib" if arm else "libe_sqlite3_OSX_x64.dylib",
                   "libe_sqlite3.dylib"),
    }[platform.system()]
    shutil.copyfile(ROOT / "bin" / "lib64" / library, output / filename)
    return output / "TravelProbe.dll"


def check_round_trip(native, dotnet, work):
    probe = build_probe(dotnet, work)
    config = work / "native.ini"
    config.write_text(
        "[NativeServer]\nListenAddress=127.0.0.1\nTravelDatabase=travel.db\n"
        "[Network]\nhttp_listener_port=0\n",
        encoding="utf-8",
    )
    command = [native, "--config", str(config)]
    subprocess.run([*command, "--init-db"], check=True, timeout=30)
    created = subprocess.run(
        [*command, "--create-user", "Travel", "Fixture"], input="fixture-password\n",
        text=True, capture_output=True, check=True, timeout=30,
    )
    agent = created.stdout.rsplit("Created user ", 1)[1].strip()
    session = str(uuid.uuid4())
    with http.server.ThreadingHTTPServer(("127.0.0.1", 0), Gatekeeper) as gate:
        thread = threading.Thread(target=gate.serve_forever, daemon=True)
        thread.start()
        try:
            with server(native, config) as port:
                token = launch_home_agent(f"http://127.0.0.1:{port}", gate, agent, session, True)
        finally:
            gate.shutdown()
            thread.join()
    subprocess.run(
        [dotnet, str(probe), str(ROOT / "bin"), str(work / "travel.db"), session, agent, token],
        check=True, timeout=30,
    )
    with server(native, config) as port:
        with xmlrpc.client.ServerProxy(f"http://127.0.0.1:{port}/") as rpc:
            assert rpc.verify_agent({"sessionID": session, "token": token}) == {"result": "False"}
            assert rpc.verify_agent({"sessionID": session, "token": "managed-travel-token"}) == {"result": "True"}
    print("PASS: native SQLite travel record reopened and updated by the original .NET provider")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--dotnet", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="native-managed-travel-") as directory:
        check_round_trip(str(args.native.resolve()), args.dotnet, Path(directory))
