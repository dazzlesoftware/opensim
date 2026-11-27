using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using OpenSim.Region.ScriptEngine.Yengine;
using OpenSim.Region.ScriptEngine.Shared;
using LSL = OpenSim.Region.ScriptEngine.Shared.LSL_Types;

public static class SuspendedProgram {
    static string mode;
    static bool capture;
    static int checks;

    public static void RecordStack(XMRInstAbstract inst) {
        ++checks;
        if (inst.callMode == XMRInstAbstract.CallMode_RESTORE) {
            inst.callMode = XMRInstAbstract.CallMode_NORMAL;
            return;
        }
        if (capture &&
            (inst.suspendOnCheckRunTemp || (mode == "background" && inst.eventCode == ScriptEventCode.touch_start) ||
             (mode == "background_function" && new System.Diagnostics.StackTrace().ToString().Contains("advance")))) {
            inst.callMode = XMRInstAbstract.CallMode_SAVE;
            throw new StackCaptureException();
        }
        if (checks > 1000)
            throw new Exception("Run exceeded bounded check budget");
    }

    static BindingFlags priv = BindingFlags.Instance | BindingFlags.NonPublic;

    static DetectParams MakeDetect(string name, int seed) {
        var d = new DetectParams();
        d.Name = name;
        d.LinkNum = seed + 2;
        d.Type = seed + 8;
        foreach (var pair in new[] { ("Key", "11111111-2222-3333-4444-555555555555"),
                                     ("Owner", "22222222-3333-4444-5555-666666666666"),
                                     ("Group", "33333333-4444-5555-6666-777777777777") }) {
            var field = typeof(DetectParams).GetField(pair.Item1);
            field.SetValue(d, Activator.CreateInstance(field.FieldType, new object[] { pair.Item2 }));
        }
        d.OffsetPos = new LSL.Vector3(seed + 0.125, seed + 0.25, seed + 0.5);
        d.Position = new LSL.Vector3(seed + 10.00000003, seed + 11.00000003, seed + 12.00000003);
        d.Rotation = new LSL.Quaternion(0.125, 0.25, 0.5, 0.875);
        d.Velocity = new LSL.Vector3(seed + 20.00000003, seed + 21.00000003, seed + 22.00000003);
        int i = 0;
        foreach (var field in new[] { "touchST", "touchNormal", "touchBinormal", "touchPos", "touchUV" }) {
            double value = 1.00000003 + seed + i++ * 3;
            typeof(DetectParams).GetField(field, priv).SetValue(d, new LSL.Vector3(value, value + 1, value + 2));
        }
        typeof(DetectParams).GetField("touchFace", priv).SetValue(d, seed + 3);
        return d;
    }

    static IEnumerable<XMRStackFrame> EnumerateFrames(XMRInstAbstract inst) {
        for (var f = inst.stackFrames; f != null; f = f.nextSF)
            yield return f;
    }

    static object Describe(DetectParams d) => new {
        key = d.Key.ToString(),
        owner = d.Owner.ToString(),
        group = d.Group.ToString(),
        name = d.Name,
        link_number = d.LinkNum,
        type = d.Type,
        grab = new[] { d.OffsetPos.x, d.OffsetPos.y, d.OffsetPos.z },
        position = new[] { d.Position.x, d.Position.y, d.Position.z },
        rotation = new[] { d.Rotation.x, d.Rotation.y, d.Rotation.z, d.Rotation.s },
        velocity = new[] { d.Velocity.x, d.Velocity.y, d.Velocity.z },
        touch_st = new[] { d.TouchST.x, d.TouchST.y, d.TouchST.z },
        touch_normal = new[] { d.TouchNormal.x, d.TouchNormal.y, d.TouchNormal.z },
        touch_binormal = new[] { d.TouchBinormal.x, d.TouchBinormal.y, d.TouchBinormal.z },
        touch_pos = new[] { d.TouchPos.x, d.TouchPos.y, d.TouchPos.z },
        touch_uv = new[] { d.TouchUV.x, d.TouchUV.y, d.TouchUV.z },
        touch_face = d.TouchFace
    };

