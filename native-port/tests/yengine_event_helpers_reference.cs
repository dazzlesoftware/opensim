// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Invokes unchanged original backend helpers, PostEvent and queue XML codecs.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Reflection.Emit;
using System.Text.Json;
using System.Xml;
using OpenSim.Region.ScriptEngine.Yengine;
using OpenSim.Region.ScriptEngine.Shared;
using LSL = OpenSim.Region.ScriptEngine.Shared.LSL_Types;

public static class EventHelpersReference {
    static readonly BindingFlags priv = BindingFlags.Instance | BindingFlags.NonPublic;
    static readonly BindingFlags stat = BindingFlags.Static | BindingFlags.NonPublic;
    static readonly List<object> results = new();
    static int checks;

    public static void Check(XMRInstAbstract instance) {
        if (++checks > 100)
            throw new Exception("Unexpected scheduler work in direct helper oracle");
    }

    static object Typed(object value) {
        string clr = value?.GetType().FullName;
        if (value == null)
            return new { type = "null", clr, value = (object)null };
        if (value is int i)
            return new { type = "integer", clr, value = i };
        if (value is LSL.LSLInteger li)
            return new { type = "integer", clr, value = li.value };
        if (value is double d)
            return new { type = "float", clr, value = d };
        if (value is LSL.LSLFloat lf)
            return new { type = "float", clr, value = lf.value };
        if (value is string s)
            return new { type = "string", clr, value = s };
        if (value is LSL.LSLString ls)
            return new { type = "string", clr, value = ls.m_string };
        if (value is LSL.key k)
            return new { type = "key", clr, value = k.ToString() };
        if (value is LSL.Vector3 v)
            return new { type = "vector", clr, value = new[] { v.x, v.y, v.z } };
        if (value is LSL.Quaternion q)
            return new { type = "rotation", clr, value = new[] { q.x, q.y, q.z, q.s } };
        if (value is LSL.list l)
            return new { type = "list", clr, value = Array.ConvertAll(l.Data, Typed) };
        return new { type = "unsupported", clr, value = value.ToString() };
    }

    static string Literal(object value) {
        string Float(double n) {
            string s = n.ToString("R", CultureInfo.InvariantCulture);
            return s.Contains('.') || s.Contains('E') || s.Contains('e') ? s : s + ".0";
        }
        if (value is int i)
            return i.ToString(CultureInfo.InvariantCulture);
        if (value is LSL.LSLInteger li)
            return li.value.ToString(CultureInfo.InvariantCulture);
        if (value is string s)
            return JsonSerializer.Serialize(s);
        if (value is LSL.LSLString ls)
            return JsonSerializer.Serialize(ls.m_string);
        if (value is LSL.key k)
            return "(key)" + JsonSerializer.Serialize(k.ToString());
        if (value is double d)
            return Float(d);
        if (value is LSL.LSLFloat lf)
            return Float(lf.value);
        if (value is LSL.Vector3 v)
            return "<" + Float(v.x) + "," + Float(v.y) + "," + Float(v.z) + ">";
        if (value is LSL.Quaternion q)
            return "<" + Float(q.x) + "," + Float(q.y) + "," + Float(q.z) + "," + Float(q.s) + ">";
        if (value is LSL.list l)
            return "[" + string.Join(",", Array.ConvertAll(l.Data, Literal)) + "]";
        throw new Exception("No source literal for " + value?.GetType());
    }

