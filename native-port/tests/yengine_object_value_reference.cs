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

public static class ObjectValueReference {
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

    static object Typed(object value) {
        string clr = value?.GetType().FullName;
        if (value == null)
            return new { type = "null", clr, value = (object)null };
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
        if (value is object[] array)
            return new { type = "object_array", clr, value = Array.ConvertAll(array, Typed) };
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
                                   typeof(ObjectValueReference), true);
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
        il.Emit(OpCodes.Call, typeof(ObjectValueReference).GetMethod(nameof(Check)));
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
            ("alias_sleep_dynamic_boolean",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7aW50ZWdlciBzb3VyY2U9MTtvYmplY3QgaW5wdXQ7b2JqZWN0IGFsaWFzO2ludGVnZXIgYmVmb3JlX2V" +
                 "xdWFsO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgZW50cmllcztpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtlbn" +
                 "RyaWVzKys7aW5wdXQ9KHNvdXJjZTwyKTthbGlhcz1pbnB1dDtiZWZvcmVfZXF1YWw9KGlucHV0PT1hbGlhcyk7bGxTbGVlcCgwL" +
                 "jEpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTthZnRlcj0xO319")),
             true),
            ("alias_sleep_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpNztvYmplY3QgYWxpYXM9aW5wdXQ7aW50ZWdlciBiZWZvcmVfZXF" +
                 "1YWw7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBlbnRyaWVzO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2VudH" +
                 "JpZXMrKztiZWZvcmVfZXF1YWw9KGlucHV0PT1hbGlhcyk7bGxTbGVlcCgwLjEpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTthZnRlc" +
                 "j0xO319")),
             true),
            ("alias_sleep_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpWzEsMl07b2JqZWN0IGFsaWFzPWlucHV0O2ludGVnZXIgYmVmb3J" +
                 "lX2VxdWFsO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgZW50cmllcztpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKX" +
                 "tlbnRyaWVzKys7YmVmb3JlX2VxdWFsPShpbnB1dD09YWxpYXMpO2xsU2xlZXAoMC4xKTtyZXN1bHQ9KGlucHV0PT1hbGlhcyk7Y" +
                 "WZ0ZXI9MTt9fQ==")),
             true),
            ("alias_sleep_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpPDEsMiwzLDQ+" +
                 "O29iamVjdCBhbGlhcz1pbnB1dDtpbnRlZ2VyIGJlZm9yZV9lcXVhbDtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGVudHJpZXM7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7ZW50cmllcysrO2JlZm9yZV9lcXVhbD0oaW5wdXQ9PWFsaWFzKTtsbF" +
                 "NsZWVwKDAuMSk7cmVzdWx0PShpbnB1dD09YWxpYXMpO2FmdGVyPTE7fX0=")),
             true),
            ("alias_sleep_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpInNhbWUiO29iamVjdCBhbGlhcz1pbnB1dDtpbnRlZ2VyIGJlZm9" +
                 "yZV9lcXVhbDtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGVudHJpZXM7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KC" +
                 "l7ZW50cmllcysrO2JlZm9yZV9lcXVhbD0oaW5wdXQ9PWFsaWFzKTtsbFNsZWVwKDAuMSk7cmVzdWx0PShpbnB1dD09YWxpYXMpO" +
                 "2FmdGVyPTE7fX0=")),
             true),
            ("alias_sleep_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpPDEsMiwzPjtvYmplY3QgYWxpYXM9aW5wdXQ7aW50ZWdlciBiZWZ" +
                 "vcmVfZXF1YWw7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBlbnRyaWVzO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeS" +
                 "gpe2VudHJpZXMrKztiZWZvcmVfZXF1YWw9KGlucHV0PT1hbGlhcyk7bGxTbGVlcCgwLjEpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzK" +
                 "TthZnRlcj0xO319")),
             true),
            ("box_boolean_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtzdHJpbmcgdHlwZW5hbWU7aW5" +
                 "0ZWdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdX" +
                 "QpO3Jlc3VsdD14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_boolean_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KDE8Mik7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmV" +
                 "zdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3R5cGVuYW1lPXhtclR5cGVOYW1lKGlucHV0KTtyZXN1bH" +
                 "Q9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("box_char_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7c3RyaW5nIHR5cGVuYW1lO2ludGV" +
                 "nZXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3R5cGVuYW1lPXhtclR5cGVOYW1lKGlucHV0KT" +
                 "tyZXN1bHQ9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("box_char_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9J0EnO3N0cmluZyB0eXBlbmFtZTtpbnRlZ2VyIHJlc3V" +
                 "sdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PX" +
                 "htckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_dynamic_boolean",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7aW50ZWdlciBzb3VyY2U9MTtvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx" +
                 "0O2RlZmF1bHR7c3RhdGVfZW50cnkoKXtpbnB1dD0oc291cmNlPDIpO3R5cGVuYW1lPXhtclR5cGVOYW1lKGlucHV0KTtyZXN1bH" +
                 "Q9eG1ySGFzaENvZGUoaW5wdXQpO319")),
             true),
            ("box_float_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O3N0cmluZyB0eXBlbmFtZTtpbnR" +
                 "lZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dC" +
                 "k7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_float_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9MS4yNTtzdHJpbmcgdHlwZW5hbWU7aW50ZWdlciByZXN" +
                 "1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3VsdD" +
                 "14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_integer_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3N0cmluZyB0eXBlbmFtZTtpbnRlZ2V" +
                 "yIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dCk7cm" +
                 "VzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_integer_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9NztzdHJpbmcgdHlwZW5hbWU7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3VsdD14bX" +
                 "JIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_key_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO3N0cmluZyB0eXBlbmFtZTtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3" +
                 "RhdGVfZW50cnkoKXt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO" +
                 "319")),
             true),
            ("box_key_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KGtleSkiMTExMTExMTEtMjIyMi0zMzMzLTQ0NDQtNTU" +
                 "1NTU1NTU1NTU1IjtzdHJpbmcgdHlwZW5hbWU7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudH" +
                 "J5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3VsdD14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_list_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO3N0cmluZyB0eXBlbmFtZTt" +
                 "pbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmFtZT14bXJUeXBlTmFtZShpbn" +
                 "B1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_list_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9WzEsIngiXTtzdHJpbmcgdHlwZW5hbWU7aW50ZWdlciB" +
                 "yZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3" +
                 "VsdD14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_null_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtzdHJpbmcgdHlwZW5hbWU7aW5" +
                 "0ZWdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdX" +
                 "QpO3Jlc3VsdD14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_null_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9dW5kZWY7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmV" +
                 "zdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3R5cGVuYW1lPXhtclR5cGVOYW1lKGlucHV0KTtyZXN1bH" +
                 "Q9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("box_rotation_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47c3RyaW5nIHR5cGVuYW1" +
                 "lO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3R5cGVuYW1lPXhtclR5cGVOYW1lKG" +
                 "lucHV0KTtyZXN1bHQ9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("box_rotation_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9PDEsMiwzLDQ+" +
                 "O3N0cmluZyB0eXBlbmFtZTtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmF" +
                 "tZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_string_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO3N0cmluZyB0eXBlbmFtZTtpbnR" +
                 "lZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dC" +
                 "k7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_string_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9IjEyIjtzdHJpbmcgdHlwZW5hbWU7aW50ZWdlciByZXN" +
                 "1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3VsdD" +
                 "14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("box_vector_explicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O3N0cmluZyB0eXBlbmFtZTtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0eXBlbmF" +
                 "tZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("box_vector_implicit",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9PDEsMiwzPjtzdHJpbmcgdHlwZW5hbWU7aW50ZWdlciB" +
                 "yZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3" +
                 "VsdD14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("default_global",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0O29iamVjdCBvdGhlcjtpbnRlZ2VyIHJlc3VsdD" +
                                          "tkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShpbnB1dD09b3RoZXIpO319")),
             true),
            ("default_local",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0O2ludGVnZXIgcmVzdWx0O2RlZmF1bHR7c3RhdG" +
                                          "VfZW50cnkoKXtvYmplY3QgbG9jYWw7cmVzdWx0PShpbnB1dD09bG9jYWwpO319")),
             true),
            ("implicit_object_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2NoYXIgcmVzdWx0O2ludGVnZXIgYWZ" +
                 "0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD1pbnB1dDthZnRlcj0xO319")),
             true),
            ("implicit_object_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2Zsb2F0IHJlc3VsdDtpbnRlZ2VyIGF" +
                 "mdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9aW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("implicit_object_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2ludGVnZXIgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD1pbnB1dDthZnRlcj0xO319")),
             true),
            ("implicit_object_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2tleSByZXN1bHQ7aW50ZWdlciBhZnR" +
                 "lcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("implicit_object_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2xpc3QgcmVzdWx0O2ludGVnZXIgYWZ" +
                 "0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD1pbnB1dDthZnRlcj0xO319")),
             true),
            ("implicit_object_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3JvdGF0aW9uIHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9aW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("implicit_object_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3N0cmluZyByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("implicit_object_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3ZlY3RvciByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("inline_dynamic_boolean_hash",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7aW50ZWdlciBzb3VyY2U9MTtvYmplY3QgaW5wdXQ7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjt" +
                 "kZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKHNvdXJjZTwyKSk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("inline_dynamic_boolean_typename",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7aW50ZWdlciBzb3VyY2U9MTtvYmplY3QgaW5wdXQ7c3RyaW5nIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2R" +
                 "lZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9eG1yVHlwZU5hbWUoKG9iamVjdCkoc291cmNlPDIpKTthZnRlcj0xO319")),
             true),
            ("local_dynamic_boolean",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7aW50ZWdlciBzb3VyY2U9MTtvYmplY3QgaW5wdXQ7c3RyaW5nIHJlc3" +
                                          "VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtvYmplY3QgbG9jYWw9KHNv" +
                                          "dXJjZTwyKTtyZXN1bHQ9eG1yVHlwZU5hbWUobG9jYWwpO2FmdGVyPTE7fX0=")),
             true),
            ("local_object_char",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7b2" +
                                          "JqZWN0IGFsaWFzO2ludGVnZXIgcmVzdWx0O2RlZmF1bHR7c3RhdGVfZW50cnkoKXtvYmplY3Qg" +
                                          "bG9jYWw9aW5wdXQ7YWxpYXM9bG9jYWw7cmVzdWx0PShpbnB1dD09YWxpYXMpO319")),
             true),
            ("local_object_float",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O2" +
                                          "9iamVjdCBhbGlhcztpbnRlZ2VyIHJlc3VsdDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7b2JqZWN0" +
                                          "IGxvY2FsPWlucHV0O2FsaWFzPWxvY2FsO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt9fQ==")),
             true),
            ("local_object_integer",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O29iam" +
                                          "VjdCBhbGlhcztpbnRlZ2VyIHJlc3VsdDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7b2JqZWN0IGxv" +
                                          "Y2FsPWlucHV0O2FsaWFzPWxvY2FsO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt9fQ==")),
             true),
            ("local_object_list",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwyXT" +
                                          "tvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe29iamVj" +
                                          "dCBsb2NhbD1pbnB1dDthbGlhcz1sb2NhbDtyZXN1bHQ9KGlucHV0PT1hbGlhcyk7fX0=")),
             true),
            ("local_object_rotation",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLD" +
                                          "MsND47b2JqZWN0IGFsaWFzO2ludGVnZXIgcmVzdWx0O2RlZmF1bHR7c3RhdGVfZW50cnkoKXtv" +
                                          "YmplY3QgbG9jYWw9aW5wdXQ7YWxpYXM9bG9jYWw7cmVzdWx0PShpbnB1dD09YWxpYXMpO319")),
             true),
            ("local_object_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O29iamVjdCBhbGlhcztpbnRlZ2VyIHJlc3VsdDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7b2JqZWN0IGxvY2FsPWlucHV0O2FsaWF" +
                 "zPWxvY2FsO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt9fQ==")),
             true),
            ("missing_arrays_option",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("b2JqZWN0IGlucHV0O2RlZmF1bHR7c3RhdGVfZW50cnkoKXt9fQ==")),
             true),
            ("objects_option_only",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBvYmplY3RzO29iamVjdCBpbnB1dDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7fX0=")),
             true),
            ("object_bool_provenance_and",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoc291cmNlJiZvdGhlcikpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KShzb3VyY2UmJm90aGVyKSk7YWZ0ZXI9MTt9fQ=" +
                 "=")),
             true),
            ("object_bool_provenance_char_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoY2g9PSdBJykpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KShjaD09J0EnKSk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_bool_provenance_float_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkocmVhbD09MS4wKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKHJlYWw9PTEuMCkpO2FmdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_less",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoc291cmNlPDIpKTtyZXN1bHQ9eG1ySGFzaENvZGUoKG9iamVjdCkoc291cmNlPDIpKTthZnRlcj0xO319")),
             true),
            ("object_bool_provenance_less_integer_cast",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoKGludGVnZXIpKHNvdXJjZTwyKSkpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KSgoaW50ZWdlcikoc291cmNlPDIpKSk" +
                 "7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_bool_provenance_list_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoaXRlbXM9PVtdKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKGl0ZW1zPT1bXSkpO2FmdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_not",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoIXNvdXJjZSkpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KSghc291cmNlKSk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_bool_provenance_numeric_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoc291cmNlPT1vdGhlcikpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KShzb3VyY2U9PW90aGVyKSk7YWZ0ZXI9MTt9fQ=" +
                 "=")),
             true),
            ("object_bool_provenance_or",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoc291cmNlfHxvdGhlcikpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KShzb3VyY2V8fG90aGVyKSk7YWZ0ZXI9MTt9fQ=" +
                 "=")),
             true),
            ("object_bool_provenance_pointer_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoaW5wdXQ9PWFsaWFzKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKGlucHV0PT1hbGlhcykpO2FmdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_pointer_not_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoaW5wdXQhPWFsaWFzKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKGlucHV0IT1hbGlhcykpO2FmdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_rotation_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkocm90PT1aRVJPX1JPVEFUSU9OKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKHJvdD09WkVST19ST1RBVElPTikpO2F" +
                 "mdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_string_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkodGV4dD09IngiKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKHRleHQ9PSJ4IikpO2FmdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_vector_equal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkodmVjPT1aRVJPX1ZFQ1RPUikpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KSh2ZWM9PVpFUk9fVkVDVE9SKSk7YWZ0ZXI" +
                 "9MTt9fQ==")),
             true),
            ("object_bool_provenance_xmr_key_truth",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoeG1yS2V5VG9Cb29sKChrZXkpdGV4dCkpKTtyZXN1bHQ9eG1ySGFzaENvZGUoKG9iamVjdCkoeG1yS2V5VG9Cb29sKChrZXk" +
                 "pdGV4dCkpKTthZnRlcj0xO319")),
             true),
            ("object_bool_provenance_xmr_list_truth",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoeG1yTGlzdFRvQm9vbChpdGVtcykpKTtyZXN1bHQ9eG1ySGFzaENvZGUoKG9iamVjdCkoeG1yTGlzdFRvQm9vbChpdGVtcyk" +
                 "pKTthZnRlcj0xO319")),
             true),
            ("object_bool_provenance_xmr_rotation_truth",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoeG1yUm90YXRpb25Ub0Jvb2wocm90KSkpO3Jlc3VsdD14bXJIYXNoQ29kZSgob2JqZWN0KSh4bXJSb3RhdGlvblRvQm9vbCh" +
                 "yb3QpKSk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_bool_provenance_xmr_string_truth",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoeG1yU3RyaW5nVG9Cb29sKHRleHQpKSk7cmVzdWx0PXhtckhhc2hDb2RlKChvYmplY3QpKHhtclN0cmluZ1RvQm9vbCh0ZXh" +
                 "0KSkpO2FmdGVyPTE7fX0=")),
             true),
            ("object_bool_provenance_xmr_vector_truth",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO2ludGVnZXIgb3RoZXI9MDtmbG9hdCByZWFsPTE" +
                 "uMDtzdHJpbmcgdGV4dD0ieCI7bGlzdCBpdGVtcz1bXTt2ZWN0b3IgdmVjPVpFUk9fVkVDVE9SO3JvdGF0aW9uIHJvdD1aRVJPX1" +
                 "JPVEFUSU9OO2NoYXIgY2g9J0EnO29iamVjdCBpbnB1dD03O29iamVjdCBhbGlhcz1pbnB1dDtzdHJpbmcgdHlwZW5hbWU7aW50Z" +
                 "WdlciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7dHlwZW5hbWU9eG1yVHlwZU5hbWUoKG9iamVj" +
                 "dCkoeG1yVmVjdG9yVG9Cb29sKHZlYykpKTtyZXN1bHQ9eG1ySGFzaENvZGUoKG9iamVjdCkoeG1yVmVjdG9yVG9Cb29sKHZlYyk" +
                 "pKTthZnRlcj0xO319")),
             true),
            ("object_parameter_return",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0O2ludGVnZXIgcmVzdWx0O3N0cmluZyB0eXBlbmFtZTtvYmplY3QgZWNobyhvYmp" +
                 "lY3QgdmFsdWUpe3JldHVybiB2YWx1ZTt9ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PWVjaG8oNyk7cmVzdWx0PShpbnRlZ2" +
                 "VyKWlucHV0O3R5cGVuYW1lPXhtclR5cGVOYW1lKGlucHV0KTt9fQ==")),
             true),
            ("object_return_primitive",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0O2ludGVnZXIgcmVzdWx0O29iamVjdCBlY2hvKCl7cmV0dXJuIDc7fWRlZmF1bHR" +
                 "7c3RhdGVfZW50cnkoKXtpbnB1dD1lY2hvKCk7cmVzdWx0PShpbnRlZ2VyKWlucHV0O319")),
             true),
            ("object_string_surrogate_55296",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHVuaXQ9NTUyOTY7b2JqZWN0IGlucHV0O3N0cmluZyByZXN1bHQ" +
                 "7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9KHN0cmluZykoY2hhcil1bml0O2xsU2xlZXAoMC4xKT" +
                 "tyZXN1bHQ9KHN0cmluZylpbnB1dDthZnRlcj0xO319")),
             true),
            ("object_string_surrogate_56320",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHVuaXQ9NTYzMjA7b2JqZWN0IGlucHV0O3N0cmluZyByZXN1bHQ" +
                 "7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9KHN0cmluZykoY2hhcil1bml0O2xsU2xlZXAoMC4xKT" +
                 "tyZXN1bHQ9KHN0cmluZylpbnB1dDthZnRlcj0xO319")),
             true),
            ("object_truth_boolean",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpKDE8Mik7aW50ZWdlciByZXN1bH" +
                                          "Q7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lmKGlucHV0KXJlc3VsdD0xO319")),
             true),
            ("object_truth_integer",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpNztpbnRlZ2VyIHJlc3VsdDtkZW" +
                                          "ZhdWx0e3N0YXRlX2VudHJ5KCl7aWYoaW5wdXQpcmVzdWx0PTE7fX0=")),
             true),
            ("object_truth_null",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpdW5kZWY7aW50ZWdlciByZXN1bH" +
                                          "Q7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lmKGlucHV0KXJlc3VsdD0xO319")),
             true),
            ("object_truth_string",
             System.Text.Encoding.UTF8.GetString(
                 Convert.FromBase64String("WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpIngiO2ludGVnZXIgcmVzdWx0O2" +
                                          "RlZmF1bHR7c3RhdGVfZW50cnkoKXtpZihpbnB1dClyZXN1bHQ9MTt9fQ==")),
             true),
            ("primitive_parameter_from_object",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PTc7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBlY2hvKGludGVnZXIgbil7cmV0dXJ" +
                 "uIG47fWRlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9ZWNobyhpbnB1dCk7fX0=")),
             true),
            ("primitive_return_object",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PTc7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBlY2hvKCl7cmV0dXJuIGlucHV0O31" +
                 "kZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PWVjaG8oKTt9fQ==")),
             true),
            ("reference_boolean",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtvYmplY3QgYWxpYXM9aW5wdXQ" +
                 "7b2JqZWN0IG90aGVyPShvYmplY3QpKDE8Mik7aW50ZWdlciByZXN1bHQ7aW50ZWdlciBhbGlhc19lcXVhbDtpbnRlZ2VyIHZhbH" +
                 "VlX2VxdWFsO2RlZmF1bHR7c3RhdGVfZW50cnkoKXthbGlhc19lcXVhbD0oaW5wdXQ9PWFsaWFzKTtyZXN1bHQ9KGlucHV0PT1vd" +
                 "Ghlcik7dmFsdWVfZXF1YWw9KChzdHJpbmcpaW5wdXQ9PShzdHJpbmcpb3RoZXIpO319")),
             true),
            ("reference_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7b2JqZWN0IGFsaWFzPWlucHV0O29" +
                 "iamVjdCBvdGhlcj0ob2JqZWN0KSdBJztpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFsaWFzX2VxdWFsO2ludGVnZXIgdmFsdWVfZX" +
                 "F1YWw7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2FsaWFzX2VxdWFsPShpbnB1dD09YWxpYXMpO3Jlc3VsdD0oaW5wdXQ9PW90aGVyK" +
                 "Tt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1dD09KHN0cmluZylvdGhlcik7fX0=")),
             true),
            ("reference_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O29iamVjdCBhbGlhcz1pbnB1dDt" +
                 "vYmplY3Qgb3RoZXI9KG9iamVjdCkxLjI1O2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWxpYXNfZXF1YWw7aW50ZWdlciB2YWx1ZV" +
                 "9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7YWxpYXNfZXF1YWw9KGlucHV0PT1hbGlhcyk7cmVzdWx0PShpbnB1dD09b3RoZ" +
                 "XIpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKW90aGVyKTt9fQ==")),
             true),
            ("reference_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O29iamVjdCBhbGlhcz1pbnB1dDtvYmp" +
                 "lY3Qgb3RoZXI9KG9iamVjdCk3O2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWxpYXNfZXF1YWw7aW50ZWdlciB2YWx1ZV9lcXVhbD" +
                 "tkZWZhdWx0e3N0YXRlX2VudHJ5KCl7YWxpYXNfZXF1YWw9KGlucHV0PT1hbGlhcyk7cmVzdWx0PShpbnB1dD09b3RoZXIpO3Zhb" +
                 "HVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKW90aGVyKTt9fQ==")),
             true),
            ("reference_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO29iamVjdCBhbGlhcz1pbnB1dDtvYmplY3Qgb3RoZXI9KG9iamVjdCkoa2V5KSIxMTExMTExMS" +
                 "0yMjIyLTMzMzMtNDQ0NC01NTU1NTU1NTU1NTUiO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWxpYXNfZXF1YWw7aW50ZWdlciB2Y" +
                 "Wx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7YWxpYXNfZXF1YWw9KGlucHV0PT1hbGlhcyk7cmVzdWx0PShpbnB1dD09" +
                 "b3RoZXIpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKW90aGVyKTt9fQ==")),
             true),
            ("reference_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO29iamVjdCBhbGlhcz1pbnB" +
                 "1dDtvYmplY3Qgb3RoZXI9KG9iamVjdClbMSwieCJdO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWxpYXNfZXF1YWw7aW50ZWdlci" +
                 "B2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7YWxpYXNfZXF1YWw9KGlucHV0PT1hbGlhcyk7cmVzdWx0PShpbnB1d" +
                 "D09b3RoZXIpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKW90aGVyKTt9fQ==")),
             true),
            ("reference_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47b2JqZWN0IGFsaWFzPWl" +
                 "ucHV0O29iamVjdCBvdGhlcj0ob2JqZWN0KTwxLDIsMyw0PjtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFsaWFzX2VxdWFsO2ludG" +
                 "VnZXIgdmFsdWVfZXF1YWw7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2FsaWFzX2VxdWFsPShpbnB1dD09YWxpYXMpO3Jlc3VsdD0oa" +
                 "W5wdXQ9PW90aGVyKTt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1dD09KHN0cmluZylvdGhlcik7fX0=")),
             true),
            ("reference_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO29iamVjdCBhbGlhcz1pbnB1dDt" +
                 "vYmplY3Qgb3RoZXI9KG9iamVjdCkiMTIiO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWxpYXNfZXF1YWw7aW50ZWdlciB2YWx1ZV" +
                 "9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7YWxpYXNfZXF1YWw9KGlucHV0PT1hbGlhcyk7cmVzdWx0PShpbnB1dD09b3RoZ" +
                 "XIpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKW90aGVyKTt9fQ==")),
             true),
            ("reference_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O29iamVjdCBhbGlhcz1pbnB1dDtvYmplY3Qgb3RoZXI9KG9iamVjdCk8MSwyLDM+" +
                 "O2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWxpYXNfZXF1YWw7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ" +
                 "5KCl7YWxpYXNfZXF1YWw9KGlucHV0PT1hbGlhcyk7cmVzdWx0PShpbnB1dD09b3RoZXIpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKW" +
                 "lucHV0PT0oc3RyaW5nKW90aGVyKTt9fQ==")),
             true),
            ("string_distinct",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0O29iamVjdCBhbGlhcztpbnRlZ2VyIGJlZm9yZV9lcXVhbDtpbnRlZ2VyIHJlc3V" +
                 "sdDtpbnRlZ2VyIGVudHJpZXM7aW50ZWdlciBhZnRlcjtzdHJpbmcgbWFrZSgpe3JldHVybiBsbEdldFN1YlN0cmluZygieHNhbW" +
                 "UiLDEsLTEpO31kZWZhdWx0e3N0YXRlX2VudHJ5KCl7ZW50cmllcysrO2lucHV0PW1ha2UoKTthbGlhcz1tYWtlKCk7YmVmb3JlX" +
                 "2VxdWFsPShpbnB1dD09YWxpYXMpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTthZnRlcj0xO319")),
             true),
            ("string_distinct_sleep",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0O29iamVjdCBhbGlhcztpbnRlZ2VyIGJlZm9yZV9lcXVhbDtpbnRlZ2VyIHJlc3V" +
                 "sdDtpbnRlZ2VyIGVudHJpZXM7aW50ZWdlciBhZnRlcjtzdHJpbmcgbWFrZSgpe3JldHVybiBsbEdldFN1YlN0cmluZygieHNhbW" +
                 "UiLDEsLTEpO31kZWZhdWx0e3N0YXRlX2VudHJ5KCl7ZW50cmllcysrO2lucHV0PW1ha2UoKTthbGlhcz1tYWtlKCk7YmVmb3JlX" +
                 "2VxdWFsPShpbnB1dD09YWxpYXMpO2xsU2xlZXAoMC4xKTtyZXN1bHQ9KGlucHV0PT1hbGlhcyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("string_identity_base64_roundtrip",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbEJhc2U2NFRvU3RyaW" +
                 "5nKGxsU3RyaW5nVG9CYXNlNjQodGV4dCkpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1d" +
                 "D09KHN0cmluZylhbGlhcyk7fX0=")),
             true),
            ("string_identity_concat_empty",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz10ZXh0KyIiO3Jlc3VsdD" +
                 "0oaW5wdXQ9PWFsaWFzKTt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1dD09KHN0cmluZylhbGlhcyk7fX0=")),
             true),
            ("string_identity_csv_extract",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbExpc3QyU3RyaW5nKG" +
                 "xsQ1NWMkxpc3QodGV4dCksMCk7cmVzdWx0PShpbnB1dD09YWxpYXMpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3Rya" +
                 "W5nKWFsaWFzKTt9fQ==")),
             true),
            ("string_identity_empty_concat",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz0iIit0ZXh0O3Jlc3VsdD" +
                 "0oaW5wdXQ9PWFsaWFzKTt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1dD09KHN0cmluZylhbGlhcyk7fX0=")),
             true),
            ("string_identity_list_dump",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbER1bXBMaXN0MlN0cm" +
                 "luZyhbdGV4dF0sIiIpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1dD09KHN0cmluZylhb" +
                 "Glhcyk7fX0=")),
             true),
            ("string_identity_list_extract",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbExpc3QyU3RyaW5nKF" +
                 "t0ZXh0XSwwKTtyZXN1bHQ9KGlucHV0PT1hbGlhcyk7dmFsdWVfZXF1YWw9KChzdHJpbmcpaW5wdXQ9PShzdHJpbmcpYWxpYXMpO" +
                 "319")),
             true),
            ("string_identity_list_soup",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz0oc3RyaW5nKVt0ZXh0XT" +
                 "tyZXN1bHQ9KGlucHV0PT1hbGlhcyk7dmFsdWVfZXF1YWw9KChzdHJpbmcpaW5wdXQ9PShzdHJpbmcpYWxpYXMpO319")),
             true),
            ("string_identity_lower_nochange",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbFRvTG93ZXIodGV4dC" +
                 "k7cmVzdWx0PShpbnB1dD09YWxpYXMpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKWFsaWFzKTt9fQ==")),
             true),
            ("string_identity_substring_full",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbEdldFN1YlN0cmluZy" +
                 "h0ZXh0LDAsLTEpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt2YWx1ZV9lcXVhbD0oKHN0cmluZylpbnB1dD09KHN0cmluZylhbGlhc" +
                 "yk7fX0=")),
             true),
            ("string_identity_trim_nochange",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbFN0cmluZ1RyaW0odG" +
                 "V4dCxTVFJJTkdfVFJJTSk7cmVzdWx0PShpbnB1dD09YWxpYXMpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nK" +
                 "WFsaWFzKTt9fQ==")),
             true),
            ("string_identity_trim_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9InNhbWUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbFN0cmluZ1RyaW0odG" +
                 "V4dCwwKTtyZXN1bHQ9KGlucHV0PT1hbGlhcyk7dmFsdWVfZXF1YWw9KChzdHJpbmcpaW5wdXQ9PShzdHJpbmcpYWxpYXMpO319")),
             true),
            ("string_identity_upper_nochange",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ9IlNBTUUiO29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciB2YWx1ZV9lcXVhbDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9dGV4dDthbGlhcz1sbFRvVXBwZXIodGV4dC" +
                 "k7cmVzdWx0PShpbnB1dD09YWxpYXMpO3ZhbHVlX2VxdWFsPSgoc3RyaW5nKWlucHV0PT0oc3RyaW5nKWFsaWFzKTt9fQ==")),
             true),
            ("string_object_float_invalid",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSJub3RudW1iZXIiO2Zsb2F0IHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR" +
                 "7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGZsb2F0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("string_object_float_overflow",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSIxZTMwOSI7ZmxvYXQgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF" +
                 "0ZV9lbnRyeSgpe3Jlc3VsdD0oZmxvYXQpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("string_object_integer_hex",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSIweDEwIjtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3R" +
                 "hdGVfZW50cnkoKXtyZXN1bHQ9KGludGVnZXIpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("string_object_integer_invalid",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSJub3RudW1iZXIiO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXV" +
                 "sdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oaW50ZWdlcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("string_object_integer_overflow",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSIyMTQ3NDgzNjQ4IjtpbnRlZ2VyIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF" +
                 "1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGludGVnZXIpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("string_object_rotation_invalid",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSJpbnZhbGlkIjtyb3RhdGlvbiByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx" +
                 "0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShyb3RhdGlvbilpbnB1dDthZnRlcj0xO319")),
             true),
            ("string_object_vector_invalid",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PSJpbnZhbGlkIjt2ZWN0b3IgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHt" +
                 "zdGF0ZV9lbnRyeSgpe3Jlc3VsdD0odmVjdG9yKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_boolean_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtjaGFyIHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGNoYXIpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_boolean_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtmbG9hdCByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShmbG9hdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_boolean_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtpbnRlZ2VyIHJlc3VsdDtpbnR" +
                 "lZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGludGVnZXIpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_boolean_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtrZXkgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oa2V5KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_boolean_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtsaXN0IHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGxpc3QpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_boolean_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtyb3RhdGlvbiByZXN1bHQ7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShyb3RhdGlvbilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_boolean_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTtzdHJpbmcgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oc3RyaW5nKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_boolean_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoMTwyKTt2ZWN0b3IgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0odmVjdG9yKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_char_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7Y2hhciByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShjaGFyKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_char_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7ZmxvYXQgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oZmxvYXQpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_char_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7aW50ZWdlciByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShpbnRlZ2VyKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_char_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7a2V5IHJlc3VsdDtpbnRlZ2VyIGF" +
                 "mdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGtleSlpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_char_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7bGlzdCByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShsaXN0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_char_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7cm90YXRpb24gcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0ocm90YXRpb24paW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_char_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7c3RyaW5nIHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHN0cmluZylpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_char_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCknQSc7dmVjdG9yIHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHZlY3RvcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_dynamic_boolean_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtjaGFyIHJlc3VsdDtzdHJ" +
                 "pbmcgdHlwZW5hbWU7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9KHNvdXJjZTwyKTt0eXBlbmFtZT" +
                 "14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PShjaGFyKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_dynamic_boolean_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtmbG9hdCByZXN1bHQ7c3R" +
                 "yaW5nIHR5cGVuYW1lO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PShzb3VyY2U8Mik7dHlwZW5hbW" +
                 "U9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3VsdD0oZmxvYXQpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_dynamic_boolean_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtpbnRlZ2VyIHJlc3VsdDt" +
                 "zdHJpbmcgdHlwZW5hbWU7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9KHNvdXJjZTwyKTt0eXBlbm" +
                 "FtZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PShpbnRlZ2VyKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_dynamic_boolean_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtrZXkgcmVzdWx0O3N0cml" +
                 "uZyB0eXBlbmFtZTtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtpbnB1dD0oc291cmNlPDIpO3R5cGVuYW1lPX" +
                 "htclR5cGVOYW1lKGlucHV0KTtyZXN1bHQ9KGtleSlpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_dynamic_boolean_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtsaXN0IHJlc3VsdDtzdHJ" +
                 "pbmcgdHlwZW5hbWU7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9KHNvdXJjZTwyKTt0eXBlbmFtZT" +
                 "14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PShsaXN0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_dynamic_boolean_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtyb3RhdGlvbiByZXN1bHQ" +
                 "7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PShzb3VyY2U8Mik7dHlwZW" +
                 "5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3VsdD0ocm90YXRpb24paW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_dynamic_boolean_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDtzdHJpbmcgcmVzdWx0O3N" +
                 "0cmluZyB0eXBlbmFtZTtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtpbnB1dD0oc291cmNlPDIpO3R5cGVuYW" +
                 "1lPXhtclR5cGVOYW1lKGlucHV0KTtyZXN1bHQ9KHN0cmluZylpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_dynamic_boolean_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIHNvdXJjZT0xO29iamVjdCBpbnB1dDt2ZWN0b3IgcmVzdWx0O3N" +
                 "0cmluZyB0eXBlbmFtZTtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtpbnB1dD0oc291cmNlPDIpO3R5cGVuYW" +
                 "1lPXhtclR5cGVOYW1lKGlucHV0KTtyZXN1bHQ9KHZlY3RvcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_float_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O2NoYXIgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oY2hhcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_float_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O2Zsb2F0IHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGZsb2F0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_float_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O2ludGVnZXIgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oaW50ZWdlcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_float_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O2tleSByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShrZXkpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_float_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O2xpc3QgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0obGlzdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_float_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O3JvdGF0aW9uIHJlc3VsdDtpbnR" +
                 "lZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHJvdGF0aW9uKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_float_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O3N0cmluZyByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShzdHJpbmcpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_float_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkxLjI1O3ZlY3RvciByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PSh2ZWN0b3IpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_integer_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2NoYXIgcmVzdWx0O2ludGVnZXIgYWZ" +
                 "0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oY2hhcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_integer_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2Zsb2F0IHJlc3VsdDtpbnRlZ2VyIGF" +
                 "mdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGZsb2F0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_integer_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2ludGVnZXIgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oaW50ZWdlcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_integer_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2tleSByZXN1bHQ7aW50ZWdlciBhZnR" +
                 "lcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShrZXkpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_integer_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O2xpc3QgcmVzdWx0O2ludGVnZXIgYWZ" +
                 "0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0obGlzdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_integer_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3JvdGF0aW9uIHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHJvdGF0aW9uKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_integer_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3N0cmluZyByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShzdHJpbmcpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_integer_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk3O3ZlY3RvciByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PSh2ZWN0b3IpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_key_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO2NoYXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD" +
                 "0oY2hhcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_key_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO2Zsb2F0IHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bH" +
                 "Q9KGZsb2F0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_key_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3" +
                 "VsdD0oaW50ZWdlcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_key_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO2tleSByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PS" +
                 "hrZXkpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_key_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO2xpc3QgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD" +
                 "0obGlzdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_key_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO3JvdGF0aW9uIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZX" +
                 "N1bHQ9KHJvdGF0aW9uKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_key_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO3N0cmluZyByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdW" +
                 "x0PShzdHJpbmcpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_key_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkoa2V5KSIxMTExMTExMS0yMjIyLTMzMzM" +
                 "tNDQ0NC01NTU1NTU1NTU1NTUiO3ZlY3RvciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdW" +
                 "x0PSh2ZWN0b3IpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_list_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO2NoYXIgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oY2hhcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_list_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO2Zsb2F0IHJlc3VsdDtpbnR" +
                 "lZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGZsb2F0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_list_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oaW50ZWdlcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_list_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO2tleSByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShrZXkpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_list_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO2xpc3QgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0obGlzdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_list_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO3JvdGF0aW9uIHJlc3VsdDt" +
                 "pbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHJvdGF0aW9uKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_list_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO3N0cmluZyByZXN1bHQ7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShzdHJpbmcpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_list_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdClbMSwieCJdO3ZlY3RvciByZXN1bHQ7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PSh2ZWN0b3IpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_null_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtjaGFyIHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGNoYXIpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_null_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtmbG9hdCByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShmbG9hdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_null_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtpbnRlZ2VyIHJlc3VsdDtpbnR" +
                 "lZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGludGVnZXIpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_null_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtrZXkgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oa2V5KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_null_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtsaXN0IHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGxpc3QpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_null_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtyb3RhdGlvbiByZXN1bHQ7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShyb3RhdGlvbilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_null_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjtzdHJpbmcgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oc3RyaW5nKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_null_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCl1bmRlZjt2ZWN0b3IgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0odmVjdG9yKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_rotation_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47Y2hhciByZXN1bHQ7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShjaGFyKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_rotation_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47ZmxvYXQgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oZmxvYXQpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_rotation_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47aW50ZWdlciByZXN1bHQ" +
                 "7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShpbnRlZ2VyKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_rotation_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47a2V5IHJlc3VsdDtpbnR" +
                 "lZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGtleSlpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_rotation_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47bGlzdCByZXN1bHQ7aW5" +
                 "0ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShsaXN0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_rotation_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47cm90YXRpb24gcmVzdWx" +
                 "0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0ocm90YXRpb24paW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_rotation_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47c3RyaW5nIHJlc3VsdDt" +
                 "pbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHN0cmluZylpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_rotation_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDMsND47dmVjdG9yIHJlc3VsdDt" +
                 "pbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHZlY3RvcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_string_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO2NoYXIgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oY2hhcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_string_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO2Zsb2F0IHJlc3VsdDtpbnRlZ2V" +
                 "yIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGZsb2F0KWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_string_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO2ludGVnZXIgcmVzdWx0O2ludGV" +
                 "nZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oaW50ZWdlcilpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_string_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO2tleSByZXN1bHQ7aW50ZWdlciB" +
                 "hZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShrZXkpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_string_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO2xpc3QgcmVzdWx0O2ludGVnZXI" +
                 "gYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0obGlzdClpbnB1dDthZnRlcj0xO319")),
             true),
            ("unbox_string_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO3JvdGF0aW9uIHJlc3VsdDtpbnR" +
                 "lZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHJvdGF0aW9uKWlucHV0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_string_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO3N0cmluZyByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShzdHJpbmcpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_string_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCkiMTIiO3ZlY3RvciByZXN1bHQ7aW50ZWd" +
                 "lciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PSh2ZWN0b3IpaW5wdXQ7YWZ0ZXI9MTt9fQ==")),
             true),
            ("unbox_vector_to_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O2NoYXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oY2hhcilpbnB1dDthZnRlcj0" +
                 "xO319")),
             true),
            ("unbox_vector_to_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O2Zsb2F0IHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KGZsb2F0KWlucHV0O2FmdGV" +
                 "yPTE7fX0=")),
             true),
            ("unbox_vector_to_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O2ludGVnZXIgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0oaW50ZWdlcilpbnB1dDt" +
                 "hZnRlcj0xO319")),
             true),
            ("unbox_vector_to_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O2tleSByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShrZXkpaW5wdXQ7YWZ0ZXI9MTt" +
                 "9fQ==")),
             true),
            ("unbox_vector_to_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O2xpc3QgcmVzdWx0O2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe3Jlc3VsdD0obGlzdClpbnB1dDthZnRlcj0" +
                 "xO319")),
             true),
            ("unbox_vector_to_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O3JvdGF0aW9uIHJlc3VsdDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtyZXN1bHQ9KHJvdGF0aW9uKWlucHV" +
                 "0O2FmdGVyPTE7fX0=")),
             true),
            ("unbox_vector_to_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O3N0cmluZyByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PShzdHJpbmcpaW5wdXQ7YWZ" +
                 "0ZXI9MTt9fQ==")),
             true),
            ("unbox_vector_to_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ9KG9iamVjdCk8MSwyLDM+" +
                 "O3ZlY3RvciByZXN1bHQ7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7cmVzdWx0PSh2ZWN0b3IpaW5wdXQ7YWZ" +
                 "0ZXI9MTt9fQ==")),
             true),
            ("object_list_index_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVs3XVswXTt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dC" +
                 "k7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("object_list_index_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVsxLjI1XVswXTt0eXBlbmFtZT14bXJUeXBlTmFtZShpbn" +
                 "B1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("object_list_index_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVsidGV4dCJdWzBdO3R5cGVuYW1lPXhtclR5cGVOYW1lKG" +
                 "lucHV0KTtyZXN1bHQ9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("object_list_index_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVsoa2V5KSIxMTExMTExMS0yMjIyLTMzMzMtNDQ0NC01NT" +
                 "U1NTU1NTU1NTUiXVswXTt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlc" +
                 "j0xO319")),
             true),
            ("object_list_index_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVsnQSddWzBdO3R5cGVuYW1lPXhtclR5cGVOYW1lKGlucH" +
                 "V0KTtyZXN1bHQ9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("object_list_index_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVs8MSwyLDM+" +
                 "XVswXTt0eXBlbmFtZT14bXJUeXBlTmFtZShpbnB1dCk7cmVzdWx0PXhtckhhc2hDb2RlKGlucHV0KTthZnRlcj0xO319")),
             true),
            ("object_list_index_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVs8MSwyLDMsND5dWzBdO3R5cGVuYW1lPXhtclR5cGVOYW" +
                 "1lKGlucHV0KTtyZXN1bHQ9eG1ySGFzaENvZGUoaW5wdXQpO2FmdGVyPTE7fX0=")),
             true),
            ("object_list_index_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIHR5cGVuYW1lO2ludGVnZXIgcmVzdWx0O2l" +
                 "udGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2lucHV0PVtbMSwyXV1bMF07dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW" +
                 "5wdXQpO3Jlc3VsdD14bXJIYXNoQ29kZShpbnB1dCk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_list_index_alias_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bN107bGlzdCBjb3B5O29iamVjdCBpbnB1dDtvYmplY3Q" +
                 "gYWxpYXM7b2JqZWN0IG90aGVyO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBjb3BpZWQ7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2" +
                 "NvcHk9dmFsdWVzK1tdO2lucHV0PXZhbHVlc1swXTthbGlhcz12YWx1ZXNbMF07b3RoZXI9Y29weVswXTtyZXBlYXRlZD0oaW5wd" +
                 "XQ9PWFsaWFzKTtjb3BpZWQ9KGlucHV0PT1vdGhlcik7fX0=")),
             true),
            ("object_list_index_alias_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bMS4yNV07bGlzdCBjb3B5O29iamVjdCBpbnB1dDtvYmp" +
                 "lY3QgYWxpYXM7b2JqZWN0IG90aGVyO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBjb3BpZWQ7ZGVmYXVsdHtzdGF0ZV9lbnRyeS" +
                 "gpe2NvcHk9dmFsdWVzK1tdO2lucHV0PXZhbHVlc1swXTthbGlhcz12YWx1ZXNbMF07b3RoZXI9Y29weVswXTtyZXBlYXRlZD0oa" +
                 "W5wdXQ9PWFsaWFzKTtjb3BpZWQ9KGlucHV0PT1vdGhlcik7fX0=")),
             true),
            ("object_list_index_alias_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bInRleHQiXTtsaXN0IGNvcHk7b2JqZWN0IGlucHV0O29" +
                 "iamVjdCBhbGlhcztvYmplY3Qgb3RoZXI7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGNvcGllZDtkZWZhdWx0e3N0YXRlX2VudH" +
                 "J5KCl7Y29weT12YWx1ZXMrW107aW5wdXQ9dmFsdWVzWzBdO2FsaWFzPXZhbHVlc1swXTtvdGhlcj1jb3B5WzBdO3JlcGVhdGVkP" +
                 "ShpbnB1dD09YWxpYXMpO2NvcGllZD0oaW5wdXQ9PW90aGVyKTt9fQ==")),
             true),
            ("object_list_index_alias_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bKGtleSkiMTExMTExMTEtMjIyMi0zMzMzLTQ0NDQtNTU" +
                 "1NTU1NTU1NTU1Il07bGlzdCBjb3B5O29iamVjdCBpbnB1dDtvYmplY3QgYWxpYXM7b2JqZWN0IG90aGVyO2ludGVnZXIgcmVwZW" +
                 "F0ZWQ7aW50ZWdlciBjb3BpZWQ7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2NvcHk9dmFsdWVzK1tdO2lucHV0PXZhbHVlc1swXTthb" +
                 "Glhcz12YWx1ZXNbMF07b3RoZXI9Y29weVswXTtyZXBlYXRlZD0oaW5wdXQ9PWFsaWFzKTtjb3BpZWQ9KGlucHV0PT1vdGhlcik7" +
                 "fX0=")),
             true),
            ("object_list_index_alias_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bJ0EnXTtsaXN0IGNvcHk7b2JqZWN0IGlucHV0O29iamV" +
                 "jdCBhbGlhcztvYmplY3Qgb3RoZXI7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGNvcGllZDtkZWZhdWx0e3N0YXRlX2VudHJ5KC" +
                 "l7Y29weT12YWx1ZXMrW107aW5wdXQ9dmFsdWVzWzBdO2FsaWFzPXZhbHVlc1swXTtvdGhlcj1jb3B5WzBdO3JlcGVhdGVkPShpb" +
                 "nB1dD09YWxpYXMpO2NvcGllZD0oaW5wdXQ9PW90aGVyKTt9fQ==")),
             true),
            ("object_list_index_alias_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bPDEsMiwzPl07bGlzdCBjb3B5O29iamVjdCBpbnB1dDt" +
                 "vYmplY3QgYWxpYXM7b2JqZWN0IG90aGVyO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBjb3BpZWQ7ZGVmYXVsdHtzdGF0ZV9lbn" +
                 "RyeSgpe2NvcHk9dmFsdWVzK1tdO2lucHV0PXZhbHVlc1swXTthbGlhcz12YWx1ZXNbMF07b3RoZXI9Y29weVswXTtyZXBlYXRlZ" +
                 "D0oaW5wdXQ9PWFsaWFzKTtjb3BpZWQ9KGlucHV0PT1vdGhlcik7fX0=")),
             true),
            ("object_list_index_alias_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bPDEsMiwzLDQ+" +
                 "XTtsaXN0IGNvcHk7b2JqZWN0IGlucHV0O29iamVjdCBhbGlhcztvYmplY3Qgb3RoZXI7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2V" +
                 "yIGNvcGllZDtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Y29weT12YWx1ZXMrW107aW5wdXQ9dmFsdWVzWzBdO2FsaWFzPXZhbHVlc1" +
                 "swXTtvdGhlcj1jb3B5WzBdO3JlcGVhdGVkPShpbnB1dD09YWxpYXMpO2NvcGllZD0oaW5wdXQ9PW90aGVyKTt9fQ==")),
             true),
            ("object_list_index_alias_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IHZhbHVlcz1bWzEsMl1dO2xpc3QgY29weTtvYmplY3QgaW5wdXQ7b2J" +
                 "qZWN0IGFsaWFzO29iamVjdCBvdGhlcjtpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgY29waWVkO2RlZmF1bHR7c3RhdGVfZW50cn" +
                 "koKXtjb3B5PXZhbHVlcytbXTtpbnB1dD12YWx1ZXNbMF07YWxpYXM9dmFsdWVzWzBdO290aGVyPWNvcHlbMF07cmVwZWF0ZWQ9K" +
                 "GlucHV0PT1hbGlhcyk7Y29waWVkPShpbnB1dD09b3RoZXIpO319")),
             true),
            ("object_function_alias_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpNztvYmplY3QgYWxpYXM7aW50ZWdlciByZXN1bHQ7b2JqZWN0IGV" +
                 "jaG8ob2JqZWN0IGFyZ3VtZW50KXtyZXR1cm4gYXJndW1lbnQ7fWRlZmF1bHR7c3RhdGVfZW50cnkoKXthbGlhcz1lY2hvKGlucH" +
                 "V0KTtyZXN1bHQ9KGlucHV0PT1hbGlhcyk7fX0=")),
             true),
            ("object_function_alias_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpInRleHQiO29iamVjdCBhbGlhcztpbnRlZ2VyIHJlc3VsdDtvYmp" +
                 "lY3QgZWNobyhvYmplY3QgYXJndW1lbnQpe3JldHVybiBhcmd1bWVudDt9ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2FsaWFzPWVjaG" +
                 "8oaW5wdXQpO3Jlc3VsdD0oaW5wdXQ9PWFsaWFzKTt9fQ==")),
             true),
            ("object_function_alias_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7b2JqZWN0IGlucHV0PShvYmplY3QpWzEsMl07b2JqZWN0IGFsaWFzO2ludGVnZXIgcmVzdWx0O29iamV" +
                 "jdCBlY2hvKG9iamVjdCBhcmd1bWVudCl7cmV0dXJuIGFyZ3VtZW50O31kZWZhdWx0e3N0YXRlX2VudHJ5KCl7YWxpYXM9ZWNoby" +
                 "hpbnB1dCk7cmVzdWx0PShpbnB1dD09YWxpYXMpO319")),
             true),
            ("object_boxed_null_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ7b2JqZWN0IGlucHV0O2ludGVnZXIgaXNudWxsO3N0cmluZyB0eXBlbmFtZTtzdHJ" +
                 "pbmcgcmVzdWx0O2RlZmF1bHR7c3RhdGVfZW50cnkoKXt0ZXh0PWxsQ29tcHV0ZUhhc2goInBheWxvYWQiLCJ1bnN1cHBvcnRlZC" +
                 "IpO2lucHV0PVt0ZXh0XVswXTtpc251bGw9KGlucHV0PT11bmRlZik7dHlwZW5hbWU9eG1yVHlwZU5hbWUoaW5wdXQpO3Jlc3Vsd" +
                 "D0oc3RyaW5nKWlucHV0O319")),
             true),
            ("object_raw_null_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7c3RyaW5nIHRleHQ7b2JqZWN0IGlucHV0O2ludGVnZXIgaXNudWxsO3N0cmluZyByZXN1bHQ7ZGVmYXV" +
                 "sdHtzdGF0ZV9lbnRyeSgpe3RleHQ9bGxDb21wdXRlSGFzaCgicGF5bG9hZCIsInVuc3VwcG9ydGVkIik7aW5wdXQ9dGV4dDtpc2" +
                 "51bGw9KGlucHV0PT11bmRlZik7cmVzdWx0PShzdHJpbmcpaW5wdXQ7fX0=")),
             true)
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
                var result = new List<object>();
                for (int a = 0; a < items.Count; a++)
                    for (int c = a + 1; c < items.Count; c++)
                        result.Add(new { left = items[a].name, right = items[c].name,
                                         same_reference = object.ReferenceEquals(items[a].value, items[c].value),
                                         object_equals = object.Equals(items[a].value, items[c].value) });
                return result;
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
                                     slot_types = Array.ConvertAll(f.objArray, value => value?.GetType().FullName),
                                     slot_values = Array.ConvertAll(f.objArray, Typed),
                                     wait_deadline_utc_ticks = f.funcName == "xmrEventDequeue"
                                                                   ? (long?)DateTime.Parse((string)f.objArray[0]).Ticks
                                                                   : null });
                return new { globals,
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
                    var fresh = Migrate(production);
                    RestoreQueue(fresh, queueXml);
                    afterLoad = State(fresh);
                    migrated = true;
                    Slice(fresh);
                    var error = fresh.ResumeEx();
                    final = State(fresh);
                    if (error != null)
                        resumeError = Failure(error);
                }
                if (permissive != null) {
                    var fresh = Migrate(permissive);
                    RestoreQueue(fresh, queueXml);
                    Slice(fresh);
                    var error = fresh.ResumeEx();
                    permissiveResume = State(fresh);
                    if (error != null)
                        permissiveResumeError = Failure(error);
                }
                Slice(instance);
                var liveFailure = instance.ResumeEx();
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
            hashes["generator_assembly"] = Sha(typeof(ObjectValueReference).Assembly.Location);
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