    static void Main(string[] args) {
        var bin = Path.GetFullPath(args[0]);
        var output = Path.GetFullPath(args[1]);
        Directory.CreateDirectory(output);
        AppDomain.CurrentDomain.AssemblyResolve += (_, r) => {
            var p = Path.Combine(bin, new AssemblyName(r.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        Run(output);
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void Run(string output) {
        var api = typeof(XMRInstance).Assembly.GetType("OpenSim.Region.ScriptEngine.Yengine.XMRLSL_Api", true);
        var mb =
            AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("OriginalDequeueContext"), AssemblyBuilderAccess.Run)
                .DefineDynamicModule("Context");
        var tb = mb.DefineType("FunctionExecutionReferenceInstance", TypeAttributes.Public, typeof(XMRInstance));
        var ctor = tb.DefineConstructor(MethodAttributes.Public, CallingConventions.Standard, Type.EmptyTypes);
        var ci = ctor.GetILGenerator();
        ci.Emit(OpCodes.Ldnull);
        ci.Emit(OpCodes.Throw);
        tb.DefineField("m_ApiManager_LSL", api, FieldAttributes.Public);
        var check = typeof(XMRInstance).GetMethod("CheckRunWork");
        var checkimpl = tb.DefineMethod("CheckRunWork", MethodAttributes.Public | MethodAttributes.Virtual,
                                        typeof(void), Type.EmptyTypes);
        var il = checkimpl.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Call, typeof(SuspendedProgram).GetMethod("RecordStack"));
        il.Emit(OpCodes.Ret);
        tb.DefineMethodOverride(checkimpl, check);
        var type = tb.CreateType();
        ScriptCodeGen.xmrInstSuperType = type;
        ScriptObjWriter.DefineInternalType("xmrsuper", type);
        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);
        ScriptConst.AddInterfaceConstants(null, api.GetFields());
        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(), type.GetField("m_ApiManager_LSL"));
        int ts = (int)ScriptEventCode.touch_start, te = (int)ScriptEventCode.touch_end;
        foreach (var name in new[] { "background", "background_function", "background_wait", "background_sleep" }) {
            mode = name;
            var folder = Path.Combine(output, name);
            Directory.CreateDirectory(folder);
            int ret = name == "timeout" ? 0 : 1 << (name.StartsWith("background") ? te : ts);
            int bg = name == "timeout" ? 0 : 1 << ts;
            string timeout = "600.0";
            string source =
                $"list result;integer count;integer background;default{{state_entry(){{count++;result=xmrEventDequeue({timeout},{ret},0,{bg},0);count+=10;}}touch_start(integer n){{while(background<n)background++;}}touch_end(integer n){{background+=100;}}timer(){{background+=1000;}}}}";
            if (name == "background_function")
                source =
                    source
                        .Replace(
                            "default{",
                            "integer advance(integer n){while(background<n)background++;return background;}default{")
                        .Replace("touch_start(integer n){while(background<n)background++;}",
                                 "touch_start(integer n){background=advance(n);}");
            if (name == "background_wait")
                source = source.Replace(
                    "touch_start(integer n){while(background<n)background++;}",
                    "touch_start(integer n){background=llGetListLength(xmrEventDequeue(-1.0,0,0,0,0))+n;}");
            if (name == "background_sleep")
                source = source.Replace("touch_start(integer n){while(background<n)background++;}",
                                        "touch_start(integer n){llSleep(0.2);background=n;}");
            File.WriteAllText(Path.Combine(folder, "source.lsl"), source);
            var cache = new System.Text.StringBuilder();
            typeof(XMRInstance)
                .GetMethod("ByteArrayToSixbitStr", BindingFlags.Static | BindingFlags.NonPublic)
                .Invoke(null, new object[] { cache, System.Security.Cryptography.SHA256.HashData(
                                                        System.Text.Encoding.UTF8.GetBytes(source)) });
            File.WriteAllText(Path.Combine(folder, "cache_key.txt"), cache.ToString() + "\n");
            var errors = new List<string>();
            var tokens =
                TokenBegin.Construct("dequeue-reference", null, (t, m) => errors.Add(m), source, out string hash);
            var reduced = tokens == null ? null : ScriptReduce.Reduce(tokens);
            using var codebytes = new MemoryStream();
            using (var bw = new BinaryWriter(
                       codebytes, System.Text.Encoding.UTF8,
                       true)) if (reduced == null ||
                                  !ScriptCodeGen.CodeGen(reduced, bw, hash)) throw new Exception(string.Join("\n",
                                                                                                             errors));
            File.WriteAllBytes(Path.Combine(folder, "original.yobj"), codebytes.ToArray());
            codebytes.Position = 0;
            var dis = new StringWriter();
            var code = new ScriptObjCode(new BinaryReader(codebytes), dis, null);
            File.WriteAllText(Path.Combine(folder, "original.il.txt"), dis.ToString());
            XMRInstance New() {
                var inst = (XMRInstance)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(type);
                inst.glblVars = new XMRInstArrays(inst);
                typeof(XMRInstAbstract).GetField("heapLimit", priv).SetValue(inst, 100000000);
                inst.stackLimit = 1024;
                inst.m_StackLeft = 1000000;
                inst.m_ObjCode = code;
                inst.newStateCode = -1;
                typeof(XMRInstance)
                    .GetField("m_DetectParams", priv)
                    .SetValue(inst, new[] { MakeDetect("outer detect marker", 9) });
                inst.m_QueueLock = new object();
                inst.m_EventQueue = new LinkedList<EventParams>();
                typeof(XMRInstance).GetField("m_EventCounts", priv).SetValue(inst, new int[(int)ScriptEventCode.Size]);
                inst.ehArgs = Array.Empty<object>();
                inst.glblVars.AllocVarArrays(code.glblSizes);
                inst.eventCode = ScriptEventCode.state_entry;
                var apiobj = Activator.CreateInstance(api);
                api.GetMethod("InitXMRLSLApi").Invoke(apiobj, new object[] { inst });
                type.GetField("m_ApiManager_LSL").SetValue(inst, apiobj);
                type.GetField("m_LSL_Functions").SetValue(inst, apiobj);
                return inst;
            }
            void Queue(XMRInstance inst, ScriptEventCode ev, int n) {
                inst.m_EventQueue.AddLast(new EventParams(
                    ev.ToString(), ev == ScriptEventCode.timer ? Array.Empty<object>() : new object[] { n },
                    new[] { MakeDetect(ev.ToString() + " detect marker", (int)ev) }));
                var counts = (int[]) typeof(XMRInstance).GetField("m_EventCounts", priv).GetValue(inst);
                counts[(int)ev]++;
            }
            var original = New();
            if (name.StartsWith("background")) {
                Queue(original, ScriptEventCode.timer, 0);
                Queue(original, ScriptEventCode.touch_start, 7);
                Queue(original, ScriptEventCode.touch_end, 9);
            }
            capture = true;
            checks = 0;
            bool captured = false;
            try {
                original.CallSEH();
            } catch (StackCaptureException) {
                captured = true;
            }
            if (!captured || original.stackFrames == null)
                throw new Exception("No genuine capture for " + name);
            using var wire = new MemoryStream();
            wire.WriteByte(XMRInstance.migrationVersion);
            wire.WriteByte(16);
            original.MigrateOut(new BinaryWriter(wire, System.Text.Encoding.UTF8, true));
            File.WriteAllBytes(Path.Combine(folder, "snapshot.bin"), wire.ToArray());
            var metadata = new SnapshotInspector(wire.ToArray()).Read();
            metadata["source_hash"] = hash;
            metadata["captured_checks"] = checks;
            metadata["api_sleep_before_resume"] = original.m_SleepUntil.ToString("O");
            metadata["original_method"] = typeof(XMRInstance).GetMethod("xmrEventDequeue").ToString();
            var frames = new List<object>();
            for (var f = original.stackFrames; f != null; f = f.nextSF) {
                var values = new List<object>();
                foreach (var v in f.objArray)
                    values.Add(v == null ? null : new { type = v.GetType().FullName, value = v.ToString() });
                frames.Add(new { function = f.funcName, call_no = f.callNo, slots = values });
            }
            metadata["frame_summary"] = frames;
            foreach (var f in EnumerateFrames(original))
                if (f.funcName == "xmrEventDequeue" && f.callNo == 1) {
                    var decodedDetects = (DetectParams[]) typeof(XMRInstance)
                                             .GetMethod("ObjArrToDetPrms", BindingFlags.Static | BindingFlags.NonPublic)
                                             .Invoke(null, new object[] { f.objArray[7] });
                    metadata["source_background_detector"] = Describe(MakeDetect("touch_start detect marker", ts));
                    metadata["restored_background_detector"] = Describe(decodedDetects[0]);
                    if (decodedDetects[0].Position.x != ts + 10.00000003 ||
                        decodedDetects[0].TouchST.x != (double)(float)(ts + 1.00000003))
                        throw new Exception("Original detector precision contract changed");
                    break;
                }

            var restoredXml = new System.Xml.XmlDocument();
            var root = restoredXml.CreateElement("ScriptState");
            restoredXml.AppendChild(root);
            root.SetAttribute("Engine", "YEngine");
            root.SetAttribute("SourceHash", hash);
            root.SetAttribute("Asset", "11111111-2222-3333-4444-555555555555");
            var sn = restoredXml.CreateElement("Snapshot");
            sn.InnerText = Convert.ToBase64String(wire.ToArray());
            root.AppendChild(sn);
            var rn = restoredXml.CreateElement("Running");
            rn.InnerText = "True";
            root.AppendChild(rn);
            var detectNode = restoredXml.CreateElement("DetectArray");
            root.AppendChild(detectNode);
            typeof(XMRInstance)
                .GetMethod("AppendXMLDetectArray", BindingFlags.Static | BindingFlags.NonPublic)
                .Invoke(null, new object[] { restoredXml, detectNode,
                                             typeof(XMRInstance).GetField("m_DetectParams", priv).GetValue(original) });
            for (var f = original.stackFrames; f != null; f = f.nextSF)
                if (f.funcName == "xmrEventDequeue") {
                    var deadline = DateTime.Parse((string)f.objArray[0]).Ticks;
                    metadata["original_deadline_utc_ticks"] = deadline;
                    if (!metadata.ContainsKey("original_outer_deadline_utc_ticks"))
                        metadata["original_outer_deadline_utc_ticks"] = deadline;
                }
            if (name == "expired_return") {
                var eager = New();
                Queue(eager, ScriptEventCode.touch_start, 9);
                capture = false;
                checks = 0;
                var got = eager.xmrEventDequeue(-1.0, 1 << ts, 0, 1 << ts, 0);
                if (got.Data.Length != 2 || got.Data[0].ToString() != ts.ToString() || got.Data[1].ToString() != "9")
                    throw new Exception("Initial expired matching call failed");
                metadata["initial_expired_matching_result"] = new[] { ts, 9 };
            }
            var resumed = New();
            wire.Position = 2;
            resumed.MigrateIn(new BinaryReader(wire, System.Text.Encoding.UTF8, true));
            Queue(resumed, ScriptEventCode.timer, 0);
            if (name != "timeout")
                Queue(resumed, name.StartsWith("background") ? ScriptEventCode.touch_end : ScriptEventCode.touch_start,
                      9);
            var queueNode = restoredXml.CreateElement("EventQueue");
            root.AppendChild(queueNode);
            foreach (var ev in resumed.m_EventQueue) {
                var en = restoredXml.CreateElement("Event");
                en.SetAttribute("Name", ev.EventName);
                typeof(XMRInstance)
                    .GetMethod("AppendXMLObjectArray", BindingFlags.Static | BindingFlags.NonPublic)
                    .Invoke(null, new object[] { restoredXml, en, ev.Params, "param" });
                typeof(XMRInstance)
                    .GetMethod("AppendXMLDetectArray", BindingFlags.Static | BindingFlags.NonPublic)
                    .Invoke(null, new object[] { restoredXml, en, ev.DetectParams });
                queueNode.AppendChild(en);
            }
            File.WriteAllText(Path.Combine(folder, "state.xml"), restoredXml.OuterXml);
            capture = false;
            checks = 0;
            resumed.CallSEH();
            var globals = new SortedDictionary<string, object>();
            foreach (var names in code.globalVarNames) {
                var a = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(resumed.glblVars);
                foreach (var p in names.Value) {
                    var v = a.GetValue(p.Key);
                    globals[p.Value] = v is int || v is string || v is double ? v : v?.ToString();
                }
            }
            metadata["api_sleep_after_resume"] = resumed.m_SleepUntil.ToString("O");
            if (name == "background_sleep" && resumed.m_SleepUntil != default(DateTime))
                throw new Exception("Original background resume replayed llSleep");
            metadata["original_resume_globals"] = globals;
            var listItems = new List<object>();
            foreach (var element in resumed.glblVars.iarLists[0].Data)
                listItems.Add(new { type = element?.GetType().FullName, value = element?.ToString() });
            metadata["original_resume_result_items"] = listItems;
            var remaining = new List<string>();
            foreach (var ev in resumed.m_EventQueue)
                remaining.Add(ev.EventName);
            metadata["remaining_queue"] = remaining;
            var detect = (DetectParams[]) typeof(XMRInstance).GetField("m_DetectParams", priv).GetValue(resumed);
            metadata["detect_name_after_resume"] = detect?[0].Name;
            metadata["original_resume_check_count"] = checks;
            metadata["event_codes"] = new { touch_start = ts, touch_end = te, timer = (int)ScriptEventCode.timer };
            var resultData = resumed.glblVars.iarLists[0].Data;
            if (resultData.Length != 2 ||
                resultData[0].ToString() != (name.StartsWith("background") ? te : ts).ToString() ||
                resultData[1].ToString() != "9")
                throw new Exception("Returned event parameter mismatch " + name);
            if (detect?[0].Name != ((name.StartsWith("background") ? "touch_end" : "touch_start") + " detect marker"))
                throw new Exception("Detect context mismatch " + name);
            if ((int)globals["count"] != 11 || (int)globals["background"] != (name.StartsWith("background") ? 7 : 0) ||
                remaining.Count != (name == "expired_return" ? 2 : 1) || remaining[0] != "timer")
                throw new Exception("Original oracle mismatch " + name);
            var hashes = new Dictionary<string, string>();
            foreach (var file in new[] { "source.lsl", "snapshot.bin", "original.yobj", "original.il.txt", "state.xml",
                                         "cache_key.txt" })
                hashes[file] = Convert.ToHexString(
                    System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(Path.Combine(folder, file))));
            hashes["original_yengine_assembly"] = Convert.ToHexString(
                System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(typeof(XMRInstance).Assembly.Location)));
            hashes["generator_source"] = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(
                File.ReadAllBytes(Path.Combine(output, "..", "Program.cs"))));
            hashes["generator_assembly"] = Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(
                File.ReadAllBytes(typeof(SuspendedProgram).Assembly.Location)));
            metadata["sha256"] = hashes;
            File.WriteAllText(Path.Combine(folder, "metadata.json"),
                              JsonSerializer.Serialize(metadata, new JsonSerializerOptions { WriteIndented = true }));
            Console.WriteLine(name + ": genuine original frames captured, original resume verified");
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
