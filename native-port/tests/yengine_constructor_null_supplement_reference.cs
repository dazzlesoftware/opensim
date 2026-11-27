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

public static class ConstructorNullSupplementReference {
    static readonly BindingFlags priv = BindingFlags.Instance | BindingFlags.NonPublic;
    static readonly BindingFlags stat = BindingFlags.Static | BindingFlags.NonPublic;
    static readonly List<object> results = new();
    static int checks;
    static string scenario;

    static Action<XMRInstance> originalCheck;

    public static void Check(XMRInstAbstract instance) {
        if (++checks > 2000)
            throw new Exception("Original scheduler oracle bound");
        originalCheck((XMRInstance)instance);
    }

    static object Typed(object value) {
        string clr = value?.GetType().FullName;
        if (value == null)
            return new { type = "null", clr, value = (object)null };
        if (value is int i)
            return new { type = "integer", clr, value = i };
        if (value is LSL.LSLInteger li)
            return new { type = "integer", clr, value = li.value };
        if (value is char ch) return new { type="char",clr,value=(int)ch,utf16=(int)ch };
        if (value is double d)
            return new { type = "float", clr, value = d };
        if (value is LSL.LSLFloat lf)
            return new { type = "float", clr, value = lf.value };
        if (value is string s)
            return new { type = "string", clr, value = s, utf16=Array.ConvertAll(s.ToCharArray(), x=>(int)x) };
        if (value is LSL.LSLString ls)
            return new { type = "string", clr, value = ls.m_string, utf16=ls.m_string==null?null:Array.ConvertAll(ls.m_string.ToCharArray(), x=>(int)x) };
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

