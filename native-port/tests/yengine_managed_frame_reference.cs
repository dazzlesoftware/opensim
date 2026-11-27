// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Executes unchanged original YEngine-generated methods; no expression oracle.
using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using OpenSim.Region.ScriptEngine.Yengine;

public static class SuspendedProgram {
    static int suspendAt = -1, checks;

    public static void RecordStack(XMRInstAbstract instance) {
        if (instance.callMode == XMRInstAbstract.CallMode_RESTORE) {
            instance.callMode = XMRInstAbstract.CallMode_NORMAL;
            return;
        }
        if (++checks == suspendAt) {
            instance.callMode = XMRInstAbstract.CallMode_SAVE;
            throw new StackCaptureException();
        }
    }

    static string outputDirectory;

    static void Main(string[] args) {
        string directory = Path.GetFullPath(args[0]);
        outputDirectory = Path.GetFullPath(args[1]);
        Directory.CreateDirectory(outputDirectory);
        AppDomain.CurrentDomain.AssemblyResolve += (_, request) => {
            string path = Path.Combine(directory, new AssemblyName(request.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        ExecuteInputs();
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void ExecuteInputs() {
        var api = typeof(XMRInstance).Assembly.GetType("OpenSim.Region.ScriptEngine.Yengine.XMRLSL_Api", true);
        var module =
            AssemblyBuilder
                .DefineDynamicAssembly(new AssemblyName("FunctionExecutionReferenceContext"), AssemblyBuilderAccess.Run)
                .DefineDynamicModule("Context");
        var builder =
            module.DefineType("FunctionExecutionReferenceInstance", TypeAttributes.Public, typeof(XMRInstAbstract));
        builder.DefineDefaultConstructor(MethodAttributes.Public);
        builder.DefineField("m_ApiManager_LSL", api, FieldAttributes.Public);
        // Environment methods are unused by these pure scripts. CheckRunWork is
        // intentionally a no-op; generated arithmetic, calls, and stack checks run unchanged.
        foreach (var method in typeof(XMRInstAbstract).GetMethods()) {
            if (!method.IsAbstract)
                continue;
            var parameters = Array.ConvertAll(method.GetParameters(), p => p.ParameterType);
            var stub = builder.DefineMethod(method.Name, MethodAttributes.Public | MethodAttributes.Virtual,
                                            method.ReturnType, parameters);
            var il = stub.GetILGenerator();
            if (method.Name == "CheckRunWork") {
                il.Emit(OpCodes.Ldarg_0);
                il.Emit(OpCodes.Call, typeof(SuspendedProgram).GetMethod(nameof(RecordStack)));
            }
            if (method.ReturnType != typeof(void)) {
                if (method.ReturnType.IsValueType) {
                    var local = il.DeclareLocal(method.ReturnType);
                    il.Emit(OpCodes.Ldloca, local);
                    il.Emit(OpCodes.Initobj, method.ReturnType);
                    il.Emit(OpCodes.Ldloc, local);
                } else
                    il.Emit(OpCodes.Ldnull);
            }
            il.Emit(OpCodes.Ret);
            builder.DefineMethodOverride(stub, method);
        }
        Type instanceType = builder.CreateType();
        ScriptCodeGen.xmrInstSuperType = instanceType;
        ScriptObjWriter.DefineInternalType("xmrsuper", instanceType);
        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);
        ScriptConst.AddInterfaceConstants(null, api.GetFields());
        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(), instanceType.GetField("m_ApiManager_LSL"));

        var cases = new[] { ("nested",
                             "integer result;integer leaf(integer n){while(n>0){n--;}return 7;}integer mid(integer " +
                             "n){return leaf(n)+1;}default{state_entry(){result=mid(2);}}",
                             3, false),
                            ("pending",
                             "integer result;integer count;integer mark(){count++;return 3;}integer leaf(integer " +
                             "n){while(n>0){n--;}return 7;}default{state_entry(){result=leaf(2)+mark();}}",
                             2, false),
                            ("list_alias",
                             "list shared=[1,2];list alias=shared;integer result;integer leaf(integer n,list " +
                             "l){while(n>0){n--;}return n;}default{state_entry(){}touch_start(integer " +
                             "count){result=leaf(count,shared);}}",
                             2, true),
                            ("global_initializer",
                             "integer initialized=mid(2);integer leaf(integer n){while(n>0){n--;}return 7;}integer " +
                             "mid(integer n){return leaf(n)+1;}default{state_entry(){}}",
                             3, false),
                            ("codec_types", "default{state_entry(){}}", -1, false) };
        foreach (var spec in cases) {
            string folder = Path.Combine(outputDirectory, spec.Item1);
            Directory.CreateDirectory(folder);
            File.WriteAllText(Path.Combine(folder, "source.lsl"), spec.Item2);
            var errors = new List<string>();
            var tokens = TokenBegin.Construct("managed-frame-reference", null, (token, message) => errors.Add(message),
                                              spec.Item2, out string hash);
            var script = tokens == null ? null : ScriptReduce.Reduce(tokens);
            using var bytes = new MemoryStream();
            using (var writer = new BinaryWriter(bytes, System.Text.Encoding.UTF8,
                                                 true)) if (script == null ||
                                                            !ScriptCodeGen.CodeGen(
                                                                script, writer,
                                                                hash)) throw new Exception(string.Join("\n", errors));
            File.WriteAllBytes(Path.Combine(folder, "original.yobj"), bytes.ToArray());
            bytes.Position = 0;
            var disassembly = new StringWriter();
            var code = new ScriptObjCode(new BinaryReader(bytes), disassembly, null);
            File.WriteAllText(Path.Combine(folder, "original.il.txt"), disassembly.ToString());
            XMRInstAbstract NewInstance() {
                var inst = (XMRInstAbstract)Activator.CreateInstance(instanceType);
                typeof(XMRInstAbstract)
                    .GetField("heapLimit", BindingFlags.Instance | BindingFlags.NonPublic)
                    .SetValue(inst, 100000000);
                inst.stackLimit = 1024;
                inst.m_StackLeft = 1000000;
                inst.m_ObjCode = code;
                inst.ehArgs = Array.Empty<object>();
                inst.glblVars.AllocVarArrays(code.glblSizes);
                inst.eventCode = ScriptEventCode.state_entry;
                return inst;
            }
            var instance = NewInstance();
            suspendAt = -1;
            checks = 0;
            if (spec.Item4) {
                instance.CallSEH();
                instance.eventCode = ScriptEventCode.touch_start;
                instance.ehArgs = new object[] { new OpenSim.Region.ScriptEngine.Shared.LSL_Types.LSLInteger(2) };
            }
            suspendAt = spec.Item3;
            checks = 0;
            bool captured = false;
            try {
                instance.CallSEH();
            } catch (StackCaptureException) {
                captured = true;
            }
            if (spec.Item1 == "codec_types") {
                var shared = new object[] { 42, null };
                var list = new OpenSim.Region.ScriptEngine.Shared.LSL_Types.list(shared);
                instance.glblVars.iarObjects = new object[] { shared };
                instance.glblVars.iarLists = new[] { list };
                instance.eventCode = ScriptEventCode.touch_start;
                instance.ehArgs = new object[] { new OpenSim.Region.ScriptEngine.Shared.LSL_Types.LSLInteger(2) };
                instance.stackFrames =
                    new XMRStackFrame { funcName = "original-writer-codec-types", callNo = 7,
                                        objArray = new object[] { true, '\u03bb', ScriptEventCode.touch_start, null,
                                                                  shared, shared, list } };
                captured = true;
            }
            if (!captured || instance.stackFrames == null)
                throw new Exception("No original managed frames captured");
            using var snapshot = new MemoryStream();
            snapshot.WriteByte(XMRInstance.migrationVersion);
            snapshot.WriteByte(16);
            instance.MigrateOut(new BinaryWriter(snapshot, System.Text.Encoding.UTF8, true));
            File.WriteAllBytes(Path.Combine(folder, "snapshot.bin"), snapshot.ToArray());
            var metadata = new SnapshotInspector(snapshot.ToArray()).Read();
            metadata["source_hash"] = code.sourceHash;
            metadata["compiled_version"] = ScriptCodeGen.COMPILED_VERSION_VALUE;
            metadata["captured_check"] = checks;
            metadata["supplemental_original_writer_graph"] = spec.Item1 == "codec_types";
            var observedTypes = new List<object>();
            for (var frame = instance.stackFrames; frame != null; frame = frame.nextSF)
                observedTypes.Add(new { function = frame.funcName, call_no = frame.callNo,
                                        slot_types = Array.ConvertAll(frame.objArray, x => x?.GetType().FullName) });
            metadata["observed_frame_slot_types"] = observedTypes;
            var clone = (ScriptObjCode) typeof(object)
                            .GetMethod("MemberwiseClone", BindingFlags.Instance | BindingFlags.NonPublic)
                            .Invoke(code, null);
            clone.dynamicMethods = new Dictionary<string, DynamicMethod>();
            clone.scriptSrcLocss = new Dictionary<string, KeyValuePair<int, ScriptSrcLoc>[]>();
            bytes.Position = 0;
            var reader = new BinaryReader(bytes);
            reader.ReadChars(ScriptCodeGen.OBJECT_CODE_MAGIC.Length);
            reader.ReadInt32();
            reader.ReadString();
            new XMRInstArSizes().ReadFromFile(reader);
            int nstates = reader.ReadInt32();
            for (int i = 0; i < nstates; i++)
                reader.ReadString();
            while (reader.ReadString() != "") {
                reader.ReadString();
                reader.ReadInt32();
            }
            var headerTypes = new Dictionary<string, TokenDeclSDType>();
            string typeName;
            while ((typeName = reader.ReadString()) != "")
                headerTypes.Add(typeName, TokenDeclSDType.ReadFromFile(headerTypes, typeName, reader, null));
            var manifest = new ManifestTokens(clone);
            ScriptObjWriter.CreateObjCode(headerTypes, reader, clone, manifest);
            manifest.AddSourceLocations(clone);
            metadata["methods"] = manifest.Methods;
            File.WriteAllText(
                Path.Combine(folder, "object_metadata.json"),
                JsonSerializer.Serialize(
                    new { source_hash = code.sourceHash, compiled_version = ScriptCodeGen.COMPILED_VERSION_VALUE,
                          states = code.stateNames, global_names = code.globalVarNames, methods = manifest.Methods },
                    new JsonSerializerOptions { WriteIndented = true }));
            if (spec.Item1 != "codec_types") {
                var restored = NewInstance();
                snapshot.Position = 2;
                restored.MigrateIn(new BinaryReader(snapshot, System.Text.Encoding.UTF8, true));
                suspendAt = -1;
                checks = 0;
                restored.CallSEH();
                var finalGlobals = new SortedDictionary<string, object>();
                foreach (var names in code.globalVarNames) {
                    var array = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(restored.glblVars);
                    foreach (var entry in names.Value) {
                        object value = array.GetValue(entry.Key);
                        finalGlobals[entry.Value] = value is int || value is double || value is string || value == null
                                                        ? value
                                                        : value.ToString();
                    }
                }
                metadata["original_roundtrip_resume_globals"] = finalGlobals;
            }
            var hashes = new Dictionary<string, string>();
            foreach (string file in new[] { "source.lsl", "snapshot.bin", "original.yobj", "original.il.txt",
                                            "object_metadata.json" })
                hashes[file] = Convert
                                   .ToHexString(System.Security.Cryptography.SHA256.HashData(
                                       File.ReadAllBytes(Path.Combine(folder, file))))
                                   .ToLowerInvariant();
            hashes["original_yengine_assembly"] = Convert
                                                      .ToHexString(System.Security.Cryptography.SHA256.HashData(
                                                          File.ReadAllBytes(typeof(XMRInstance).Assembly.Location)))
                                                      .ToLowerInvariant();
            hashes["generator_assembly"] = Convert
                                               .ToHexString(System.Security.Cryptography.SHA256.HashData(
                                                   File.ReadAllBytes(typeof(SuspendedProgram).Assembly.Location)))
                                               .ToLowerInvariant();
            string generatorSource = Path.Combine(outputDirectory, "..", "yengine_managed_frame_reference.cs");
            if (File.Exists(generatorSource))
                hashes["generator_source"] =
                    Convert
                        .ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(generatorSource)))
                        .ToLowerInvariant();
            metadata["sha256"] = hashes;
            File.WriteAllText(Path.Combine(folder, "metadata.json"),
                              JsonSerializer.Serialize(metadata, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(spec.Item1 + ": captured original frames, serialized " + snapshot.Length + " bytes");
        }
    }
}

sealed class SnapshotInspector {
    readonly BinaryReader reader;
    int nextId;

    public SnapshotInspector(byte[] data) {
        reader = new BinaryReader(new MemoryStream(data));
    }

    Dictionary<string, object> Node() {
        int kind = reader.ReadByte();
        var node = new Dictionary<string, object> { { "kind", kind } };
        switch (kind) {
        case 0:
        case 17:
            return node;
        case 1:
        case 3:
        case 12:
            node["value"] = reader.ReadInt32();
            return node;
        case 2:
        case 10:
            node["value"] = reader.ReadDouble();
            return node;
        case 11:
            node["value"] = (double)reader.ReadSingle();
            return node;
        case 16:
            node["value"] = reader.ReadBoolean();
            return node;
        case 20:
            node["value"] = (int)reader.ReadChar();
            return node;
        case 6:
            node["value"] =
                new[] { reader.ReadDouble(), reader.ReadDouble(), reader.ReadDouble(), reader.ReadDouble() };
            return node;
        case 8:
            node["value"] = new[] { reader.ReadDouble(), reader.ReadDouble(), reader.ReadDouble() };
            return node;
        case 4:
        case 7:
            node["children"] = new[] { Node() };
            return node;
        case 15:
            node["reference"] = reader.ReadInt32();
            return node;
        case 5:
            node["id"] = nextId++;
            node["children"] = new[] { Node() };
            return node;
        case 13:
            node["id"] = nextId++;
            node["value"] = reader.ReadString();
            return node;
        case 9:
            node["id"] = nextId++;
            node["element_type"] = reader.ReadString();
            int length = reader.ReadInt32();
            var children = new List<object>();
            for (int i = 0; i < length; i++)
                children.Add(Node());
            node["children"] = children;
            return node;
        default:
            throw new Exception("Unexpected original fixture wire tag " + kind);
        }
    }

    public Dictionary<string, object> Read() {
        var data = new Dictionary<string, object>();
        data["migration_version"] = reader.ReadByte();
        data["reserved"] = reader.ReadByte();
        data["event_arguments"] = Node();
        data["initialize_globals"] = reader.ReadBoolean();
        data["state_index"] = reader.ReadInt32();
        data["event_code"] = reader.ReadInt32();
        var globals = new List<object>();
        for (int i = 0; i < 11; i++)
            globals.Add(Node());
        data["globals"] = globals;
        var frames = new List<object>();
        string name;
        while ((name = reader.ReadString()) != "") {
            if (name == "**newStateCode**") {
                data["next_state_index"] = reader.ReadInt32();
                continue;
            }
            frames.Add(new Dictionary<string, object> { { "function", name },
                                                        { "call_no", reader.ReadInt32() },
                                                        { "slots", Node() } });
        }
        if (reader.BaseStream.Position != reader.BaseStream.Length)
            throw new Exception("Unparsed fixture bytes");
        data["frames"] = frames;
        return data;
    }
}

sealed class ManifestTokens : ObjectTokens {
    public readonly List<Dictionary<string, object>> Methods = new();
    readonly Dictionary<string, Dictionary<string, object>> byName = new();
    Dictionary<string, object> current;

    public ManifestTokens(ScriptObjCode code) : base(code) {}

    static string Alias(Type type) {
        var aliases = (Dictionary<Type, string>)typeof(ScriptObjWriter)
                          .GetField("type2String", BindingFlags.Static | BindingFlags.NonPublic)
                          .GetValue(null);
        return aliases.TryGetValue(type, out string alias) ? alias : type.FullName;
    }

    List<Dictionary<string, object>> Records => (List<Dictionary<string, object>>)current["records"];

    void Emit(int offset, OpCode op, string kind = "none", object operand = null) {
        Records.Add(new Dictionary<string, object> { { "offset", offset },
                                                     { "opcode", op.Name },
                                                     { "opcode_value", (int)op.Value },
                                                     { "operand_kind", kind },
                                                     { "operand", operand } });
    }

    public override void Close() {}

    public override void DefineMethod(string name, Type ret, Type[] types, string[] names) {
        var args = new List<object>();
        for (int i = 0; i < types.Length; i++)
            args.Add(new Dictionary<string, object> { { "name", names[i] },
                                                      { "type", Alias(types[i]) },
                                                      { "clr_type", types[i].FullName } });
        var value = new Dictionary<string, object> { { "name", name },
                                                     { "return_type", Alias(ret) },
                                                     { "return_clr_type", ret.FullName },
                                                     { "parameters", args },
                                                     { "locals", new List<object>() },
                                                     { "labels", new List<Dictionary<string, object>>() },
                                                     { "records", new List<Dictionary<string, object>>() } };
        Methods.Add(value);
        byName[name] = value;
    }

    public override void BegMethod(DynamicMethod method) {
        current = byName[method.Name];
    }

    public override void EndMethod() {}

    public override void DefineLabel(int number, string name) {
        ((List<Dictionary<string, object>>)current["labels"])
            .Add(new Dictionary<string, object> { { "number", number }, { "name", name } });
    }

    public override void DefineLocal(int number, string name, string type, Type clr) {
        ((List<object>)current["locals"]).Add(new { number, name, type, clr_type = clr.FullName });
    }

    public override void MarkLabel(int offset, int number) {
        foreach (var l in (List<Dictionary<string, object>>)current["labels"])
            if ((int)l["number"] == number)
                l["offset"] = offset;
    }

    public override void BegExcBlk(int o) {
        Emit(o, OpCodes.Nop, "exception", "begin");
    }

    public override void BegCatBlk(int o, Type t) {
        Emit(o, OpCodes.Nop, "exception", "catch:" + t.FullName);
    }

    public override void BegFinBlk(int o) {
        Emit(o, OpCodes.Nop, "exception", "finally");
    }

    public override void EndExcBlk(int o) {
        Emit(o, OpCodes.Nop, "exception", "end");
    }

    public override void EmitNull(int o, OpCode op) {
        Emit(o, op);
    }

    public override void EmitField(int o, OpCode op, FieldInfo f) {
        Emit(o, op, "field", f.DeclaringType.FullName + ":" + f.Name);
    }

    public override void EmitLocal(int o, OpCode op, int n) {
        Emit(o, op, "local", n);
    }

    public override void EmitType(int o, OpCode op, Type t) {
        Emit(o, op, "type", t.FullName);
    }

    public override void EmitLabel(int o, OpCode op, int n) {
        Emit(o, op, "label", n);
    }

    public override void EmitLabels(int o, OpCode op, int[] ns) {
        Emit(o, op, "labels", ns);
    }

    public override void EmitMethod(int o, OpCode op, MethodInfo m) {
        Emit(o, op, "method", (m.DeclaringType?.FullName ?? "<script>") + ":" + m.Name);
    }

    public override void EmitCtor(int o, OpCode op, ConstructorInfo c) {
        Emit(o, op, "constructor", c.DeclaringType.FullName);
    }

    public override void EmitDouble(int o, OpCode op, double v) {
        Emit(o, op, "double", v);
    }

    public override void EmitFloat(int o, OpCode op, float v) {
        Emit(o, op, "float", v);
    }

    public override void EmitInteger(int o, OpCode op, int v) {
        Emit(o, op, "integer", v);
    }

    public override void EmitString(int o, OpCode op, string v) {
        Emit(o, op, "string", v);
    }

    public void AddSourceLocations(ScriptObjCode code) {
        foreach (var method in Methods) {
            var name = (string)method["name"];
            if (!code.scriptSrcLocss.TryGetValue(name, out var locations))
                continue;
            method["source_locations"] =
                Array.ConvertAll(locations, pair => new { offset = pair.Key, file = pair.Value.file,
                                                          line = pair.Value.line, column = pair.Value.posn });
            foreach (var record in (List<Dictionary<string, object>>)method["records"]) {
                ScriptSrcLoc found = null;
                foreach (var pair in locations)
                    if (pair.Key <= (int)record["offset"])
                        found = pair.Value;
                    else
                        break;
                if (found != null)
                    record["source"] = new { file = found.file, line = found.line, column = found.posn };
            }
            var calls = new List<object>();
            foreach (var label in (List<Dictionary<string, object>>)method["labels"]) {
                var match = System.Text.RegularExpressions.Regex.Match((string)label["name"], @"^__call_(\d+)_(\d+)$");
                if (!match.Success)
                    continue;
                var call = new Dictionary<string, object> { { "call_no", int.Parse(match.Groups[1].Value) },
                                                            { "label", label } };
                foreach (var record in (List<Dictionary<string, object>>)method["records"])
                    if (label.TryGetValue("offset", out var offset) && (int)record["offset"] == (int)offset &&
                        record.TryGetValue("source", out var source)) {
                        call["source"] = source;
                        break;
                    }
                calls.Add(call);
            }
            method["call_labels"] = calls;
            var slots = new List<object>();
            var records = (List<Dictionary<string, object>>)method["records"];
            int capture = records.FindIndex(r => r["operand"] is string s && s.EndsWith(":CaptureStackFrame"));
            if (capture >= 0) {
                int start = capture + 1;
                for (int end = start; end < records.Count; end++) {
                    if ((string)records[end]["opcode"] != "stelem.ref")
                        continue;
                    int? index = null;
                    Dictionary<string, object> storage = null;
                    for (int j = start; j < end; j++) {
                        string op = (string)records[j]["opcode"];
                        if (index == null && op.StartsWith("ldc.i4")) {
                            index = records[j]["operand"] is int integer ? integer
                                    : op == "ldc.i4.m1"                  ? -1
                                                                         : int.Parse(op.Substring(7));
                            continue;
                        }
                        if (index != null && storage == null && (op.StartsWith("ldloc") || op.StartsWith("ldarg")))
                            storage = records[j];
                    }
                    if (index != null) {
                        var slot = new Dictionary<string, object> { { "index", index.Value },
                                                                    { "capture_record_begin", start },
                                                                    { "capture_record_end", end } };
                        if (storage != null) {
                            string op = (string)storage["opcode"];
                            bool argument = op.StartsWith("ldarg");
                            int number = storage["operand"] is int numberValue
                                             ? numberValue
                                             : int.Parse(op.Substring(op.IndexOf('.') + 1));
                            slot["storage_kind"] = argument ? "argument" : "local";
                            slot["storage_number"] = number;
                            if (argument) {
                                var args = (List<object>)method["parameters"];
                                if (number < args.Count)
                                    foreach (var pair in (Dictionary<string, object>)args[number])
                                        slot[pair.Key] = pair.Value;
                            } else
                                foreach (var local in (List<object>)method["locals"]) {
                                    var properties = local.GetType().GetProperties();
                                    if ((int)local.GetType().GetProperty("number").GetValue(local) != number)
                                        continue;
                                    foreach (var property in properties)
                                        if (property.Name != "number")
                                            slot[property.Name] = property.GetValue(local);
                                }
                        }
                        slots.Add(slot);
                    }
                    start = end + 1;
                }
            }
            method["saved_slots"] = slots;
        }
    }
}
