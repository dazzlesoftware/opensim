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

public static class GlobalEventReference {
    static readonly BindingFlags priv = BindingFlags.Instance | BindingFlags.NonPublic;
    static readonly BindingFlags stat = BindingFlags.Static | BindingFlags.NonPublic;
    static readonly List<object> results = new();
    static int checks;
    static bool capturing;
    static string scenario;

    public static void Check(XMRInstAbstract instance) {
        if (++checks > 2000)
            throw new Exception("Constructor oracle exceeded scheduler check bound");
        if (instance.callMode == XMRInstAbstract.CallMode_RESTORE) {
            instance.callMode = XMRInstAbstract.CallMode_NORMAL;
            return;
        }
        if (capturing && (instance.suspendOnCheckRunTemp ||
                          (scenario.StartsWith("global_reentrant_entry") && scenario.EndsWith("capture") &&
                           !instance.doGblInit && new System.Diagnostics.StackTrace().ToString().Contains("finish")))) {
            instance.callMode = XMRInstAbstract.CallMode_SAVE;
            throw new StackCaptureException();
        }
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
        il.Emit(OpCodes.Call, typeof(GlobalEventReference).GetMethod(nameof(Check)));
        il.Emit(OpCodes.Ret);
        tb.DefineMethodOverride(method, typeof(XMRInstance).GetMethod("CheckRunWork"));
        var type = tb.CreateType();
        ScriptCodeGen.xmrInstSuperType = type;
        ScriptObjWriter.DefineInternalType("xmrsuper", type);
        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);
        ScriptConst.AddInterfaceConstants(null, api.GetFields());
        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(), type.GetField("m_ApiManager_LSL"));

        var cases = new[] {
            ("synthesized_entry", "integer value=init();integer init(){return 7;}default{touch_start(integer n){}}",
             false),
            ("constructor_detect_helpers",
             "list before=xmrEventSaveDets();string initial=init();string seen;integer complete;string " +
                 "init(){xmrEventLoadDets(" + Literal(new LSL.list(new object[] { 1 })) +
                 ");return llDetectedName(0);}default{state_entry(){seen=llDetectedName(0);complete=1;}}",
             false),
            ("global_enqueue_return",
             "list result=init();integer complete;list init(){xmrEventEnqueue([21,7]);return " +
                 "xmrEventDequeue(0.0,2097152,0,0,0);}default{state_entry(){complete=1;}touch_start(integer n){}}",
             false),
            ("global_wait_return",
             "integer calls;list result=init();integer complete;string seen;list init(){calls++;return " +
                 "xmrEventDequeue(600.0,2097152,0,0,0);}default{state_entry(){seen=llDetectedName(0);complete=1;}" +
                 "touch_" + "start(integer n){}}",
             true),
            ("global_wait_expired",
             "integer calls;list result=init();integer complete;list init(){calls++;return " +
                 "xmrEventDequeue(-1.0,2097152,0,0,0);}default{state_entry(){complete=1;}touch_start(integer n){}}",
             true),
            ("global_wait_stop_start",
             "integer calls;list result=init();integer complete;list init(){calls++;return " +
                 "xmrEventDequeue(600.0,2097152,0,0,0);}default{state_entry(){complete=1;}touch_start(integer n){}}",
             true),
            ("global_background_sleep",
             "integer calls;integer background;list result=init();integer complete;string seen;list " +
                 "init(){calls++;return " +
                 "xmrEventDequeue(600.0,536870912,0,2097152,0);}default{state_entry(){seen=llDetectedName(0);" +
                 "complete=1;}" +
                 "touch_start(integer n){background+=n;llSleep(1.0);background+=100;}touch_end(integer n){}}",
             true),
            ("global_self_reset",
             "integer calls;integer value=init();integer init(){calls++;llResetScript();calls+=10;return " +
                 "7;}default{state_entry(){calls+=100;}}",
             false),
            ("global_reentrant_entry_guard_boundary",
             "integer calls;integer value=init();integer complete;integer init(){calls++;if(calls==1){" +
                 "xmrEventEnqueue([30]);xmrEventEnqueue([21,9]);return " +
                 "llGetListLength(xmrEventDequeue(600.0,2097152,0,1073741824,0));}return 7;}" +
                 "default{state_entry(){complete++;}touch_start(integer n){}}",
             false),
            ("global_reentrant_entry_capture",
             "integer calls;integer value=init();integer complete;integer finish(integer n){while(n<0){}return n;}" +
                 "integer init(){calls++;integer " +
                 "marker=(integer)llGetObjectDesc();llSetObjectDesc((string)(marker+1));if(marker==0){" +
                 "xmrEventEnqueue([30]);xmrEventEnqueue([21,9]);return " +
                 "finish(llGetListLength(xmrEventDequeue(600.0,2097152,0,1073741824,0)));}return 7;}" +
                 "default{state_entry(){complete++;}touch_start(integer n){}}",
             true),
            ("global_state_change_boundary",
             "integer calls;integer value=init();integer init(){calls++;state other;return " +
                 "7;}default{state_entry(){calls+=100;}state_exit(){}}state other{state_entry(){calls+=1000;}}",
             false)
        };
        var allCases = new List<(string, string, bool)>(cases);
        var reentrant = Array.Find(cases, value => value.Item1 == "global_reentrant_entry_capture");
        allCases.Add(("global_reentrant_entry_complex_capture",
                      reentrant.Item2.Replace("state_entry(){complete++;}", "state_entry(){while(FALSE){}complete++;}"),
                      true));
        foreach (var spec in allCases) {
            scenario = spec.Item1;
            string folder = Path.Combine(output, spec.Item1);
            Directory.CreateDirectory(folder);
            string text = spec.Item2;
            if (spec.Item1 == "constructor_detect_helpers") {
                var seed = (XMRInstance)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(type);
                GC.SuppressFinalize(seed);
                Dets(seed, new[] { Rich(3) });
                text =
                    text.Replace("xmrEventLoadDets([1])", "xmrEventLoadDets(" + Literal(seed.xmrEventSaveDets()) + ")");
            }
            File.WriteAllText(Path.Combine(folder, "source.lsl"), text);
            var diagnostics = new List<string>();
            string sourceHash = null;
            bool accepted = false;
            object compileError = null;
            ScriptObjCode code = null;
            try {
                var first = TokenBegin.Construct("global-event-reference", null,
                                                 (token, message) => diagnostics.Add(message), text, out string digest);
                sourceHash = digest;
                var tree = first == null ? null : ScriptReduce.Reduce(first);
                using var stream = new MemoryStream();
                using (var writer = new BinaryWriter(stream, System.Text.Encoding.UTF8, true)) accepted =
                    tree != null && ScriptCodeGen.CodeGen(tree, writer, digest);
                if (accepted) {
                    File.WriteAllBytes(Path.Combine(folder, "original.yobj"), stream.ToArray());
                    stream.Position = 0;
                    var asm = new StringWriter();
                    code = new ScriptObjCode(new BinaryReader(stream), asm, null);
                    File.WriteAllText(Path.Combine(folder, "original.il.txt"), asm.ToString());
                }
            } catch (Exception error) {
                compileError = Failure(error);
            }
            if (!accepted) {
                results.Add(new { name = spec.Item1, accepted, diagnostics, compile_exception = compileError });
                continue;
            }
            var cacheKey = new System.Text.StringBuilder();
            typeof(XMRInstance)
                .GetMethod("ByteArrayToSixbitStr", stat)
                .Invoke(null, new object[] { cacheKey, System.Security.Cryptography.SHA256.HashData(
                                                           System.Text.Encoding.UTF8.GetBytes(text)) });
            File.WriteAllText(Path.Combine(folder, "cache_key.txt"), cacheKey + "\n",
                              new System.Text.UTF8Encoding(false));
            XMRInstance New() {
                var i = (XMRInstance)System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(type);
                GC.SuppressFinalize(i);
                typeof(XMRInstAbstract).GetField("heapLimit", priv).SetValue(i, 100000000);
                i.glblVars = new XMRInstArrays(i);
                i.glblVars.AllocVarArrays(code.glblSizes);
                i.m_ObjCode = code;
                i.ehArgs = Array.Empty<object>();
                i.m_StackLeft = 1000000;
                i.stackLimit = 1024;
                i.newStateCode = -1;
                i.stateCode = 0;
                i.eventCode = ScriptEventCode.state_entry;
                i.doGblInit = true;
                i.m_IState = XMRInstState.RUNNING;
                i.m_Running = true;
                i.m_QueueLock = new object();
                i.m_EventQueue = new LinkedList<EventParams>();
                typeof(XMRInstance).GetField("m_EventCounts", priv).SetValue(i, new int[(int)ScriptEventCode.Size]);
                var handled = new bool[(int)ScriptEventCode.Size];
                for (int state = 0; state < code.scriptEventHandlerTable.GetLength(0); state++)
                    for (int ev = 0; ev < handled.Length; ev++)
                        handled[ev] |= code.scriptEventHandlerTable[state, ev] != null;
                typeof(XMRInstance).GetField("m_HaveEventHandlers", priv).SetValue(i, handled);
                Dets(i, Array.Empty<DetectParams>());
                var apiObject = Activator.CreateInstance(api);
                api.GetMethod("InitXMRLSLApi").Invoke(apiObject, new object[] { i });
                type.GetField("m_ApiManager_LSL").SetValue(i, apiObject);
                type.GetField("m_LSL_Functions").SetValue(i, apiObject);
                var hostField = api.BaseType.GetField("m_host", priv);
                var host = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(hostField.FieldType);
                hostField.SetValue(apiObject, host);
                typeof(XMRInstance).GetField("m_Part").SetValue(i, host);
                var itemField = api.BaseType.GetField("m_item", priv);
                var item = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(itemField.FieldType);
                var identityProperty = itemField.FieldType.GetProperty("ItemID");
                var uuid = Activator.CreateInstance(identityProperty.PropertyType,
                                                    new object[] { "11111111-2222-3333-4444-555555555555" });
                identityProperty.SetValue(item, uuid);
                itemField.SetValue(apiObject, item);
                typeof(XMRInstance).GetField("m_Item").SetValue(i, item);
                typeof(XMRInstance).GetField("m_ItemID").SetValue(i, uuid);
                var engineField = typeof(XMRInstance).GetField("m_Engine", priv);
                var engine =
                    System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(engineField.FieldType);
                var dictionaryField = engineField.FieldType.GetField("m_InstancesDict", priv);
                var dictionary = (System.Collections.IDictionary)Activator.CreateInstance(dictionaryField.FieldType);
                dictionary.Add(uuid, i);
                dictionaryField.SetValue(engine, dictionary);
                engineField.SetValue(i, engine);
                api.BaseType.GetField("m_ScriptEngine", priv).SetValue(apiObject, engine);
                return i;
            }
            object State(XMRInstance i) {
                var globals = new SortedDictionary<string, object>();
                foreach (var names in code.globalVarNames) {
                    var array = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(i.glblVars);
                    foreach (var name in names.Value)
                        globals[name.Value] = Typed(array.GetValue(name.Key));
                }
                var frames = new List<object>();
                for (var f = i.stackFrames; f != null; f = f.nextSF)
                    frames.Add(new { function = f.funcName, call_no = f.callNo, slots = f.objArray.Length,
                                     slot_types = Array.ConvertAll(f.objArray, value => value?.GetType().FullName) });
                return new { globals,
                             event_code = i.eventCode.ToString(),
                             state_index = i.stateCode,
                             new_state_index = i.newStateCode,
                             initialize_globals = i.doGblInit,
                             running = i.Running,
                             detectors = Array.ConvertAll(Dets(i), Describe),
                             queue = Queue(i),
                             frames };
            }
            void Post(XMRInstance i, int code, int count, int detectSeed) {
                i.PostEvent(new EventParams(((ScriptEventCode)code).ToString(),
                                            new object[] { new LSL.LSLInteger(count) }, new[] { Rich(detectSeed) }));
            }
            byte[] Snapshot(XMRInstance i, string name) {
                using var stream = new MemoryStream();
                stream.WriteByte(XMRInstance.migrationVersion);
                stream.WriteByte(16);
                i.MigrateOut(new BinaryWriter(stream, System.Text.Encoding.UTF8, true));
                var data = stream.ToArray();
                File.WriteAllBytes(Path.Combine(folder, name), data);
                return data;
            }
            XMRInstance Migrate(byte[] data) {
                var i = New();
                using var stream = new MemoryStream(data);
                stream.Position = 2;
                i.MigrateIn(new BinaryReader(stream));
                return i;
            }
            string QueueXml(XMRInstance i) {
                var doc = new XmlDocument();
                var root = doc.CreateElement("EventQueue");
                doc.AppendChild(root);
                foreach (var ev in i.m_EventQueue) {
                    var node = doc.CreateElement("Event");
                    node.SetAttribute("Name", ev.EventName);
                    root.AppendChild(node);
                    typeof(XMRInstance)
                        .GetMethod("AppendXMLObjectArray", stat)
                        .Invoke(null, new object[] { doc, node, ev.Params, "param" });
                    typeof(XMRInstance)
                        .GetMethod("AppendXMLDetectArray", stat)
                        .Invoke(null, new object[] { doc, node, ev.DetectParams });
                }
                return doc.OuterXml;
            }
            void RestoreQueue(XMRInstance i, string xml) {
                var doc = new XmlDocument();
                doc.LoadXml(xml);
                i.m_EventQueue = (LinkedList<EventParams>)typeof(XMRInstance)
                                     .GetMethod("RestoreEventQueue", priv)
                                     .Invoke(i, new object[] { doc.DocumentElement });
                var counts = (int[]) typeof(XMRInstance).GetField("m_EventCounts", priv).GetValue(i);
                foreach (var ev in i.m_EventQueue)
                    counts[(int)Enum.Parse<ScriptEventCode>(ev.EventName)]++;
            }
            var instance = New();
            if (spec.Item1 == "global_background_sleep") {
                Post(instance, 21, 7, 1);
                Post(instance, 29, 9, 2);
            }
            capturing = spec.Item3;
            checks = 0;
            bool suspended = false;
            object initialError = null;
            try {
                instance.CallSEH();
            } catch (StackCaptureException) {
                suspended = true;
            } catch (Exception error) {
                initialError = Failure(error);
            }
            var initial = State(instance);
            object afterLoad = null, afterStop = null, afterRestartWait = null, final = null, resumeError = null;
            bool migrated = false;
            if (suspended) {
                byte[] snapshot = Snapshot(instance, "snapshot.bin");
                string queueXml = QueueXml(instance);
                File.WriteAllText(Path.Combine(folder, "event-queue.xml"), queueXml);
                var restored = Migrate(snapshot);
                RestoreQueue(restored, queueXml);
                migrated = true;
                afterLoad = State(restored);
                if (spec.Item1 == "global_wait_stop_start") {
                    Post(restored, 21, 9, 4);
                    restored.m_IState = XMRInstState.SUSPENDED;
                    typeof(XMRInstance).GetField("m_SuspendCount", priv).SetValue(restored, 1);
                    restored.Running = false;
                    afterStop = State(restored);
                    restored.Running = true;
                    capturing = true;
                    checks = 0;
                    try {
                        restored.CallSEH();
                    } catch (StackCaptureException) {
                        afterRestartWait = State(restored);
                        snapshot = Snapshot(restored, "snapshot-after-stop-start.bin");
                    } catch (Exception error) {
                        resumeError = Failure(error);
                    }
                    var next = Migrate(snapshot);
                    Post(next, 21, 11, 5);
                    restored = next;
                } else if (spec.Item1.StartsWith("global_wait_"))
                    Post(restored, 21, 9, 4);
                capturing = false;
                checks = 0;
                try {
                    restored.CallSEH();
                } catch (Exception error) {
                    resumeError = Failure(error);
                }
                final = State(restored);
            } else
                final = initial;
            string Sha(string file) =>
                Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(file)))
                    .ToLowerInvariant();
            var hashes = new Dictionary<string, string>();
            foreach (string file in Directory.GetFiles(folder))
                if (!file.EndsWith("metadata.json"))
                    hashes[Path.GetFileName(file)] = Sha(file);
            hashes["generator_source"] = Sha(generator);
            hashes["generator_assembly"] = Sha(typeof(GlobalEventReference).Assembly.Location);
            hashes["original_yengine_assembly"] = Sha(typeof(XMRInstance).Assembly.Location);
            foreach (string file in new[] { "MMRScriptCodeGen.cs", "XMRInstAbstract.cs", "XMRInstCtor.cs",
                                            "XMRInstBackend.cs", "XMRInstRun.cs", "XMRInstMisc.cs" })
                hashes["original_source_" + file] =
                    Sha(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine", file));
            var record = new {
                name = spec.Item1,
                accepted,
                diagnostics,
                compile_exception = compileError,
                source_hash = sourceHash,
                compiled_version = ScriptCodeGen.COMPILED_VERSION_VALUE,
                suspended,
                migrated,
                initial_exception = initialError,
                initial,
                after_load = afterLoad,
                after_stop = afterStop,
                after_restart_wait = afterRestartWait,
                resume_exception = resumeError,
                final,
                incomplete_scene_resource_boundary =
                    spec.Item1 == "global_state_change_boundary" && initialError != null,
                scheduler_guard_boundary =
                    spec.Item1 == "global_reentrant_entry_guard_boundary" && initialError != null,
                stop_scheduler_context =
                    spec.Item1 == "global_wait_stop_start"
                        ? "Scheduler places captured frame in SUSPENDED with suspendCount=1; unchanged Running " +
                              "setter clears queue and resumes flag; original CallSEH is driven directly without " +
                              "engine worker threads."
                        : null,
                methodology = "Actual unchanged CodeGen/XMRInstance helpers/API/engine instance " +
                              "lookup/PostEvent/Running setter/MigrateOut/MigrateIn/CallSEH. Only CheckRunWork " +
                              "scheduler capture is overridden. Scene resource cleanup is not replaced; " +
                              "missing context is recorded as boundary exception, not successful state change.",
                sha256 = hashes
            };
            File.WriteAllText(Path.Combine(folder, "metadata.json"),
                              JsonSerializer.Serialize(record, new JsonSerializerOptions { WriteIndented = true }));
            results.Add(record);
            Console.WriteLine(spec.Item1 + ": accepted=" + accepted + " captured=" + suspended +
                              " migrated=" + migrated);
        }
        File.WriteAllText(Path.Combine(output, "metadata.json"),
                          JsonSerializer.Serialize(results, new JsonSerializerOptions { WriteIndented = true }));
    }
}