    static DetectParams Rich(int seed) {
        var d = new DetectParams();
        foreach (var pair in new[] { ("Key", "11111111-2222-3333-4444-555555555555"),
                                     ("Owner", "22222222-3333-4444-5555-666666666666"),
                                     ("Group", "33333333-4444-5555-6666-777777777777") }) {
            var field = typeof(DetectParams).GetField(pair.Item1);
            field.SetValue(d, Activator.CreateInstance(field.FieldType, new object[] { pair.Item2 }));
        }
        d.Name = "original rich detector " + seed;
        d.LinkNum = seed + 2;
        d.Type = seed + 8;
        d.OffsetPos = new LSL.Vector3(seed + .125, seed + .25, seed + .5);
        d.Position = new LSL.Vector3(seed + 10.00000003, seed + 11.00000003, seed + 12.00000003);
        d.Rotation = new LSL.Quaternion(.125, .25, .5, .875);
        d.Velocity = new LSL.Vector3(seed + 20.00000003, seed + 21.00000003, seed + 22.00000003);
        int i = 0;
        foreach (string name in new[] { "touchST", "touchNormal", "touchBinormal", "touchPos", "touchUV" }) {
            double n = 1.00000003 + seed + i++ * 3;
            typeof(DetectParams).GetField(name, priv).SetValue(d, new LSL.Vector3(n, n + 1, n + 2));
        }
        typeof(DetectParams).GetField("touchFace", priv).SetValue(d, seed + 3);
        return d;
    }

    static DetectParams[] Dets(XMRInstance i) =>
        (DetectParams[]) typeof(XMRInstance).GetField("m_DetectParams", priv).GetValue(i);
    static void Dets(XMRInstance i,
                     DetectParams[] d) => typeof(XMRInstance).GetField("m_DetectParams", priv).SetValue(i, d);
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

    static object Queue(XMRInstance i) {
        var a = new List<object>();
        foreach (var e in i.m_EventQueue)
            a.Add(new { name = e.EventName, arguments = Array.ConvertAll(e.Params, Typed),
                        detectors = Array.ConvertAll(e.DetectParams, Describe) });
        return a;
    }

    static object Failure(Exception e) => new { type = e.GetType().FullName, message = e.Message };

