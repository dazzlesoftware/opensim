"""Inventory evidence and audit obligations without equating citations with parity."""
from __future__ import annotations

import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "native-port"
OUT = NATIVE / "parity-audit"


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def save(name, value):
    (OUT / name).write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def domain(path):
    groups = [
        ("ScriptEngine", "Scripting and script persistence"),
        ("PhysicsModules", "Physics"), ("/Tests/", "Legacy tests"),
        ("/Data/", "Database providers and migrations"),
        ("/Services/", "Services and federation"),
        ("/ClientStack/", "Viewer protocols and capabilities"),
        ("/Framework/Scenes/", "Scene and object lifecycle"),
        ("/OptionalModules/", "Optional modules"),
        ("/CoreModules/", "Core region modules"),
        ("Prebuild/", "Legacy build tooling"),
        ("ThirdParty/", "Bundled dependencies"),
        ("/Server/", "Server deployment and handlers"),
    ]
    return next((label for token, label in groups if token in path), "Framework and applications")


def main():
    OUT.mkdir(exist_ok=True)
    baseline = read_json(NATIVE / "source-baseline.json")
    mapping = read_json(NATIVE / "port-map.json")
    remaining = read_json(NATIVE / "remaining-work.json")
    preview = read_json(NATIVE / "preview-acceptance.json")
    mapped = collections.defaultdict(list)
    for entry in mapping["entries"]:
        mapped[entry["source"]].append(entry)

    # Index only authoring inputs. Build directories contain generated copies and
    # would otherwise inflate evidence counts and hide absent implementation.
    documents = []
    for folder in ("src", "include", "tests", "tools"):
        for path in (NATIVE / folder).rglob("*"):
            if path.suffix in {".cpp", ".hpp", ".cs", ".py", ".ps1", ".txt"}:
                documents.append(path)
    documents += [NATIVE / "CMakeLists.txt", NATIVE / "BUILDING.txt"]
    path_mentions = collections.defaultdict(set)
    symbols = collections.defaultdict(set)
    for path in documents:
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        relative = path.relative_to(ROOT).as_posix()
        normalized = text.replace("\\", "/")
        for reference in re.findall(r"(?:OpenSim|Prebuild|ThirdParty)/[\w./+-]+\.cs", normalized):
            path_mentions[reference].add(relative)
        for symbol in set(re.findall(r"\b[A-Za-z_]\w{3,}\b", text)):
            symbols[symbol].add(relative)

    tracked = subprocess.check_output(["git", "ls-files", "--", "*.cs"], cwd=ROOT, text=True).splitlines()
    tracked = sorted(p for p in tracked if not p.startswith("native-port/"))
    rows = []
    for source in tracked:
        path = ROOT / source
        text = path.read_text(encoding="utf-8-sig", errors="replace")
        clean = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        types = sorted(set(re.findall(r"\b(?:class|interface|struct|enum)\s+(\w+)", clean)))
        native_candidates = sorted({p for name in types for p in symbols.get(name, ())
                                    if "/src/" in p or "/include/" in p})
        references = sorted(path_mentions[source])
        tests = sorted({p for p in references if "/tests/" in p or p.endswith("CMakeLists.txt")})
        kind = "production"
        if re.search(r"(^|/)(Tests|tests)(/|$)", source):
            kind = "legacy_test"
        elif source.startswith("Prebuild/"):
            kind = "legacy_build_tool"
        elif source.startswith("ThirdParty/"):
            kind = "bundled_dependency"
        elif path.name == "AssemblyInfo.cs":
            kind = "assembly_metadata"
        elif "interface " in clean and not re.search(r"\b(class|struct)\s", clean):
            kind = "interface_contract"
        if mapped[source]:
            status = "explicit_scoped_mapping"
        elif kind in {"legacy_test", "legacy_build_tool", "assembly_metadata"}:
            status = "validation_or_replacement_obligation"
        elif tests:
            status = "original_source_referenced_by_native_tests"
        elif native_candidates:
            status = "native_symbol_candidate_requires_method_audit"
        else:
            status = "no_direct_native_evidence_found"
        rows.append({
            "source": source, "kind": kind, "domain": domain(source), "classification": status,
            "fullParity": "unverified", "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "declaredTypes": types, "mappedScopes": mapped[source],
            "exactSourceReferences": references, "nativeSymbolCandidates": native_candidates,
            "nextAction": {
                "explicit_scoped_mapping": "Compare all public/consumed methods with mapped scope and record separate Windows/Linux results.",
                "validation_or_replacement_obligation": "Record equivalent native build/test/metadata coverage or justify retirement; do not count as missing runtime feature.",
                "original_source_referenced_by_native_tests": "Inspect cited test boundaries and locate native implementation; promote only the tested subset to the source map.",
                "native_symbol_candidate_requires_method_audit": "Inspect candidate symbols for actual ownership and method coverage; name matches alone prove no implementation.",
                "no_direct_native_evidence_found": "Inspect source responsibilities and native equivalents, then add a mapping or bounded implementation task.",
            }[status],
        })
    assert len(rows) == len(tracked) == len({row["source"] for row in rows})
    save("file-classification.json", {"platforms": ["Windows", "Linux"], "files": rows})

    requirements = []
    preview_scopes = {
        "Successful viewer login", "Viewer networking", "Inventory services",
        "Avatar appearance and structured-data compatibility",
        "Connected native world and first script execution", "Baked texture uploads and asset cache",
    }
    for scope in remaining["scopes"]:
        for index, text in enumerate(scope.get("required", []), 1):
            only_mac = bool(re.match(r"^(Verify |Execute (and verify )?on |Execute on )?macOS", text)) and not any(
                word in text.lower() for word in ["full", "sql", "legacy", "windows", "linux", "simulator"])
            superseded = scope["name"] in preview_scopes and any(
                term in text.lower() for term in (
                    "viewer", "login", "appearance", "bake", "inventory", "physics", "platform"))
            requirements.append({"id": f"{remaining['scopes'].index(scope)+1:02d}-{index:02d}",
                                 "scope": scope["name"], "original": text,
                                 "status": "excluded_macos_only" if only_mac else
                                 "preview_subset_superseded_full_scope_open" if superseded else
                                 "open_requires_scope_reconciliation",
                                 "reason": "macOS excluded by user" if only_mac else
                                 "Preview verifies bounded user flows on Windows/Linux. The broad historical item retains additional API/lifecycle coverage; do not close it wholesale. See preview-evidence.json." if superseded else
                                 "Mixed historical requirements are not proof of an absent feature; reconcile at method/acceptance level."})
    save("requirement-reconciliation.json", requirements)

    preview_rows = []
    for check in preview["checks"]:
        evidence = []
        for key, value in preview["evidence"].items():
            if isinstance(value, str) and (NATIVE / value).is_file():
                evidence.append({"key": key, "path": value})
        preview_rows.append({"id": check["id"], "status": "bounded_flow_verified_with_disclosures",
                             "windows": check["windows"], "linux": check["linux"],
                             "availableEvidence": evidence})
    save("preview-evidence.json", preview_rows)
    # A missing literal is a dispatch audit candidate, not proof of absence:
    # implementations can generate dispatch names or delegate to another engine.
    native_script = "\n".join(path.read_text(encoding="utf-8-sig", errors="replace")
                              for folder in ("src", "include")
                              for path in (NATIVE / folder).rglob("*")
                              if path.suffix in {".cpp", ".hpp", ".inc"})
    api_rows = []
    for interface in (ROOT / "OpenSim/Region/ScriptEngine/Shared/Api/Interface").glob("*.cs"):
        text = interface.read_text(encoding="utf-8-sig", errors="replace")
        text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
        names = sorted(set(re.findall(r"\b((?:ll|os)[A-Z]\w*)\s*\(", text)))
        for name in names:
            api_rows.append({"api": name, "interface": interface.relative_to(ROOT).as_posix(),
                             "classification": "dispatch_literal_present_requires_behavior_verification"
                             if f'"{name}"' in native_script else "no_dispatch_literal_found_review_required"})
    save("script-api-classification.json", api_rows)
    summary = {
        "platforms": ["Windows", "Linux"], "fullParityVerified": False,
        "trackedCsharpFiles": len(rows), "uniqueMappedSources": sum(bool(entries) for entries in mapped.values()),
        "unmappedSources": sum(not mapped[row["source"]] for row in rows),
        "classificationCounts": dict(collections.Counter(row["classification"] for row in rows)),
        "kindCounts": dict(collections.Counter(row["kind"] for row in rows)),
        "domainCounts": dict(collections.Counter(row["domain"] for row in rows)),
        "sourceMapDuplicates": [p for p, entries in mapped.items() if len(entries) > 1],
        "scriptApiCounts": dict(collections.Counter(row["classification"] for row in api_rows)),
        "baselineDrift": {"newTrackedSources": sorted(set(tracked) - {s["path"] for s in baseline["sources"]}),
                          "removedTrackedSources": sorted({s["path"] for s in baseline["sources"]} - set(tracked))},
        "eta": {"forecastReady": False, "reason": "Per-method coverage and estimated acceptance tasks remain unresolved; file classification is not feature completion."},
    }
    save("summary.json", summary)
    report = ["Windows/Linux parity reconciliation", "", json.dumps(summary, indent=2), "",
              "Every tracked legacy C# file has a classification and a concrete next action in file-classification.json.",
              "Source citations and symbol candidates are discovery evidence only. No whole-file parity is promoted by this script.",
              "Legacy tests, build tools, assembly metadata and interfaces remain obligations, not automatically missing runtime features.",
              "See reconciled-checklist.txt for reviewed work packages and preview supersessions."]
    (OUT / "classification-summary.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
