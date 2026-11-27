"""Extract method-level evidence and compile declared script API calls.

Requires tree-sitter, tree-sitter-c-sharp and tree-sitter-cpp on PYTHONPATH.
Compilation acceptance is kept separate from runtime behavioral equivalence.
"""
import collections
import hashlib
import json
from pathlib import Path
import re
import subprocess

from tree_sitter import Language, Parser
import tree_sitter_c_sharp
import tree_sitter_cpp

ROOT = Path(__file__).resolve().parents[2]
NATIVE = ROOT / "native-port"
OUT = NATIVE / "parity-audit"


def walk(node):
    stack = [node]
    while stack:
        current = stack.pop()
        yield current
        stack.extend(reversed(current.named_children))


def text(data, node):
    return data[node.start_byte:node.end_byte].decode("utf-8", errors="replace") if node else ""


def key(name):
    return re.sub(r"[^a-z0-9]", "", name.lower())


def argument(kind):
    kind = kind.strip()
    if kind in {"LSL_Vector", "Vector3", "vector", "LSL_Types.Vector3"}:
        return "<0.0, 0.0, 0.0>"
    if kind in {"LSL_Rotation", "Quaternion", "rotation"}:
        return "<0.0, 0.0, 0.0, 1.0>"
    if kind in {"LSL_List", "list"} or kind.endswith("[]"):
        return "[]"
    if kind in {"LSL_String", "LSL_Key", "string", "key", "LSL_Types.LSLString"}:
        return '""'
    if kind in {"LSL_Float", "float", "double"}:
        return "0.0"
    if kind in {"LSL_Integer", "int", "uint", "long", "bool", "LSL_Types.LSLInteger"}:
        return "0"
    raise ValueError(kind)


def main():
    cs = Parser(Language(tree_sitter_c_sharp.language()))
    cpp = Parser(Language(tree_sitter_cpp.language()))
    classification = json.loads((OUT / "file-classification.json").read_text())
    definitions = collections.defaultdict(list)
    native_errors = []
    for path in (NATIVE / "src").glob("*.cpp"):
        data = path.read_bytes()
        tree = cpp.parse(data)
        if tree.root_node.has_error:
            native_errors.append(path.relative_to(ROOT).as_posix())
        for node in walk(tree.root_node):
            if node.type != "function_definition":
                continue
            declarator = node.child_by_field_name("declarator")
            if not declarator:
                continue
            function = next((n for n in walk(declarator) if n.type == "function_declarator"), None)
            if not function:
                continue
            name = text(data, function.child_by_field_name("declarator"))
            short = name.split("::")[-1]
            if re.fullmatch(r"[A-Za-z_]\w*", short):
                definitions[key(short)].append({"symbol": name, "file": path.relative_to(ROOT).as_posix(),
                                                "line": node.start_point.row + 1})
    methods = []
    calls = []
    source_errors = []
    for record in classification["files"]:
        path = ROOT / record["source"]
        data = path.read_bytes()
        tree = cs.parse(data)
        if tree.root_node.has_error:
            source_errors.append(record["source"])
        allowed_files = {"native-port/" + n for entry in record["mappedScopes"] for n in entry["native"]}
        allowed_files.update(record["nativeSymbolCandidates"])
        for node in walk(tree.root_node):
            if node.type not in {"method_declaration", "constructor_declaration", "property_declaration", "indexer_declaration"}:
                continue
            name = text(data, node.child_by_field_name("name")) or "this[]"
            parameters = node.child_by_field_name("parameters")
            signature_end = parameters.end_byte if parameters else node.start_byte + min(node.end_byte-node.start_byte, 200)
            signature = data[node.start_byte:signature_end].decode("utf-8", errors="replace")
            candidates = [item for item in definitions[key(name)] if item["file"] in allowed_files]
            methods.append({"source": record["source"], "line": node.start_point.row + 1,
                            "kind": node.type, "name": name, "signature": signature,
                            "domain": record["domain"], "fileKind": record["kind"],
                            "bodyHash": hashlib.sha256(data[node.start_byte:node.end_byte]).hexdigest(),
                            "nativeDefinitionCandidates": candidates,
                            "behaviorStatus": "requires_contract_review",
                            "limitations": "Name matching can miss consolidated implementations and inherited methods; candidates do not establish equivalence."})
            if "/Api/Interface/" not in record["source"] or not re.fullmatch(r"(?:ll|os)[A-Z]\w*", name):
                continue
            types = [text(data, p.child_by_field_name("type")) for p in parameters.named_children if p.type == "parameter"]
            call = {"id": len(calls), "api": name, "source": record["source"],
                    "line": node.start_point.row+1, "signature": signature, "parameterTypes": types}
            try:
                args = ", ".join(argument(kind) for kind in types)
                source = f"default {{ state_entry() {{ {name}({args}); }} }}\n"
                path = OUT / "probe/calls" / f"call-{call['id']:04d}.lsl"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(source)
                call["probe"] = path.relative_to(ROOT).as_posix()
            except ValueError as error:
                call["status"] = "probe_generator_unsupported_type"
                call["reason"] = str(error)
            calls.append(call)
    (OUT / "method-inventory.json").write_text(json.dumps(methods, indent=2) + "\n")
    executable = NATIVE / "build-https/Release/parity_compile_probe.exe"
    # Positive control distinguishes a broken probe/link from an unsupported API.
    control = OUT / "probe/control.lsl"
    control.write_text('default { state_entry() { llOwnerSay("audit control"); } }\n')
    check = subprocess.run([str(executable), str(control)], capture_output=True, text=True, check=True)
    assert "\taccepted" in check.stdout, check.stdout + check.stderr
    pending = [call for call in calls if "probe" in call]
    for start in range(0, len(pending), 40):
        batch = pending[start:start+40]
        result = subprocess.run([str(executable), *[str(ROOT / c["probe"]) for c in batch]],
                                capture_output=True, text=True, check=True)
        lines = result.stdout.splitlines()
        assert len(lines) == len(batch), result.stdout
        for call, line in zip(batch, lines):
            fields = line.split("\t")
            call["windowsCompile"] = fields[1]
            call["diagnostics"] = fields[2:]
            call["runtimeBehavior"] = "not_executed_by_compile_probe"
    (OUT / "script-api-method-review.json").write_text(json.dumps(calls, indent=2)+"\n")
    summary = {"sourceMethodsPropertiesConstructors": len(methods),
               "scriptOverloads": len(calls),
               "windowsCompile": dict(collections.Counter(call.get("windowsCompile", call.get("status")) for call in calls)),
               "originalParseErrorFiles": source_errors, "nativeParseErrorFiles": native_errors,
               "windowsProbeSha256": hashlib.sha256(executable.read_bytes()).hexdigest(),
               "windowsLinkedArchiveSha256": hashlib.sha256((executable.parent / "opensim_server_core.lib").read_bytes()).hexdigest(),
               "compilerBehaviorIsNotRuntimeParity": True}
    (OUT / "method-review-summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    print(json.dumps({k:v for k,v in summary.items() if not k.endswith("Files")}, indent=2))


if __name__ == "__main__":
    main()
