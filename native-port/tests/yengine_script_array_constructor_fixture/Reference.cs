// Copyright (c) Contributors, http://opensimulator.org/

// BSD license: native-port/LICENSE.txt.

// Invokes unchanged original backend helpers, PostEvent and queue XML codecs.

using System;

using System.Collections.Generic;

using System.Globalization;

using System.IO;

using System.Linq;

using System.Reflection;

using System.Reflection.Emit;

using System.Text.Json;

using System.Xml;

using OpenSim.Region.ScriptEngine.Yengine;

using OpenSim.Region.ScriptEngine.Shared;

using LSL = OpenSim.Region.ScriptEngine.Shared.LSL_Types;



public static class FixedArrayReference {

    static readonly BindingFlags priv = BindingFlags.Instance | BindingFlags.NonPublic;

    static readonly BindingFlags stat = BindingFlags.Static | BindingFlags.NonPublic;

    static readonly List<object> results = new();

    static int checks;

    static string scenario;

    static string importRoot;



    static Action<XMRInstance> originalCheck;



    public static void Check(XMRInstAbstract instance) {

        if (++checks > 2000)

            throw new Exception("Original scheduler oracle bound");

        originalCheck((XMRInstance)instance);

    }



    static object DoubleValue(double value) => new { clr = "System.Double",

                                                     value = double.IsFinite(value) ? (object)value : null,

                                                     bits = BitConverter.DoubleToUInt64Bits(value).ToString("x16"),

                                                     text = value.ToString("R", CultureInfo.InvariantCulture) };



    static readonly Dictionary<object, int> graphIds =

        new(System.Collections.Generic.ReferenceEqualityComparer.Instance);



    static object Typed(object value) {

        string clr = value?.GetType().FullName;

        if (value == null)

            return new { type = "null", clr, value = (object)null };

        if (value is XMRSDTypeClObj wrapper) {

            if (graphIds.TryGetValue(value, out int previous))

                return new { type = "script_class_reference", clr, reference_id = previous };

            int id = graphIds.Count + 1;

            graphIds.Add(value, id);

            var fields = wrapper.instVars;

            return new { type = "script_class", clr, reference_id = id,

                script_type = wrapper.sdtcClass.longName.val,

                class_index = wrapper.sdtcClass.sdTypeIndex,

                rank = wrapper.sdtcClass.arrayOfRank,

                element_type = wrapper.sdtcClass.arrayOfType?.ToString(),

                arrays_heap_use = fields.arraysHeapUse,

                integers = fields.iarIntegers.ToArray(),

                objects = Array.ConvertAll(fields.iarObjects, Typed) };

        }

        if (value is Array array) {

            if (graphIds.TryGetValue(value, out int previous))

                return new { type = "array_reference", clr, reference_id = previous };

            int id = graphIds.Count + 1;

            graphIds.Add(value, id);

            return new { type = "clr_array", clr, reference_id = id,

                element_type = array.GetType().GetElementType().FullName,

                length = array.Length, items = array.Cast<object>().Select(Typed).ToArray() };

        }

        if (value is int i)

            return new { type = "integer", clr, value = i };

        if (value is LSL.LSLInteger li)

            return new { type = "integer", clr, value = li.value };

        if (value is char ch)

            return new { type = "char", clr, value = (int)ch, utf16 = (int)ch };

        if (value is double d)

            return new { type = "float", clr, value = double.IsFinite(d) ? (object)d : null,

                         bits = BitConverter.DoubleToUInt64Bits(d).ToString("x16") };

        if (value is LSL.LSLFloat lf)

            return new { type = "float", clr, value = double.IsFinite(lf.value) ? (object)lf.value : null,

                         bits = BitConverter.DoubleToUInt64Bits(lf.value).ToString("x16") };

        if (value is string s)

            return new { type = "string", clr, value = s, utf16 = Array.ConvertAll(s.ToCharArray(), x => (int)x) };

        if (value is LSL.LSLString ls)

            return new { type = "string", clr, value = ls.m_string,

                         utf16 =

                             ls.m_string == null ? null : Array.ConvertAll(ls.m_string.ToCharArray(), x => (int)x) };

        if (value is LSL.key k)

            return new { type = "key", clr, value = k.ToString() };

        if (value is LSL.Vector3 v)

