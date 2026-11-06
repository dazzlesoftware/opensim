#  * Copyright (c) Contributors, http://opensimulator.org/
#  * See CONTRIBUTORS.TXT for a full list of copyright holders.
#  *
#  * - Redistribution and use in source and binary forms, with or without
#  *   modification, are permitted provided that the following conditions are met:
#  *
#  * - Redistributions of source code must retain the above copyright notice, this
#  *   list of conditions and the following disclaimer.
#  * - Neither the name of the openmetaverse.org nor the names
#  *   of its contributors may be used to endorse or promote products derived from
#  *   this software without specific prior written permission.
#  *
#  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
#  * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
#  * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
#  * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
#  * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
#  * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
#  * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
#  * POSSIBILITY OF SUCH DAMAGE.
#  */
"""Compare the actual RegionURI parser with native parsing and state mutation."""

import argparse
import hashlib
import json
import random
from pathlib import Path
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--native", required=True)
    p.add_argument("--dotnet", required=True)
    p.add_argument("--work", required=True, type=Path)
    args = p.parse_args()
    work = args.work.resolve()
    work.mkdir(parents=True, exist_ok=True)
    source = "OpenSim/Framework/RegionURI.cs"
    project = ET.Element("Project", Sdk="Microsoft.NET.Sdk")
    props = ET.SubElement(project, "PropertyGroup")
    for name, value in {
        "TargetFramework": "net8.0",
        "OutputType": "Exe",
        "EnableDefaultCompileItems": "false",
        "Nullable": "disable",
    }.items():
        ET.SubElement(props, name).text = value
    items = ET.SubElement(project, "ItemGroup")
    for path in [ROOT / source, Path(__file__).parent / "region_uri_reference/Program.cs"]:
        ET.SubElement(items, "Compile", Include=str(path))
    for name in ["OpenMetaverse", "OpenMetaverseTypes"]:
        ref = ET.SubElement(items, "Reference", Include=name)
        ET.SubElement(ref, "HintPath").text = str(ROOT / "bin" / (name + ".dll"))
    path = work / "UriReference.csproj"
    ET.ElementTree(project).write(path, encoding="utf-8", xml_declaration=True)
    build = subprocess.run(
        [
            args.dotnet,
            "build",
            str(path),
            "-c",
            "Release",
            "-o",
            str(work / "app"),
            "--nologo",
            "-v",
            "quiet",
        ],
        capture_output=True,
        text=True,
    )
    if build.returncode:
        raise RuntimeError(build.stdout + build.stderr)
    values = [
        None,
        "",
        " ",
        "+",
        "Alpha",
        " Alpha ",
        "+++Alpha",
        "Alpha+Beta",
        "Alpha%20Beta",
        "Alpha%2BBeta",
        "Alpha%FF",
        "%E2%82%AC",
        "%ED%A0%80",
        ".alpha",
        "alpha.beta",
        "localhost",
        "https://localhost",
        "http://127.0.0.1/Alpha",
        "https://grid.example:8002/Alpha/1/2/-3",
        "hop://grid.example/Alpha",
        "hg|!!grid.example|8002|Alpha",
        "http://user+name@grid.example:9000/Alpha",
        "http://local.example/Alpha",
        "http://bad.invalid/Alpha",
        "Alpha/128/64/70",
        "Alpha(128, 64, -70)",
        "Alpha()",
        "Alpha(1)",
        "Alpha(1,)",
        "Alpha/1/",
        "Alpha/1/2/",
        "Alpha/1/2/-",
        "Alpha/1/2/+3",
        "Alpha/4294967296/4294967295/-2147483648",
        "%2FName",
        "(Alpha)",
        "Alpha;Bad",
        "http://grid.example:65536/Alpha",
        "http://grid.example:65537/Alpha",
        "http://grid.example:4294967295/Alpha",
        "http://grid.example:/Alpha",
        "http://grid.example/",
        "https://grid.example",
        "http://foo_bar.example/Alpha",
        "http://-bad.example/Alpha",
        "http://bad-.example/Alpha",
        "http://foo..example/Alpha",
    ]
    for scheme in [
        "http",
        "https",
        "hg",
        "hgs",
        "hop",
        "hops",
        "surl",
        "x-grid-info",
        "httpss",
        "bad",
        "a",
    ]:
        for separator in ["://", "|!!"]:
            values.append(scheme + separator + "grid.example/Alpha")
    for tail in [
        "1",
        "1/2",
        "1/2/3",
        "-1/2/3",
        "1/bad/3",
        "1junk/2tail/3more",
        "1/2/--1",
        "(1)",
        "( 1, +2, -3)",
        "(1,,3)",
    ]:
        values.append("Alpha/" + tail)
        values.append("Alpha" + tail)
    for host in [
        "_bad.example",
        "bad_.example",
        "bad-.example",
        "0.0.0.0",
        "127.1",
        "127.0.0.999",
        "1.2.3.4.5",
        "grid.example.",
        "a" * 63 + ".example",
        "a" * 64 + ".example",
        "\u00e9.example",
        "\u8a66\u9a13.example",
        "\u2000.example",
        "\u200b.example",
        "a\u2000.example",
        "a\u200b.example",
        "%FF.example",
        "%00.example",
        "a%2fb.example",
    ]:
        values.append("http://" + host + "/Alpha")
    values.extend(
        [
            "\u00e9",
            "\u00e9Alpha",
            "Alpha\u00e9",
            "\u8a66\u9a13",
            "Alpha/1/2/3\u00e9",
            "http://grid.example/\u8a66\u9a13",
            "http://\u00e9.example:80/\u8a66\u9a13",
            "http://\u00e9@grid.example/Alpha",
            "http://u.\u00e9@grid.example/Alpha",
            "http://user@grid.example/%E8%A9%A6%E9%A8%93",
            "Alpha%00Beta",
            "Alpha\0Beta",
        ]
    )
    rng = random.Random(53771)
    for index in range(200):
        name = "".join(rng.choice("ABCabc012 +%():/.,;-_") for _ in range(rng.randrange(1, 32)))
        values.append(name)
    cases = [(mode, value, "") for mode in [0, 1, 3, 4, 5] for value in values]
    cases.extend(
        (2, old, new)
        for old in [
            "https://grid.example:9000/Alpha/1/2/3",
            "Alpha(4,5,6)",
            "http://user@grid.example/Alpha",
        ]
        for new in [None, "", "Beta", "http://other.example/Gamma", "Delta/7", ".bad"]
    )
    encode = lambda value: "N" if value is None else value.encode().hex()
    lines = [
        str(mode) + "|" + encode(value) + "|" + encode(second) for mode, value, second in cases
    ]
    data = "\n".join(lines) + "\n"
    outputs = []
    for executable in [[args.dotnet, str(work / "app/UriReference.dll")], [args.native]]:
        run = subprocess.run(
            executable, input=data, capture_output=True, text=True, encoding="utf-8", timeout=30
        )
        if run.returncode:
            raise RuntimeError(run.stdout + run.stderr)
        outputs.append(run.stdout.splitlines())
    if any(len(output) != len(cases) for output in outputs):
        raise AssertionError("URI probe length differs")
    differences = [
        {
            "input": case,
            "reference": bytes.fromhex(expected).decode(),
            "native": bytes.fromhex(actual).decode(),
        }
        for case, expected, actual in zip(cases, *outputs)
        if expected != actual
    ]
    report = {
        "passed": not differences,
        "scenarioCount": len(cases),
        "sourceHash": hashlib.sha256((ROOT / source).read_bytes()).hexdigest(),
        "dependencyHash": hashlib.sha256(
            (ROOT / "bin/OpenMetaverseTypes.dll").read_bytes()
        ).hexdigest(),
        "differences": differences,
    }
    (work / "region-uri-parity-results.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    if differences:
        raise AssertionError(json.dumps(differences[:6], indent=2))
    print(f"PASS: {len(cases)} RegionURI parser/state observations")


if __name__ == "__main__":
    main()