    static object Failure(Exception e) => new { type = e.GetType().FullName, message = e.Message,
                                                stack = e.StackTrace };

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
        var dm = new DynamicMethod("OriginalCheck", typeof(void), new[] { typeof(XMRInstance) },
                                   typeof(ConstructorNullSupplementReference), true);
        var cil = dm.GetILGenerator();
        cil.Emit(OpCodes.Ldarg_0);
        cil.Emit(OpCodes.Call, typeof(XMRInstance).GetMethod("CheckRunWork"));
        cil.Emit(OpCodes.Ret);
        originalCheck = (Action<XMRInstance>)dm.CreateDelegate(typeof(Action<XMRInstance>));
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
        il.Emit(OpCodes.Call, typeof(ConstructorNullSupplementReference).GetMethod(nameof(Check)));
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
        allCases.Clear();
        allCases.Add(("null_vs_completed_equal", "list first=read();list later;integer result;integer after;list read(){return later;}default{state_entry(){result=first==later;after=1;}}", false));
        allCases.Add(("null_vs_completed_not_equal", "list first=read();list later;integer result;integer after;list read(){return later;}default{state_entry(){result=first!=later;after=1;}}", false));
        allCases.Add(("null_self_equal", "list first=read();list later;integer result;integer after;list read(){return later;}default{state_entry(){result=first==first;after=1;}}", false));
        allCases.Add(("nested_null_list", "list first=read();list later;list result;integer after;list read(){return later;}default{state_entry(){result=[first];after=1;}}", false));
        allCases.Add(("nested_null_list_cast", "list first=read();list later;string result;integer after;list read(){return later;}default{state_entry(){result=(string)[first];after=1;}}", false));
        allCases.Add(("forward_rotation_capture", "rotation first=read();rotation later;integer after;rotation read(){return later;}default{state_entry(){llSleep(3.0);after=1;}}", true));
        allCases.Add(("forward_key_capture", "key first=read();key later;integer after;key read(){return later;}default{state_entry(){llSleep(3.0);after=1;}}", true));
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
                var host = Activator.CreateInstance(hostField.FieldType);
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
                // Bind unchanged StateChange to actual original empty scene/plugin resources.
                var scenes = Assembly.LoadFrom(Path.Combine(
                    Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location), "OpenSim.Region.Framework.dll"));
                var sceneType = scenes.GetType("OpenSim.Region.Framework.Scenes.Scene", true);
                var sceneCtor = Array.Find(sceneType.GetConstructors(),
                                           c => c.GetParameters().Length == 1 &&
                                                c.GetParameters()[0].ParameterType.Name == "RegionInfo");
                var region = Activator.CreateInstance(sceneCtor.GetParameters()[0].ParameterType);
                var scene = sceneCtor.Invoke(new[] { region });
                engineField.FieldType.GetField("m_Scene").SetValue(engine, scene);
                var shared =
                    Assembly.LoadFrom(Path.Combine(Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location),
                                                   "OpenSim.Region.ScriptEngine.Shared.Api.dll"));
                var managerType = shared.GetType("OpenSim.Region.ScriptEngine.Shared.Api.AsyncCommandManager", true);
                var manager = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(managerType);
                managerType.GetField("m_ScriptEngine").SetValue(manager, engine);
                foreach (string fieldName in new[] { "m_Dataserver", "m_ScriptTimer", "m_SensorRepeat" }) {
                    var dictionaryResource =
                        (System.Collections.IDictionary)managerType.GetField(fieldName, stat).GetValue(null);
                    var pluginType = dictionaryResource.GetType().GenericTypeArguments[1];
                    object plugin;
                    if (fieldName == "m_ScriptTimer")
                        plugin = Activator.CreateInstance(pluginType, new[] { manager });
                    else {
                        // Empty resources: initialize original collection/lock fields; do not replace backend methods or start scheduler workers.
                        plugin = System.Runtime.CompilerServices.RuntimeHelpers.GetUninitializedObject(pluginType);
                        foreach (string field in fieldName == "m_Dataserver"
                                     ? new[] { "DataserverRequests" }
                                     : new[] { "SenseRepeaters", "SenseRepeatListLock" }) {
                            var info = pluginType.GetField(field, priv);
                            info.SetValue(plugin, info.FieldType == typeof(object)
                                                      ? new object()
                                                      : Activator.CreateInstance(info.FieldType));
                        }
                    }
                    dictionaryResource.Add(engine, plugin);
                }
                api.GetField("acm").SetValue(apiObject, manager);
                typeof(XMRInstance).GetField("m_XMRLSLApi", priv).SetValue(i, apiObject);

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
                             call_mode = i.callMode,
                             sleep_until_ticks = i.m_SleepUntil.Ticks,
                             object_description = typeof(XMRInstance)
                                                      .GetField("m_Part")
                                                      .GetValue(i)
                                                      .GetType()
                                                      .GetProperty("Description")
                                                      .GetValue(typeof(XMRInstance).GetField("m_Part").GetValue(i)),
                             event_arguments = i.ehArgs == null ? null : Array.ConvertAll(i.ehArgs, Typed),
                             state_index = i.stateCode,
                             new_state_index = i.newStateCode,
                             initialize_globals = i.doGblInit,
                             running = i.Running,
                             detectors = Array.ConvertAll(Dets(i), Describe),
                             queue = Queue(i),
                             frames };
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
            var timestamp = Assembly
                                .LoadFrom(Path.Combine(Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location),
                                                       "OpenSim.Framework.dll"))
                                .GetType("OpenSim.Framework.Util")
                                .GetMethod("GetTimeStampMS");
            void Slice(XMRInstance i) {
                checks = 0;
                i.m_SliceStart = (double)timestamp.Invoke(null, null);
            }
            var instance = New();
            Dets(instance, new[] { Rich(3) });
            Slice(instance);
            var startError = instance.StartEx();
            object initialError = startError == null ? null : Failure(startError);
            bool suspended = instance.stackFrames != null && instance.eventCode != ScriptEventCode.None;
            var initial = State(instance);
            object prefixResume = null, afterFollowup = null, afterLoad = null, afterStop = null,
                   afterRestartWait = null, liveResume = null, liveError = null, final = initial, resumeError = null;
            bool migrated = false;
            var liveCycles = new List<object>();
            var migratedCycles = new List<object>();
            object productionWriteError=null, permissiveWriteError=null, permissiveResume=null, permissiveResumeError=null;
            if (suspended) {
                byte[] production=null, permissive=null;
                using(var stream=new MemoryStream()) {
                    try {
                        // Invoke the unchanged actual production snapshot method,
                        // including its own default BinaryWriter constructor.
                        typeof(XMRInstance).GetMethod("MigrateOutEventHandler",priv).Invoke(instance,new object[]{stream});
                        production=stream.ToArray();
                        File.WriteAllBytes(Path.Combine(folder,"snapshot.bin"),production);
                    } catch(Exception error) {
                        productionWriteError=Failure(error.InnerException??error);
                        File.WriteAllBytes(Path.Combine(folder,"production-partial.bin"),stream.ToArray());
                    }
                }
                using(var stream=new MemoryStream()) {
                    try {
                        stream.WriteByte(XMRInstance.migrationVersion);stream.WriteByte(16);
                        instance.MigrateOut(new BinaryWriter(stream,System.Text.Encoding.UTF8,true));
                        permissive=stream.ToArray();
                        File.WriteAllBytes(Path.Combine(folder,"permissive-control.bin"),permissive);
                    } catch(Exception error) {permissiveWriteError=Failure(error);}
                }
                var queueXml=QueueXml(instance);File.WriteAllText(Path.Combine(folder,"event-queue.xml"),queueXml);
                if(production!=null) {
                    var fresh=Migrate(production);RestoreQueue(fresh,queueXml);afterLoad=State(fresh);migrated=true;
                    Slice(fresh);var error=fresh.ResumeEx();final=State(fresh);
                    if(error!=null)resumeError=Failure(error);
                }
                if(permissive!=null) {
                    var fresh=Migrate(permissive);RestoreQueue(fresh,queueXml);
                    Slice(fresh);var error=fresh.ResumeEx();permissiveResume=State(fresh);
                    if(error!=null)permissiveResumeError=Failure(error);
                }
                Slice(instance);var liveFailure=instance.ResumeEx();liveResume=State(instance);
                if(liveFailure!=null)liveError=Failure(liveFailure);
            }
            string Sha(string file) =>
                Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(file)))
                    .ToLowerInvariant();
            var hashes = new Dictionary<string, string>();
            foreach (string file in Directory.GetFiles(folder))
                if (!file.EndsWith("metadata.json"))
                    hashes[Path.GetFileName(file)] = Sha(file);
            hashes["generator_source"] = Sha(generator);
            hashes["generator_assembly"] = Sha(typeof(ConstructorNullSupplementReference).Assembly.Location);
            hashes["original_yengine_assembly"] = Sha(typeof(XMRInstance).Assembly.Location);
            hashes["original_scene_assembly"] = Sha(Path.Combine(
                Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location), "OpenSim.Region.Framework.dll"));
            hashes["original_plugins_assembly"] =
                Sha(Path.Combine(Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location),
                                 "OpenSim.Region.ScriptEngine.Shared.Api.dll"));
            foreach (string file in new[] { "MMRScriptCodeGen.cs", "XMRInstAbstract.cs", "XMRInstCtor.cs",
                                            "XMRInstBackend.cs", "XMRInstRun.cs", "XMRInstMisc.cs",
                                            "XMRScriptUThread.cs", "XMRInstCapture.cs" })
                hashes["original_source_" + file] =
                    Sha(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine", file));
            foreach (string file in new[] {
                         "OpenSim/Region/ScriptEngine/Shared/LSL_Types.cs",
                         "OpenSim/Region/ScriptEngine/YEngine/MMRScriptTypeCast.cs",
                         "OpenSim/Region/Framework/Scenes/Scene.cs",
                         "OpenSim/Region/Framework/Scenes/SceneObjectPart.cs",
                         "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/AsyncCommandManager.cs",
                         "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/Plugins/ScriptTimer.cs",
                         "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/Plugins/Dataserver.cs",
                         "OpenSim/Region/ScriptEngine/Shared/Api/Implementation/Plugins/SensorRepeat.cs"
                     })
                hashes["original_source_" + Path.GetFileName(file)] = Sha(Path.Combine(sourceRoot, file));
            var record = new {
                name = spec.Item1,
                accepted,
                diagnostics,
                compile_exception = compileError,
                source_hash = sourceHash,
                compiled_version = ScriptCodeGen.COMPILED_VERSION_VALUE,
                suspended,
                migrated,
                production_write_exception=productionWriteError,
                permissive_write_exception=permissiveWriteError,
                permissive_control_resume=permissiveResume,
                permissive_control_resume_exception=permissiveResumeError,
                production_writer="Unchanged private XMRInstance.MigrateOutEventHandler(Stream): new BinaryWriter(stream)",
                initial_exception = initialError,
                live_cycles = liveCycles,
                migrated_cycles = migratedCycles,
                prefix_resume = prefixResume,
                live_resume_exception = liveError,
                live_resume = liveResume,
                direct_reset_scheduler_boundary = spec.Item1 == "reset" && initialError != null,
                initial,
                after_followup = afterFollowup,
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
                methodology = "Actual unchanged original CodeGen/real SDK and StartEx/base CheckRunWork captures real llSleep. " +
                    "Actual private production MigrateOutEventHandler is invoked unchanged. Encoder exceptions are recorded, not bypassed. " +
                    "Explicit Encoding.UTF8 writer is separately named permissive control, not production parity. Live ResumeEx and successful byte imports are measured independently; CLR string utf16 arrays precede JSON replacement.",
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