            return new { type = "vector", clr, value = new[] { DoubleValue(v.x), DoubleValue(v.y), DoubleValue(v.z) } };

        if (value is LSL.Quaternion q)

            return new { type = "rotation", clr,

                         value = new[] { DoubleValue(q.x), DoubleValue(q.y), DoubleValue(q.z), DoubleValue(q.s) } };

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

        rotation = new[] { DoubleValue(d.Rotation.x), DoubleValue(d.Rotation.y), DoubleValue(d.Rotation.z),

                           DoubleValue(d.Rotation.s) },

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

        importRoot = args.Length > 4 ? Path.GetFullPath(args[4]) : null;

        Directory.CreateDirectory(output);

        AppDomain.CurrentDomain.AssemblyResolve += (_, r) => {

            string p = Path.Combine(bin, new AssemblyName(r.Name).Name + ".dll");

            return File.Exists(p) ? Assembly.LoadFrom(p) : null;

        };

        Run(output, generator, sourceRoot);

    }



    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]

    static void Run(string output, string generator, string sourceRoot) {

        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;

        var dm = new DynamicMethod("OriginalCheck", typeof(void), new[] { typeof(XMRInstance) },

                                   typeof(FixedArrayReference), true);

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

        il.Emit(OpCodes.Call, typeof(FixedArrayReference).GetMethod(nameof(Check)));

        il.Emit(OpCodes.Ret);

        tb.DefineMethodOverride(method, typeof(XMRInstance).GetMethod("CheckRunWork"));

        var type = tb.CreateType();

        ScriptCodeGen.xmrInstSuperType = type;

        ScriptObjWriter.DefineInternalType("xmrsuper", type);

        ScriptObjWriter.DefineInternalType("apimanager_LSL", api);

        ScriptConst.AddInterfaceConstants(null, api.GetFields());

        TokenDeclInline.AddInterfaceMethods(null, api.GetMethods(), type.GetField("m_ApiManager_LSL"));



        var marvin = typeof(object).Assembly.GetType("System.Marvin", true);

        ulong marvinSeed =

            (ulong)marvin.GetProperty("DefaultSeed", BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Static)

                .GetValue(null);

        var allCases = new List<(string, string, bool)> {
            ("constructor_basic", "YOption allowall;\ninteger[] input; object pressure; integer result; integer before; integer during; integer after;\ninteger Use(integer[] value,integer ignored){input=value;return value.Length;}\ninteger Pause(){during=xmrHeapUsed();llSleep(0.1);return 7;}\ndefault{state_entry(){before=xmrHeapUsed();result=Use(new integer[](4),Pause());after=1;}}\n", true),
            ("constructor_pressure", "YOption allowall;\ninteger[] input; object pressure; integer result; integer before; integer during; integer after;\ninteger Use(integer[] value,integer ignored){input=value;return value.Length;}\ninteger Pause(){pressure=xmrFixedArrayAllocI(28);during=xmrHeapUsed();llSleep(0.1);return 7;}\ndefault{state_entry(){before=xmrHeapUsed();result=Use(new integer[](4),Pause());after=1;}}\n", true)
        };

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

                typeof(XMRInstAbstract).GetField("heapLimit", priv).SetValue(i, scenario == "constructor_pressure" ? 128 : 100000000);

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

            object Input(XMRInstance i) {

                foreach (var names in code.globalVarNames)

                    foreach (var entry in names.Value)

                        if (entry.Value == "input")

                            return ((Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(i.glblVars))

                                .GetValue(entry.Key);

                return null;

            }

            object HashObservation(XMRInstance i) {

                object input = Input(i);

                object error = null;

                int? hash = null;

                int[] components = null;

                int? formula = null;

                bool? aliasData = null;

                try {

                    hash = XMRInstAbstract.xmrHashCode(input);

                } catch (Exception e) {

                    error = Failure(e);

                }

                if (input is LSL.Vector3 v) {

                    components = new[] { v.x.GetHashCode(), v.y.GetHashCode(), v.z.GetHashCode() };

                    formula = unchecked(components[0] + components[1] + components[2]);

                }

                if (input is LSL.Quaternion q) {

                    components = new[] { q.x.GetHashCode(), q.y.GetHashCode(), q.z.GetHashCode(), q.s.GetHashCode() };

                    formula = components[0] ^ components[1] ^ components[2] ^ components[3];

                }

                if (input is LSL.list l) {

                    formula = l.Data.GetHashCode();

                    foreach (var names in code.globalVarNames)

                        foreach (var entry in names.Value)

                            if (entry.Value == "alias") {

                                var other = ((Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(i.glblVars))

                                                .GetValue(entry.Key) as LSL.list;

                                aliasData = other != null && object.ReferenceEquals(l.Data, other.Data);

                            }

                }

                return new { input = Typed(input),

                             direct_hash = hash,

                             direct_exception = error,

                             component_hashes = components,

                             formula,

                             alias_same_backing = aliasData };

            }

            object ReferencePairs(XMRInstance i) {

                var items = new List<(string name, object value)>();

                foreach (var names in code.globalVarNames) {

                    var a = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(i.glblVars);

                    if (a.GetType().GetElementType() != typeof(object) &&

                        a.GetType().GetElementType() != typeof(string))

                        continue;

                    foreach (var e in names.Value)

                        items.Add((e.Value, a.GetValue(e.Key)));

                }

                var topItems = items.ToArray();

                foreach (var item in topItems)

                    if (item.value is Array ar) {

                        foreach (int index in new[] { 0, ar.Length / 2, ar.Length - 1 }.Distinct())

                            items.Add((item.name + "[" + index + "]", ar.GetValue(index)));

                    }

                var result = new List<object>();

                for (int a = 0; a < items.Count; a++)

                    for (int c = a + 1; c < items.Count; c++)

                        result.Add(new { left = items[a].name, right = items[c].name,

                                         same_reference = object.ReferenceEquals(items[a].value, items[c].value),

                                         object_equals = object.Equals(items[a].value, items[c].value) });

                return result;

            }

            object State(XMRInstance i) {

                graphIds.Clear();

                var globals = new SortedDictionary<string, object>();

                foreach (var names in code.globalVarNames) {

                    var array = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(i.glblVars);

                    foreach (var name in names.Value)

                        globals[name.Value] = Typed(array.GetValue(name.Key));

                }

                var frames = new List<object>();

                for (var f = i.stackFrames; f != null; f = f.nextSF)

                    frames.Add(new { function = f.funcName, call_no = f.callNo, slots = f.objArray.Length,

                                     slot_types = Array.ConvertAll(f.objArray, value => value?.GetType().FullName),

                                     slot_values = Array.ConvertAll(f.objArray, Typed),

                                     wait_deadline_utc_ticks = f.funcName == "xmrEventDequeue"

                                                                   ? (long?)DateTime.Parse((string)f.objArray[0]).Ticks

                                                                   : null });

                return new { heap_used = i.xmrHeapUsed(),

                             heap_left = i.xmrHeapLeft(),

                             arrays_heap_used = i.glblVars.arraysHeapUse,

                             locals_heap_used = i.m_localsHeapUsed,

                             globals,

                             object_hash = HashObservation(i),

                             object_reference_pairs = ReferencePairs(i),

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

            string detectorXml = null;

            XMRInstance Migrate(byte[] data) {

                var i = New();

                using var stream = new MemoryStream(data);

                stream.Position = 2;

                i.MigrateIn(new BinaryReader(stream));

                if (detectorXml != null) {

                    var document = new XmlDocument();

                    document.LoadXml(detectorXml);

                    var restored = (DetectParams[]) typeof(XMRInstance)

                                       .GetMethod("RestoreDetectParams", priv)

                                       .Invoke(i, new object[] { document.DocumentElement });

                    Dets(i, restored);

                }

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

            bool imported = importRoot != null && File.Exists(Path.Combine(importRoot, spec.Item1, "snapshot.bin"));

            if (imported)

                instance = Migrate(File.ReadAllBytes(Path.Combine(importRoot, spec.Item1, "snapshot.bin")));

            Dets(instance, new[] { Rich(3) });

            Slice(instance);

            var startError = imported ? instance.ResumeEx() : instance.StartEx();

            object initialError = startError == null ? null : Failure(startError);

            bool suspended = instance.stackFrames != null && instance.eventCode != ScriptEventCode.None;

            var initial = State(instance);
            var retainedConstructors = new List<XMRSDTypeClObj>();
            var retainedSlots = new List<object>();
            for(var frame=instance.stackFrames;frame!=null;frame=frame.nextSF)
                for(int slot=0;slot<frame.objArray.Length;slot++)
                    if(frame.objArray[slot] is XMRSDTypeClObj value) {
                        retainedConstructors.Add(value);
                        retainedSlots.Add(new {function=frame.funcName,call_no=frame.callNo,slot,
                            class_index=value.sdtcClass.sdTypeIndex,name=value.sdtcClass.longName.val,
                            dimensions=value.instVars.iarIntegers.ToArray(),own_heap_usage=value.instVars.arraysHeapUse,
                            identity=System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(value)});
                    }
            File.WriteAllText(Path.Combine(folder,"retained-constructor-capture.json"),JsonSerializer.Serialize(new {
                process_id=Environment.ProcessId,retained=retainedSlots,global_heap=instance.glblVars.arraysHeapUse,
                locals_heap=instance.m_localsHeapUsed,heap_limit=scenario=="constructor_pressure"?128:100000000
            },new JsonSerializerOptions{WriteIndented=true}));


            var detectDocument = new XmlDocument();

            var detectRoot = detectDocument.CreateElement("DetectArray");

            detectDocument.AppendChild(detectRoot);

            typeof(XMRInstance)

                .GetMethod("AppendXMLDetectArray", stat)

                .Invoke(null, new object[] { detectDocument, detectRoot, Dets(instance) });

            detectorXml = detectDocument.OuterXml;

            File.WriteAllText(Path.Combine(folder, "detect-array.xml"), detectorXml);

            object prefixResume = null, afterFollowup = null, afterLoad = null, afterStop = null,

                   afterRestartWait = null, liveResume = null, liveError = null, final = initial, resumeError = null;

            bool migrated = false;

            var liveCycles = new List<object>();

            var migratedCycles = new List<object>();

            object binaryLoadError = null, queueRestoreError = null, afterBinaryLoad = null;

            object productionWriteError = null, permissiveWriteError = null, permissiveResume = null,

                   permissiveResumeError = null;

            if (suspended) {

                byte[] production = null, permissive = null;

                using (var stream = new MemoryStream()) {

                    try {

                        // Invoke the unchanged actual production snapshot method,

                        // including its own default BinaryWriter constructor.

                        typeof(XMRInstance)

                            .GetMethod("MigrateOutEventHandler", priv)

                            .Invoke(instance, new object[] { stream });

                        production = stream.ToArray();

                        File.WriteAllBytes(Path.Combine(folder, "snapshot.bin"), production);

                    } catch (Exception error) {

                        productionWriteError = Failure(error.InnerException ?? error);

                        File.WriteAllBytes(Path.Combine(folder, "production-partial.bin"), stream.ToArray());

                    }

                }

                using (var stream = new MemoryStream()) {

                    try {

                        stream.WriteByte(XMRInstance.migrationVersion);

                        stream.WriteByte(16);

                        instance.MigrateOut(new BinaryWriter(stream, System.Text.Encoding.UTF8, true));

                        permissive = stream.ToArray();

                        File.WriteAllBytes(Path.Combine(folder, "permissive-control.bin"), permissive);

                    } catch (Exception error) {

                        permissiveWriteError = Failure(error);

                    }

                }

                var queueXml = QueueXml(instance);

                File.WriteAllText(Path.Combine(folder, "event-queue.xml"), queueXml);

                if (production != null) {

                    XMRInstance fresh = null;

                    try {

                        fresh = Migrate(production);

                        afterBinaryLoad = State(fresh);

                    } catch (Exception error) {

                        binaryLoadError = Failure(error.InnerException ?? error);

                    }

                    if (fresh != null) {

                        try {

                            RestoreQueue(fresh, queueXml);

                        } catch (Exception error) {

                            queueRestoreError = Failure(error.InnerException ?? error);

                        }

                        afterLoad = State(fresh);

                        migrated = true;

                        if (queueRestoreError == null) {

                            Slice(fresh);

                            var error = fresh.ResumeEx();

                            final = State(fresh);

                            if (error != null)

                                resumeError = Failure(error);

                        }

                    }

                }

                Slice(instance);

                var liveFailure = instance.ResumeEx();
                var resumedInput=Input(instance);
                File.WriteAllText(Path.Combine(folder,"retained-constructor-live.json"),JsonSerializer.Serialize(new {
                    retained_count=retainedConstructors.Count, live_input_is_captured_reference=retainedConstructors.Any(value=>ReferenceEquals(value,resumedInput)),
                    global_heap=instance.glblVars.arraysHeapUse,locals_heap=instance.m_localsHeapUsed,
                    resume_exception=liveFailure==null?null:Failure(liveFailure)
                },new JsonSerializerOptions{WriteIndented=true}));


                liveResume = State(instance);

                if (liveFailure != null)

                    liveError = Failure(liveFailure);

            }

            object hashInput = null;

            foreach (var names in code.globalVarNames)

                foreach (var name in names.Value)

                    if (name.Value == "input")

                        hashInput = ((Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(instance.glblVars))

                                        .GetValue(name.Key);

            object directResult = null, directError = null;

            try {

                directResult = XMRInstAbstract.xmrHashCode((object)hashInput);

            } catch (Exception e) {

                directError = Failure(e);

            }

            string Sha(string file) =>

                Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(file)))

                    .ToLowerInvariant();

            var hashes = new Dictionary<string, string>();

            foreach (string file in Directory.GetFiles(folder))

                if (!file.EndsWith("metadata.json"))

                    hashes[Path.GetFileName(file)] = Sha(file);

            hashes["generator_source"] = Sha(generator);

            hashes["generator_assembly"] = Sha(typeof(FixedArrayReference).Assembly.Location);

            hashes["original_yengine_assembly"] = Sha(typeof(XMRInstance).Assembly.Location);

            hashes["original_scene_assembly"] = Sha(Path.Combine(

                Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location), "OpenSim.Region.Framework.dll"));

            hashes["original_plugins_assembly"] =

                Sha(Path.Combine(Path.GetDirectoryName(typeof(XMRInstance).Assembly.Location),

                                 "OpenSim.Region.ScriptEngine.Shared.Api.dll"));

            foreach (string file in new[] { "MMRScriptCodeGen.cs", "XMRInstAbstract.cs", "XMRInstCtor.cs",

                                            "XMRInstBackend.cs", "XMRInstRun.cs", "XMRInstMisc.cs", "XMRInstCapture.cs",

                                            "XMRScriptUThread.cs", "XMRInstCapture.cs" })

                hashes["original_source_" + file] =

                    Sha(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine", file));

            foreach (string file in new[] {

                         "OpenSim/Region/ScriptEngine/Shared/LSL_Types.cs",

                         "OpenSim/Region/ScriptEngine/Shared/Helpers.cs",

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

                process_id = Environment.ProcessId,

                marvin_seed = marvinSeed.ToString("x16"),

                imported_capture = imported,

                imported_from = imported ? Path.Combine(importRoot, spec.Item1, "snapshot.bin") : null,

                direct_object_input = Typed(hashInput),

                direct_object_hash = directResult,

                direct_object_exception = directError,

                hash_call_lines = File.ReadAllLines(Path.Combine(folder, "original.il.txt"))

                                      .Where(l => l.Contains("xmrHashCode"))

                                      .ToArray(),

                production_culture = CultureInfo.CurrentCulture.Name,

                runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,

                architecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),

                accepted,

                diagnostics,

                compile_exception = compileError,

                source_hash = sourceHash,

                compiled_version = ScriptCodeGen.COMPILED_VERSION_VALUE,

                suspended,

                migrated,

                binary_load_exception = binaryLoadError,

                queue_restore_exception = queueRestoreError,

                after_binary_load = afterBinaryLoad,

                production_write_exception = productionWriteError,

                permissive_write_exception = permissiveWriteError,

                permissive_control_resume = permissiveResume,

                permissive_control_resume_exception = permissiveResumeError,

                production_writer =

                    "Unchanged private XMRInstance.MigrateOutEventHandler(Stream): new BinaryWriter(stream)",

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

                methodology =

                    "Actual original full Object value source controls, gated YOption arrays. Scalar boxes/unboxes, " +

                    "defaults/locals/parameters/returns, CLR reference vs object.Equals and string identity observed " +

                    "from original object/string storage only (no synthetic boxing of value-storage during " +

                    "observation). RealSleep/privateproductionbinary/SAME-live and optional separateprocessimport; " +

                    "hash numbers are not assumed portable for identity-bearing values. Preserved separate hash " +

                    "baseline/gating groups.",

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