    static void Main(string[] args) {
        string bin = Path.GetFullPath(args[0]), output = Path.GetFullPath(args[1]),
               generator = Path.GetFullPath(args[2]), sourceRoot = Path.GetFullPath(args[3]);
        Directory.CreateDirectory(output);
        AppDomain.CurrentDomain.AssemblyResolve += (_, r) => {
            string p = Path.Combine(bin, new AssemblyName(r.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        Run(output, generator, sourceRoot);
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void Run(string output, string generator, string sourceRoot) {
        var api = typeof(XMRInstance).Assembly.GetType("OpenSim.Region.ScriptEngine.Yengine.XMRLSL_Api", true);
        var mb =
            AssemblyBuilder.DefineDynamicAssembly(new AssemblyName("OriginalHelperContext"), AssemblyBuilderAccess.Run)
                .DefineDynamicModule("Context");
        var tb = mb.DefineType("EventHelpersInstance", TypeAttributes.Public, typeof(XMRInstance));
        var ctor = tb.DefineConstructor(MethodAttributes.Public, CallingConventions.Standard, Type.EmptyTypes);
        var ci = ctor.GetILGenerator();
        ci.Emit(OpCodes.Ldnull);
        ci.Emit(OpCodes.Throw);
        tb.DefineField("m_ApiManager_LSL", api, FieldAttributes.Public);
        var method = tb.DefineMethod("CheckRunWork", MethodAttributes.Public | MethodAttributes.Virtual, typeof(void),
                                     Type.EmptyTypes);
        var il = method.GetILGenerator();
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Call, typeof(EventHelpersReference).GetMethod(nameof(Check)));
        il.Emit(OpCodes.Ret);
        tb.DefineMethodOverride(method, typeof(XMRInstance).GetMethod("CheckRunWork"));
        var type = tb.CreateType();
        ScriptCodeGen.xmrInstSuperType = type;
        ScriptObjWriter.DefineInternalType("xmrsuper", type);
        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);
        ScriptConst.AddInterfaceConstants(null, api.GetFields());
        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(), type.GetField("m_ApiManager_LSL"));
        string source = "integer received;default{state_entry(){}touch_start(integer " +
                        "n){received=n;}timer(){}listen(integer c,string n,key k,string m){}attach(key " +
                        "k){}state_exit(){}}state other{changed(integer f){}touch_end(integer n){}}";
        File.WriteAllText(Path.Combine(output, "source.lsl"), source);
        var errors = new List<string>();
        var tokens =
            TokenBegin.Construct("event-helpers-reference", null, (t, m) => errors.Add(m), source, out string hash);
        var reduced = tokens == null ? null : ScriptReduce.Reduce(tokens);
        using var codebytes = new MemoryStream();
        using (var bw = new BinaryWriter(
                   codebytes, System.Text.Encoding.UTF8,
                   true)) if (reduced == null ||
                              !ScriptCodeGen.CodeGen(reduced, bw, hash)) throw new Exception(string.Join("\n", errors));
        File.WriteAllBytes(Path.Combine(output, "original.yobj"), codebytes.ToArray());
        codebytes.Position = 0;
        var dis = new StringWriter();
        var code = new ScriptObjCode(new BinaryReader(codebytes), dis, null);
        File.WriteAllText(Path.Combine(output, "original.il.txt"), dis.ToString());
        XMRInstance New() {
            var i = (XMRInstance)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(type);
            typeof(XMRInstAbstract).GetField("heapLimit", priv).SetValue(i, 100000000);
            i.glblVars = new XMRInstArrays(i);
            i.glblVars.AllocVarArrays(code.glblSizes);
            i.m_ObjCode = code;
            i.ehArgs = Array.Empty<object>();
            i.m_StackLeft = 1000000;
            i.stackLimit = 1024;
            i.newStateCode = -1;
            i.stateCode = 0;
            i.m_IState = XMRInstState.RUNNING;
            i.m_Running = true;
            typeof(XMRInstAbstract).GetField("heapLimit", priv).SetValue(i, 100000000);
            i.m_QueueLock = new object();
            i.m_EventQueue = new LinkedList<EventParams>();
            typeof(XMRInstance).GetField("m_EventCounts", priv).SetValue(i, new int[(int)ScriptEventCode.Size]);
            var handled = new bool[(int)ScriptEventCode.Size];
            for (int state = 0; state < code.scriptEventHandlerTable.GetLength(0); state++)
                for (int eventCode = 0; eventCode < handled.Length; eventCode++)
                    handled[eventCode] |= code.scriptEventHandlerTable[state, eventCode] != null;
            typeof(XMRInstance).GetField("m_HaveEventHandlers", priv).SetValue(i, handled);
            var apiObject = Activator.CreateInstance(api);
            api.GetMethod("InitXMRLSLApi").Invoke(apiObject, new object[] { i });
            type.GetField("m_ApiManager_LSL").SetValue(i, apiObject);
            type.GetField("m_LSL_Functions").SetValue(i, apiObject);
            var hostField = api.BaseType.GetField("m_host", priv);
            var hostPart = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(hostField.FieldType);
            var ownerProperty = hostField.FieldType.GetProperty("OwnerID");
            ownerProperty.SetValue(hostPart,
                                   Activator.CreateInstance(ownerProperty.PropertyType,
                                                            new object[] { "11111111-2222-3333-4444-555555555555" }));
            hostField.SetValue(apiObject, hostPart);
            Dets(i, new[] { Rich(0), Rich(1) });
            return i;
        }
        object Dequeue(XMRInstance i) {
            if (i.m_EventQueue.Count == 0)
                return null;
            var code = (int)Enum.Parse<ScriptEventCode>(i.m_EventQueue.First.Value.EventName);
            return Typed(i.xmrEventDequeue(-1, code < 32 ? 1 << code : 0, code >= 32 ? 1 << (code - 32) : 0, 0, 0));
        }
        var initial = New();
        var rich = initial.xmrEventSaveDets();
        initial.xmrEventLoadDets(rich);
        var narrowed = initial.xmrEventSaveDets();
        var richInfo =
            new { detectors = Array.ConvertAll(new[] { Rich(0), Rich(1) }, Describe), rich_list_source = Literal(rich),
                  save_result = Typed(rich), load_save_roundtrip = Typed(narrowed),
                  loaded_detectors = Array.ConvertAll(Dets(initial), Describe) };
        var choices = new(object, string)[] { (new LSL.LSLInteger(1), "LSLInteger"),
                                              (1, "SystemInt32"),
                                              (new LSL.LSLFloat(1.0), "LSLFloat"),
                                              (1.0, "SystemDouble"),
                                              (new LSL.LSLString("1"), "LSLString"),
                                              ("1", "SystemString"),
                                              (new LSL.key("1"), "LSLKey"),
                                              (true, "SystemBool"),
                                              (null, "null"),
                                              (new LSL.Vector3(1, 2, 3), "vector"),
                                              (new LSL.list(new object[] { 1 }), "list") };
        foreach (var choice in choices)
            foreach (string conversion in new[] { "ListInt", "ListStr" }) {
                object result = null, error = null;
                try {
                    result = Typed(conversion == "ListInt" ? (object)XMRInstance.ListInt(choice.Item1)
                                                           : XMRInstance.ListStr(choice.Item1));
                } catch (Exception e) {
                    error = Failure(e);
                }
                results.Add(new { name = conversion + "_" + choice.Item2, input = Typed(choice.Item1), result,
                                  exception = error });
            }
        var loadCases = new List<(string, object[])> { ("empty", Array.Empty<object>()),
                                                       ("version_only", new object[] { new LSL.LSLInteger(1) }),
                                                       ("version_two", new object[] { 2 }),
                                                       ("wrong_count", new object[] { 1, 0 }) };
        foreach (var choice in choices) {
            var data = (object[])rich.Data.Clone();
            data[0] = choice.Item1;
            loadCases.Add(("version_" + choice.Item2, data));
        }
        foreach (var replacement in new(object, string)[] {
                     ("not-a-uuid", "bad_uuid"), (new LSL.key("11111111-2222-3333-4444-555555555555"), "key_as_uuid"),
                     (null, "null_uuid"), (1, "integer_as_uuid")
                 }) {
            var data = (object[])rich.Data.Clone();
            data[1] = replacement.Item1;
            loadCases.Add((replacement.Item2, data));
        }
        foreach (var uuid in new[] { ("", "empty_uuid"), ("11111111222233334444555555555555", "compact_uuid"),
                                     ("11111111-2222-3333-4444-555555555555suffix", "suffix_uuid"),
                                     (" 11111111-2222-3333-4444-555555555555", "leading_space_uuid"),
                                     ("{11111111-2222-3333-4444-555555555555}", "braced_uuid"),
                                     ("AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE", "uppercase_uuid") }) {
            var data = (object[])rich.Data.Clone();
            data[1] = uuid.Item1;
            loadCases.Add((uuid.Item2, data));
        }
        foreach (var slot in new[] { 2, 3, 5, 7, 8, 9, 11, 16, 17 + 7 }) {
            var data = (object[])rich.Data.Clone();
            data[slot] = new LSL.LSLString("wrong-type");
            loadCases.Add(("wrong_type_slot_" + slot, data));
        }
        foreach (var spec in loadCases) {
            var i = New();
            var originalDets = Dets(i);
            object error = null;
            try {
                i.xmrEventLoadDets(new LSL.list(spec.Item2));
            } catch (Exception e) {
                error = Failure(e);
            }
            results.Add(
                new { name = "LoadDets_" + spec.Item1, input = Typed(new LSL.list(spec.Item2)), exception = error,
                      atomic_reference_preserved = Object.ReferenceEquals(originalDets, Dets(i)),
                      after = Typed(i.xmrEventSaveDets()), loaded_detectors = Array.ConvertAll(Dets(i), Describe) });
        }
        var enqueues = new List<(string, object[])> {
            ("empty", Array.Empty<object>()),
            ("invalid_code", new object[] { 999, 7 }),
            ("negative_code", new object[] { -1, 7 }),
            ("unhandled", new object[] { 4, 7 }),
            ("no_args", new object[] { 21 }),
            ("wrong_type", new object[] { 21, new LSL.LSLString("custom") }),
            ("arbitrary_args", new object[] { 21, new LSL.LSLString("custom"), new LSL.Vector3(1, 2, 3),
                                              new LSL.list(new object[] { new LSL.LSLInteger(19) }) }),
            ("extra_args", new object[] { 21, new LSL.LSLInteger(7), new LSL.LSLString("extra") }),
            ("any_state", new object[] { 33, new LSL.LSLInteger(256) }),
            ("attach_no_args", new object[] { 0 }),
            ("null_argument", new object[] { 21, null }),
            ("system_arguments", new object[] { 21, 7, 1.25, "system string" }),
            ("key_argument", new object[] { 21, new LSL.key("11111111-2222-3333-4444-555555555555") })
        };
        foreach (var choice in choices) {
            var data = new object[] { choice.Item1, new LSL.LSLInteger(7) };
            if (choice.Item1 is int)
                data[0] = 21;
            else if (choice.Item1 is LSL.LSLInteger)
                data[0] = new LSL.LSLInteger(21);
            enqueues.Add(("code_" + choice.Item2, data));
        }
        foreach (var spec in enqueues) {
            var i = New();
            object error = null, returned = null, migrationError = null, migratedQueue = null, dispatchError = null;
            string queueXml = null;
            try {
                i.xmrEventEnqueue(new LSL.list(spec.Item2));
            } catch (Exception e) {
                error = Failure(e);
            }
            var queued = Queue(i);
            if (i.m_EventQueue.Count > 0) {
                var first = i.m_EventQueue.First.Value;
                try {
                    var doc = new XmlDocument();
                    var root = doc.CreateElement("EventQueue");
                    doc.AppendChild(root);
                    var n = doc.CreateElement("Event");
                    n.SetAttribute("Name", first.EventName);
                    root.AppendChild(n);
                    typeof(XMRInstance)
                        .GetMethod("AppendXMLObjectArray", stat)
                        .Invoke(null, new object[] { doc, n, first.Params, "param" });
                    typeof(XMRInstance)
                        .GetMethod("AppendXMLDetectArray", stat)
                        .Invoke(null, new object[] { doc, n, first.DetectParams });
                    queueXml = doc.OuterXml;
                    var restored = (LinkedList<EventParams>)typeof(XMRInstance)
                                       .GetMethod("RestoreEventQueue", priv)
                                       .Invoke(i, new object[] { root });
                    var clone = New();
                    clone.m_EventQueue = restored;
                    migratedQueue = Queue(clone);
                } catch (Exception e) {
                    migrationError = Failure(e is TargetInvocationException tie ? tie.InnerException : e);
                }
                var dispatch = New();
                dispatch.ehArgs = first.Params;
                dispatch.eventCode = Enum.Parse<ScriptEventCode>(first.EventName);
                Dets(dispatch, first.DetectParams);
                try {
                    dispatch.CallSEH();
                } catch (Exception e) {
                    dispatchError = Failure(e);
                }
                returned = Dequeue(i);
            }
            results.Add(new { name = "Enqueue_" + spec.Item1, input = Typed(new LSL.list(spec.Item2)),
                              exception = error, queue = queued, returned,
                              detect_after_dequeue = Array.ConvertAll(Dets(i), Describe), queue_xml = queueXml,
                              migrated_queue = migratedQueue, migration_exception = migrationError,
                              dispatch_exception = dispatchError });
        }
        foreach (var value in new(object, string)[] { (new LSL.LSLInteger(256), "LSLInteger"), (256, "SystemInt32"),
                                                      (new LSL.LSLInteger(0), "zeroLSLInteger"),
                                                      (0, "zeroSystemInt32") }) {
            var i = New();
            typeof(XMRInstance).GetField("m_minEventDelay", priv).SetValue(i, 0.5);
            object error = null;
            try {
                i.xmrEventEnqueue(new LSL.list(new object[] { 33, value.Item1 }));
            } catch (Exception e) {
                error = Failure(e);
            }
            results.Add(new { name = "Enqueue_changed_minimum_delay_" + value.Item2,
                              input = Typed(new LSL.list(new object[] { 33, value.Item1 })), exception = error,
                              queue = Queue(i) });
        }
        var cap = New();
        for (int n = 0; n < 70; n++)
            cap.xmrEventEnqueue(new LSL.list(new object[] { 21, new LSL.LSLInteger(n) }));
        cap.xmrEventEnqueue(new LSL.list(new object[] { 2 }));
        cap.xmrEventEnqueue(new LSL.list(new object[] { 2 }));
        results.Add(new { name = "Enqueue_per_event_cap", queue = Queue(cap), count = cap.m_EventQueue.Count });
        var alias = New();
        var oldDet = Dets(alias);
        var nested = new LSL.list(new object[] { new LSL.LSLInteger(1) });
        var dataAlias = new object[] { 21, new LSL.LSLInteger(7), nested };
        alias.xmrEventEnqueue(new LSL.list(dataAlias));
        dataAlias[1] = new LSL.LSLInteger(99);
        nested.Data[0] = new LSL.LSLInteger(42);
        alias.xmrEventLoadDets(new LSL.list(new object[] { 1 }));
        bool oldReference = Object.ReferenceEquals(alias.m_EventQueue.First.Value.DetectParams, oldDet);
        oldDet[0].Name = "old queued detector mutated";
        results.Add(new { name = "Enqueue_shallow_alias_and_detect_capture", queue = Queue(alias),
                          returned = Dequeue(alias), old_detect_array_captured = oldReference,
                          current_detectors = Array.ConvertAll(Dets(alias), Describe) });
        var scriptCases = new List<object>();
        string scriptFolder = Path.Combine(output, "script_sources");
        Directory.CreateDirectory(scriptFolder);
        string helperSource = source;
        var helperCode = code;
        var scriptInputs = new List<(string, string, bool)> {
            ("key_literal_list",
             "list output;default{state_entry(){output=[(key)\"11111111-2222-3333-4444-555555555555\"];}}", true),
            ("key_variable_list",
             "key k=\"11111111-2222-3333-4444-555555555555\";list output;default{state_entry(){output=[k];}}", true),
            ("key_literal_cast_list",
             "list output;default{state_entry(){output=(list)((key)\"11111111-2222-3333-4444-555555555555\");}}", true),
            ("key_variable_concat_right",
             "key k=\"11111111-2222-3333-4444-555555555555\";list output;default{state_entry(){output=[]+k;}}", true),
            ("key_variable_concat_left",
             "key k=\"11111111-2222-3333-4444-555555555555\";list output;default{state_entry(){output=k+[];}}", true),
            ("key_user_function_list",
             "list output;key owner(){return " +
                 "(key)\"11111111-2222-3333-4444-555555555555\";}default{state_entry(){output=[owner()];}}",
             true),
            ("key_direct_api_list", "list output;default{state_entry(){output=[llGetOwner()];}}", true),
            ("key_assigned_api_list", "list output;default{state_entry(){key k=llGetOwner();output=[k];}}", true),
            ("key_direct_api_cast_list", "list output;default{state_entry(){output=(list)llGetOwner();}}", true),
            ("key_direct_api_concat", "list output;default{state_entry(){output=[]+llGetOwner();}}", true),
            ("nested_minimal_literal", "default{state_entry(){xmrEventLoadDets([[1]]);}}", true),
            ("nested_list_variable", "default{state_entry(){list nested=[1];xmrEventLoadDets([nested]);}}", true)
        };
        scriptInputs.Add(("nested_output", "list output;default{state_entry(){output=[[1],[2]];}}", true));
        string exactNested = Path.Combine(
            sourceRoot,
            "native-port/tests/yengine_event_helpers_fixture/script_sources/LoadDets_version_list_exact.lsl");
        if (!File.Exists(exactNested))
            throw new Exception("The preserved exact native-failure source is required for this reference");
        scriptInputs.Add(("LoadDets_version_list_exact", File.ReadAllText(exactNested), true));
        foreach (var spec in scriptInputs) {
            string text = spec.Item2.Replace("\\\"", "\"");
            string file = Path.Combine(scriptFolder, spec.Item1 + ".lsl");
            File.WriteAllText(file, text);
            var diagnostics = new List<string>();
            bool accepted = false;
            object compileError = null, executionError = null;
            object globals = null;
            try {
                var first = TokenBegin.Construct("event-helper-script-reference", null,
                                                 (token, message) => diagnostics.Add(message), text, out string digest);
                var tree = first == null ? null : ScriptReduce.Reduce(first);
                using var stream = new MemoryStream();
                using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8, true)) accepted =
                    tree != null && ScriptCodeGen.CodeGen(tree, writer, digest);
                if (accepted) {
                    File.WriteAllBytes(Path.ChangeExtension(file, ".yobj"), stream.ToArray());
                    if (spec.Item3) {
                        stream.Position = 0;
                        code = new ScriptObjCode(new BinaryReader(stream), null, null);
                        var instance = New();
                        instance.doGblInit = true;
                        checks = 0;
                        try {
                            instance.eventCode = ScriptEventCode.state_entry;
                            instance.CallSEH();
                        } catch (Exception error) {
                            executionError = Failure(error);
                        }
                        var values = new SortedDictionary<string, object>();
                        foreach (var names in code.globalVarNames) {
                            var array = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(instance.glblVars);
                            foreach (var name in names.Value)
                                values[name.Value] = Typed(array.GetValue(name.Key));
                        }
                        globals = values;
                    }
                }
            } catch (Exception error) {
                compileError = Failure(error);
            }
            scriptCases.Add(new { name = spec.Item1, source_file = "script_sources/" + spec.Item1 + ".lsl",
                                  source = text, accepted, diagnostics, compile_exception = compileError,
                                  execution_requested = spec.Item3, execution_exception = executionError, globals });
        }
        code = helperCode;
        string Sha(string path) =>
            Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(path)))
                .ToLowerInvariant();
        var hashes = new Dictionary<string, string>();
        foreach (string name in new[] { "source.lsl", "original.yobj", "original.il.txt" })
            hashes[name] = Sha(Path.Combine(output, name));
        foreach (string file in Directory.GetFiles(scriptFolder))
            hashes["script_sources/" + Path.GetFileName(file)] = Sha(file);
        hashes["generator_source"] = Sha(generator);
        hashes["generator_assembly"] = Sha(typeof(EventHelpersReference).Assembly.Location);
        hashes["original_yengine_assembly"] = Sha(typeof(XMRInstance).Assembly.Location);
        foreach (string name in new[] { "XMRInstBackend.cs", "XMRInstMisc.cs", "XMRInstRun.cs", "XMRInstCapture.cs",
                                        "XMRInstCtor.cs", "MMRScriptCodeGen.cs", "MMRScriptTokenize.cs" })
            hashes["original_source_" + name] =
                Sha(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine", name));
        var metadata = new {
            source_hash = code.sourceHash,
            compiled_version = ScriptCodeGen.COMPILED_VERSION_VALUE,
            methodology =
                "Unchanged actual XMRInstance backend/ListInt/ListStr/PostEvent/direct generated CallSEH and " +
                "original XML queue serializer/parser. Only CheckRunWork override is a bounded scheduler guard; " +
                "instances use real handler table any-state union, Running state and actual queues/counts.",
            rich = richInfo,
            cases = results,
            script_compile_cases = scriptCases,
            sha256 = hashes
        };
        File.WriteAllText(Path.Combine(output, "metadata.json"),
                          JsonSerializer.Serialize(metadata, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine("Original event helpers: " + results.Count + " measured cases");
    }
}
