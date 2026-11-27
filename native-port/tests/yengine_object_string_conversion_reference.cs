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

public static class ObjectStringConversionReference {
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
                                   typeof(ObjectStringConversionReference), true);
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
        il.Emit(OpCodes.Call, typeof(ObjectStringConversionReference).GetMethod(nameof(Check)));
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
            ("bool_false",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykoaW5wdXQ+" +
                 "MCk7c2Vjb25kPShzdHJpbmcpKGlucHV0PjApO2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g" +
                 "9KGE9PShvYmplY3QpIjAiKTt0ZXh0X21hdGNoPShmaXJzdD09IjAiKTthZ2Fpbj0oc3RyaW5nKShpbnB1dD4wKTtjPWFnYWluO2" +
                 "FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("bool_true",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTE7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykoaW5wdXQ+" +
                 "MCk7c2Vjb25kPShzdHJpbmcpKGlucHV0PjApO2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g" +
                 "9KGE9PShvYmplY3QpIjEiKTt0ZXh0X21hdGNoPShmaXJzdD09IjEiKTthZ2Fpbj0oc3RyaW5nKShpbnB1dD4wKTtjPWFnYWluO2" +
                 "FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("char_ascii",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTY1O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKGNoYXIpaW5wdXQ7c2Vjb25kPShzdHJpbmcpKGNoYXIp" +
                 "aW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiQSIpO3RleHR" +
                 "fbWF0Y2g9KGZpcnN0PT0iQSIpO2FnYWluPShzdHJpbmcpKGNoYXIpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW" +
                 "5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("char_bmp",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTk1NTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKShjaGFyKWlucHV0O3NlY29uZD0oc3RyaW5nKShjaGFy" +
                 "KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIs67Iik7dGV" +
                 "4dF9tYXRjaD0oZmlyc3Q9PSLOuyIpO2FnYWluPShzdHJpbmcpKGNoYXIpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKT" +
                 "tnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("char_high",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTU1Mjk2O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKGNoYXIpaW5wdXQ7c2Vjb25kPShzdHJpbmcpKGNo" +
                 "YXIpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCki77+" +
                 "9Iik7dGV4dF9tYXRjaD0oZmlyc3Q9PSLvv70iKTthZ2Fpbj0oc3RyaW5nKShjaGFyKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj" +
                 "0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("char_low",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTU2MzIwO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKGNoYXIpaW5wdXQ7c2Vjb25kPShzdHJpbmcpKGNo" +
                 "YXIpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCki77+" +
                 "9Iik7dGV4dF9tYXRjaD0oZmlyc3Q9PSLvv70iKTthZ2Fpbj0oc3RyaW5nKShjaGFyKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj" +
                 "0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("char_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykoY2hhcilpbnB1dDtzZWNvbmQ9KHN0cmluZykoY2hhcilp" +
                 "bnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIAIik7dGV4dF9" +
                 "tYXRjaD0oZmlyc3Q9PSIAIik7YWdhaW49KHN0cmluZykoY2hhcilpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbm" +
                 "VyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("float_finite",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0xLjI1O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiMS4yNTAwMDAiKTt0ZXh0X21hdGN" +
                 "oPShmaXJzdD09IjEuMjUwMDAwIik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYX" +
                 "RlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("float_integer42",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD00Mi4wO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiNDIuMDAwMDAwIik7dGV4dF9tYXR" +
                 "jaD0oZmlyc3Q9PSI0Mi4wMDAwMDAiKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZX" +
                 "JhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("float_nan",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9bGxTcXJ0KC0xLjApO2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShz" +
                 "dHJpbmcpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiTmF" +
                 "OIik7dGV4dF9tYXRjaD0oZmlyc3Q9PSJOYU4iKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yi" +
                 "k7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("float_negative_infinity",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9bGxMb2coMC4wKTtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3Ry" +
                 "aW5nKWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIi1JbmZ" +
                 "pbml0eSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iLUluZmluaXR5Iik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3" +
                 "BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("float_negative_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0tMC4wO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiLTAuMDAwMDAwIik7dGV4dF9tYXR" +
                 "jaD0oZmlyc3Q9PSItMC4wMDAwMDAiKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZX" +
                 "JhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("float_one",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0xLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1dDthPWZpcnN0" +
                 "O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIxLjAwMDAwMCIpO3RleHRfbWF0Y2g" +
                 "9KGZpcnN0PT0iMS4wMDAwMDAiKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdG" +
                 "VkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("float_positive_infinity",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9LWxsTG9nKDAuMCk7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0" +
                 "cmluZylpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSJJbmZ" +
                 "pbml0eSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iSW5maW5pdHkiKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcG" +
                 "Fpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("float_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1dDthPWZpcnN0" +
                 "O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIwLjAwMDAwMCIpO3RleHRfbWF0Y2g" +
                 "9KGZpcnN0PT0iMC4wMDAwMDAiKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdG" +
                 "VkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("integer_-1",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PS0xO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiLTEiKTt0ZXh0X21hdGNoPShmaXJ" +
                 "zdD09Ii0xIik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT" +
                 "09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("integer_0",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1dDthPWZpcnN0" +
                 "O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIwIik7dGV4dF9tYXRjaD0oZmlyc3Q" +
                 "9PSIwIik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yy" +
                 "k7YWZ0ZXI9MTt9fQ==")),
             true),
            ("integer_1",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTE7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1dDthPWZpcnN0" +
                 "O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIxIik7dGV4dF9tYXRjaD0oZmlyc3Q" +
                 "9PSIxIik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yy" +
                 "k7YWZ0ZXI9MTt9fQ==")),
             true),
            ("integer_299",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTI5OTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3RyaW5nKWlucHV0O2E9Zmly" +
                 "c3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjI5OSIpO3RleHRfbWF0Y2g9KGZ" +
                 "pcnN0PT0iMjk5Iik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD" +
                 "0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("integer_300",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTMwMDtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3RyaW5nKWlucHV0O2E9Zmly" +
                 "c3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjMwMCIpO3RleHRfbWF0Y2g9KGZ" +
                 "pcnN0PT0iMzAwIik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD" +
                 "0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("integer_42",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTQyO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiNDIiKTt0ZXh0X21hdGNoPShmaXJ" +
                 "zdD09IjQyIik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT" +
                 "09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("integer_concat_empty",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTQyO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PSIiK2lucHV0O3NlY29uZD0iIitpbnB1dDthPWZpcnN0O2I9c2Vjb25k" +
                 "O3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSI0MiIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iNDIiKTt" +
                 "hZ2Fpbj0iIitpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ" +
                 "==")),
             true),
            ("null_object_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztvYmplY3QgaW5wdXQ7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3RyaW5" +
                 "nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdGNoO2" +
                 "ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZnRlc" +
                 "jtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1dDthPWZpcnN0O2I9" +
                 "c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIiKTt0ZXh0X21hdGNoPShmaXJzdD09IiI" +
                 "pO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdG" +
                 "VyPTE7fX0=")),
             true),
            ("object_bool_false",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KShpbnB1dD4wKTtzZWNvbmQ9KHN0cmluZyko" +
                 "b2JqZWN0KShpbnB1dD4wKTthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN" +
                 "0KSJGYWxzZSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iRmFsc2UiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpKGlucHV0PjApO2M9YW" +
                 "dhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("object_bool_true",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTE7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KShpbnB1dD4wKTtzZWNvbmQ9KHN0cmluZyko" +
                 "b2JqZWN0KShpbnB1dD4wKTthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN" +
                 "0KSJUcnVlIik7dGV4dF9tYXRjaD0oZmlyc3Q9PSJUcnVlIik7YWdhaW49KHN0cmluZykob2JqZWN0KShpbnB1dD4wKTtjPWFnYW" +
                 "luO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_char_ascii",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTY1O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdCkoY2hhcilpbnB1dDtzZWNvbmQ9KHN0cmlu" +
                 "Zykob2JqZWN0KShjaGFyKWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PSh" +
                 "vYmplY3QpIkEiKTt0ZXh0X21hdGNoPShmaXJzdD09IkEiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpKGNoYXIpaW5wdXQ7Yz1hZ2" +
                 "FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_char_bmp",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTk1NTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKShvYmplY3QpKGNoYXIpaW5wdXQ7c2Vjb25kPShzdHJp" +
                 "bmcpKG9iamVjdCkoY2hhcilpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0" +
                 "ob2JqZWN0KSLOuyIpO3RleHRfbWF0Y2g9KGZpcnN0PT0izrsiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpKGNoYXIpaW5wdXQ7Yz" +
                 "1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_char_high",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTU1Mjk2O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdCkoY2hhcilpbnB1dDtzZWNvbmQ9KHN0" +
                 "cmluZykob2JqZWN0KShjaGFyKWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE" +
                 "9PShvYmplY3QpIu+/" +
                 "vSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0i77+" +
                 "9Iik7YWdhaW49KHN0cmluZykob2JqZWN0KShjaGFyKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX2" +
                 "1hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("object_char_low",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTU2MzIwO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdCkoY2hhcilpbnB1dDtzZWNvbmQ9KHN0" +
                 "cmluZykob2JqZWN0KShjaGFyKWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE" +
                 "9PShvYmplY3QpIu+/" +
                 "vSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0i77+" +
                 "9Iik7YWdhaW49KHN0cmluZykob2JqZWN0KShjaGFyKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX2" +
                 "1hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("object_char_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KShjaGFyKWlucHV0O3NlY29uZD0oc3RyaW5n" +
                 "KShvYmplY3QpKGNoYXIpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9" +
                 "iamVjdCkiACIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iACIpO2FnYWluPShzdHJpbmcpKG9iamVjdCkoY2hhcilpbnB1dDtjPWFnYW" +
                 "luO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_float_finite",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0xLjI1O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2Jq" +
                 "ZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjEuMjU" +
                 "iKTt0ZXh0X21hdGNoPShmaXJzdD09IjEuMjUiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYW" +
                 "lyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_float_integer42",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD00Mi4wO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2Jq" +
                 "ZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjQyIik" +
                 "7dGV4dF9tYXRjaD0oZmlyc3Q9PSI0MiIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KG" +
                 "E9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_float_nan",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9bGxTcXJ0KC0xLjApO2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtz" +
                 "ZWNvbmQ9KHN0cmluZykob2JqZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g" +
                 "9KGE9PShvYmplY3QpIk5hTiIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iTmFOIik7YWdhaW49KHN0cmluZykob2JqZWN0KWlucHV0O2" +
                 "M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("object_float_negative_infinity",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9bGxMb2coMC4wKTtmaXJzdD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7c2Vj" +
                 "b25kPShzdHJpbmcpKG9iamVjdClpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPSh" +
                 "hPT0ob2JqZWN0KSItSW5maW5pdHkiKTt0ZXh0X21hdGNoPShmaXJzdD09Ii1JbmZpbml0eSIpO2FnYWluPShzdHJpbmcpKG9iam" +
                 "VjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_float_negative_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0tMC4wO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2Jq" +
                 "ZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIi0wIik" +
                 "7dGV4dF9tYXRjaD0oZmlyc3Q9PSItMCIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KG" +
                 "E9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_float_one",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0xLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0O3NlY29uZD0oc3RyaW5nKShvYmpl" +
                 "Y3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiMSIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0iMSIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PW" +
                 "IpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_float_positive_infinity",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7aW5wdXQ9LWxsTG9nKDAuMCk7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0O3Nl" +
                 "Y29uZD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0" +
                 "oYT09KG9iamVjdCkiSW5maW5pdHkiKTt0ZXh0X21hdGNoPShmaXJzdD09IkluZmluaXR5Iik7YWdhaW49KHN0cmluZykob2JqZW" +
                 "N0KWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("object_float_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0wLjA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0O3NlY29uZD0oc3RyaW5nKShvYmpl" +
                 "Y3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiMCIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0iMCIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PW" +
                 "IpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_integer_-1",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PS0xO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2Jq" +
                 "ZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIi0xIik" +
                 "7dGV4dF9tYXRjaD0oZmlyc3Q9PSItMSIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KG" +
                 "E9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_integer_0",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0O3NlY29uZD0oc3RyaW5nKShvYmpl" +
                 "Y3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiMCIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0iMCIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PW" +
                 "IpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_integer_1",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTE7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0O3NlY29uZD0oc3RyaW5nKShvYmpl" +
                 "Y3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiMSIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0iMSIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PW" +
                 "IpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_integer_299",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTI5OTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7c2Vjb25kPShzdHJpbmcpKG9i" +
                 "amVjdClpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIyOTk" +
                 "iKTt0ZXh0X21hdGNoPShmaXJzdD09IjI5OSIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaX" +
                 "I9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_integer_300",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTMwMDtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7c2Vjb25kPShzdHJpbmcpKG9i" +
                 "amVjdClpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIzMDA" +
                 "iKTt0ZXh0X21hdGNoPShmaXJzdD09IjMwMCIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaX" +
                 "I9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("object_integer_42",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTQyO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2Jq" +
                 "ZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjQyIik" +
                 "7dGV4dF9tYXRjaD0oZmlyc3Q9PSI0MiIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KG" +
                 "E9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("sleep_float_nan",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dDtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDtzdHJpbmc" +
                 "gYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbWF0Y2g7aW" +
                 "50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyIGFmdGVyO" +
                 "2RlZmF1bHR7c3RhdGVfZW50cnkoKXtpbnB1dD1sbFNxcnQoLTEuMCk7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmlu" +
                 "ZylpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSJOYU4iKTt" +
                 "0ZXh0X21hdGNoPShmaXJzdD09Ik5hTiIpO2xsU2xlZXAoMC4xKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcG" +
                 "Fpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("sleep_integer_-1",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PS0xO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiLTEiKTt0ZXh0X21hdGNoPShmaXJ" +
                 "zdD09Ii0xIik7bGxTbGVlcCgwLjEpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcm" +
                 "F0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("sleep_integer_0",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTA7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1dDthPWZpcnN0" +
                 "O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSIwIik7dGV4dF9tYXRjaD0oZmlyc3Q" +
                 "9PSIwIik7bGxTbGVlcCgwLjEpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZW" +
                 "RfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("sleep_integer_299",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTI5OTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3RyaW5nKWlucHV0O2E9Zmly" +
                 "c3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjI5OSIpO3RleHRfbWF0Y2g9KGZ" +
                 "pcnN0PT0iMjk5Iik7bGxTbGVlcCgwLjEpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW" +
                 "5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("sleep_integer_300",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTMwMDtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDt" +
                 "zdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbW" +
                 "F0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyI" +
                 "GFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3RyaW5nKWlucHV0O2E9Zmly" +
                 "c3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjMwMCIpO3RleHRfbWF0Y2g9KGZ" +
                 "pcnN0PT0iMzAwIik7bGxTbGVlcCgwLjEpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW" +
                 "5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("sleep_integer_42",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTQyO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJz" +
                 "dDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiNDIiKTt0ZXh0X21hdGNoPShmaXJ" +
                 "zdD09IjQyIik7bGxTbGVlcCgwLjEpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcm" +
                 "F0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("sleep_typename",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTc7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9eG1yVHlwZU5hbWUoaW5wdXQpO3NlY29uZD14bXJUeXBlTmFtZShpbnB1" +
                 "dCk7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiaW50ZWdlciIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0iaW50ZWdlciIpO2xsU2xlZXAoMC4xKTthZ2Fpbj14bXJUeXBlTmFtZShpbnB1dCk7Yz1hZ2Fpbj" +
                 "thZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("typename_bool",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTE7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9eG1yVHlwZU5hbWUoKG9iamVjdCkoaW5wdXQ+" +
                 "MCkpO3NlY29uZD14bXJUeXBlTmFtZSgob2JqZWN0KShpbnB1dD4wKSk7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik" +
                 "7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiYm9vbCIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iYm9vbCIpO2FnYWluPXhtclR5cG" +
                 "VOYW1lKChvYmplY3QpKGlucHV0PjApKTtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7Y" +
                 "WZ0ZXI9MTt9fQ==")),
             true),
            ("typename_char",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztjaGFyIGlucHV0PSdBJztzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDtzdHJ" +
                 "pbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbWF0Y2" +
                 "g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyIGFmd" +
                 "GVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD14bXJUeXBlTmFtZShpbnB1dCk7c2Vjb25kPXhtclR5cGVOYW1lKGlucHV0" +
                 "KTthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSJjaGFyIik7dGV4dF9" +
                 "tYXRjaD0oZmlyc3Q9PSJjaGFyIik7YWdhaW49eG1yVHlwZU5hbWUoaW5wdXQpO2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2" +
                 "VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("typename_float",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztmbG9hdCBpbnB1dD0xLjI1O3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PXhtclR5cGVOYW1lKGlucHV0KTtzZWNvbmQ9eG1yVHlwZU5hbWUoaW5w" +
                 "dXQpO2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpImZsb2F0Iik7dGV" +
                 "4dF9tYXRjaD0oZmlyc3Q9PSJmbG9hdCIpO2FnYWluPXhtclR5cGVOYW1lKGlucHV0KTtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PW" +
                 "IpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("typename_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztpbnRlZ2VyIGlucHV0PTc7c3RyaW5nIGZpcnN0O3N0cmluZyBzZWNvbmQ7c3R" +
                 "yaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaXRlcmFsX21hdG" +
                 "NoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7aW50ZWdlciBhZ" +
                 "nRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9eG1yVHlwZU5hbWUoaW5wdXQpO3NlY29uZD14bXJUeXBlTmFtZShpbnB1" +
                 "dCk7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiaW50ZWdlciIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0iaW50ZWdlciIpO2FnYWluPXhtclR5cGVOYW1lKGlucHV0KTtjPWFnYWluO2FmdGVyX3BhaXI9KG" +
                 "E9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("typename_key",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztrZXkgaW5wdXQ9IngiO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cml" +
                 "uZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaD" +
                 "tpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0Z" +
                 "XI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PXhtclR5cGVOYW1lKGlucHV0KTtzZWNvbmQ9eG1yVHlwZU5hbWUoaW5wdXQp" +
                 "O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpInN0cmluZyIpO3RleHR" +
                 "fbWF0Y2g9KGZpcnN0PT0ic3RyaW5nIik7YWdhaW49eG1yVHlwZU5hbWUoaW5wdXQpO2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yi" +
                 "k7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("typename_list",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVtdO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cml" +
                 "uZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaD" +
                 "tpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0Z" +
                 "XI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PXhtclR5cGVOYW1lKGlucHV0KTtzZWNvbmQ9eG1yVHlwZU5hbWUoaW5wdXQp" +
                 "O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpImxpc3QiKTt0ZXh0X21" +
                 "hdGNoPShmaXJzdD09Imxpc3QiKTthZ2Fpbj14bXJUeXBlTmFtZShpbnB1dCk7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW" +
                 "5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("typename_rotation",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztyb3RhdGlvbiBpbnB1dD1aRVJPX1JPVEFUSU9OO3N0cmluZyBmaXJzdDtzdHJ" +
                 "pbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZX" +
                 "IgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hd" +
                 "GNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PXhtclR5cGVOYW1lKGlucHV0KTtzZWNvbmQ9eG1y" +
                 "VHlwZU5hbWUoaW5wdXQpO2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3Q" +
                 "pInJvdGF0aW9uIik7dGV4dF9tYXRjaD0oZmlyc3Q9PSJyb3RhdGlvbiIpO2FnYWluPXhtclR5cGVOYW1lKGlucHV0KTtjPWFnYW" +
                 "luO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("typename_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztzdHJpbmcgaW5wdXQ9IngiO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N" +
                 "0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYX" +
                 "RjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgY" +
                 "WZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PXhtclR5cGVOYW1lKGlucHV0KTtzZWNvbmQ9eG1yVHlwZU5hbWUoaW5w" +
                 "dXQpO2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpInN0cmluZyIpO3R" +
                 "leHRfbWF0Y2g9KGZpcnN0PT0ic3RyaW5nIik7YWdhaW49eG1yVHlwZU5hbWUoaW5wdXQpO2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT" +
                 "09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("typename_vector",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFyczt2ZWN0b3IgaW5wdXQ9WkVST19WRUNUT1I7c3RyaW5nIGZpcnN0O3N0cmluZyB" +
                 "zZWNvbmQ7c3RyaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaX" +
                 "RlcmFsX21hdGNoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7a" +
                 "W50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9eG1yVHlwZU5hbWUoaW5wdXQpO3NlY29uZD14bXJUeXBl" +
                 "TmFtZShpbnB1dCk7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkidmV" +
                 "jdG9yIik7dGV4dF9tYXRjaD0oZmlyc3Q9PSJ2ZWN0b3IiKTthZ2Fpbj14bXJUeXBlTmFtZShpbnB1dCk7Yz1hZ2FpbjthZnRlcl" +
                 "9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("vector_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFyczt2ZWN0b3IgaW5wdXQ9WkVST19WRUNUT1I7c3RyaW5nIGZpcnN0O3N0cmluZyB" +
                 "zZWNvbmQ7c3RyaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaX" +
                 "RlcmFsX21hdGNoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7a" +
                 "W50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9KHN0cmluZylpbnB1" +
                 "dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSI8MC4wMDAwMDAsIDA" +
                 "uMDAwMDAwLCAwLjAwMDAwMD4iKTt0ZXh0X21hdGNoPShmaXJzdD09IjwwLjAwMDAwMCwgMC4wMDAwMDAsIDAuMDAwMDAwPiIpO2" +
                 "FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyP" +
                 "TE7fX0=")),
             true),
            ("object_vector_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFyczt2ZWN0b3IgaW5wdXQ9WkVST19WRUNUT1I7c3RyaW5nIGZpcnN0O3N0cmluZyB" +
                 "zZWNvbmQ7c3RyaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZWQ7aW50ZWdlciBsaX" +
                 "RlcmFsX21hdGNoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lcmF0ZWRfbWF0Y2g7a" +
                 "W50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0O3NlY29uZD0oc3Ry" +
                 "aW5nKShvYmplY3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamV" +
                 "jdCkiPDAuMDAwMDAwLCAwLjAwMDAwMCwgMC4wMDAwMDA+" +
                 "Iik7dGV4dF9tYXRjaD0oZmlyc3Q9PSI8MC4wMDAwMDAsIDAuMDAwMDAwLCAwLjAwMDAwMD4iKTthZ2Fpbj0oc3RyaW5nKShvYmp" +
                 "lY3QpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("vector_finite",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFyczt2ZWN0b3IgaW5wdXQ9PDEuMjUsLTIuNSwzLjc1PjtzdHJpbmcgZmlyc3Q7c3R" +
                 "yaW5nIHNlY29uZDtzdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2" +
                 "VyIGxpdGVyYWxfbWF0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tY" +
                 "XRjaDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3RyaW5n" +
                 "KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjwxLjI1MDA" +
                 "wMCwgLTIuNTAwMDAwLCAzLjc1MDAwMD4iKTt0ZXh0X21hdGNoPShmaXJzdD09IjwxLjI1MDAwMCwgLTIuNTAwMDAwLCAzLjc1MD" +
                 "AwMD4iKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jK" +
                 "TthZnRlcj0xO319")),
             true),
            ("object_vector_finite",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFyczt2ZWN0b3IgaW5wdXQ9PDEuMjUsLTIuNSwzLjc1PjtzdHJpbmcgZmlyc3Q7c3R" +
                 "yaW5nIHNlY29uZDtzdHJpbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2" +
                 "VyIGxpdGVyYWxfbWF0Y2g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tY" +
                 "XRjaDtpbnRlZ2VyIGFmdGVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7c2Vjb25k" +
                 "PShzdHJpbmcpKG9iamVjdClpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0" +
                 "ob2JqZWN0KSI8MS4yNTAwMDAsIC0yLjUwMDAwMCwgMy43NTAwMDA+" +
                 "Iik7dGV4dF9tYXRjaD0oZmlyc3Q9PSI8MS4yNTAwMDAsIC0yLjUwMDAwMCwgMy43NTAwMDA+" +
                 "Iik7YWdhaW49KHN0cmluZykob2JqZWN0KWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPSh" +
                 "hPT1jKTthZnRlcj0xO319")),
             true),
            ("rotation_identity",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztyb3RhdGlvbiBpbnB1dD1aRVJPX1JPVEFUSU9OO3N0cmluZyBmaXJzdDtzdHJ" +
                 "pbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZX" +
                 "IgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hd" +
                 "GNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcp" +
                 "aW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiPDAuMDAwMDA" +
                 "wLCAwLjAwMDAwMCwgMC4wMDAwMDAsIDEuMDAwMDAwPiIpO3RleHRfbWF0Y2g9KGZpcnN0PT0iPDAuMDAwMDAwLCAwLjAwMDAwMC" +
                 "wgMC4wMDAwMDAsIDEuMDAwMDAwPiIpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lc" +
                 "mF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_rotation_identity",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztyb3RhdGlvbiBpbnB1dD1aRVJPX1JPVEFUSU9OO3N0cmluZyBmaXJzdDtzdHJ" +
                 "pbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZX" +
                 "IgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hd" +
                 "GNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9" +
                 "KHN0cmluZykob2JqZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PSh" +
                 "vYmplY3QpIjwwLjAwMDAwMCwgMC4wMDAwMDAsIDAuMDAwMDAwLCAxLjAwMDAwMD4iKTt0ZXh0X21hdGNoPShmaXJzdD09IjwwLj" +
                 "AwMDAwMCwgMC4wMDAwMDAsIDAuMDAwMDAwLCAxLjAwMDAwMD4iKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpaW5wdXQ7Yz1hZ2Fpb" +
                 "jthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("rotation_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztyb3RhdGlvbiBpbnB1dD0ocm90YXRpb24pIjwwLDAsMCwwPiI7c3RyaW5nIGZ" +
                 "pcnN0O3N0cmluZyBzZWNvbmQ7c3RyaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZW" +
                 "Q7aW50ZWdlciBsaXRlcmFsX21hdGNoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lc" +
                 "mF0ZWRfbWF0Y2g7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZylpbnB1dDtzZWNvbmQ9" +
                 "KHN0cmluZylpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSI" +
                 "8MC4wMDAwMDAsIDAuMDAwMDAwLCAwLjAwMDAwMCwgMC4wMDAwMDA+" +
                 "Iik7dGV4dF9tYXRjaD0oZmlyc3Q9PSI8MC4wMDAwMDAsIDAuMDAwMDAwLCAwLjAwMDAwMCwgMC4wMDAwMDA+" +
                 "Iik7YWdhaW49KHN0cmluZylpbnB1dDtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ" +
                 "0ZXI9MTt9fQ==")),
             true),
            ("object_rotation_zero",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztyb3RhdGlvbiBpbnB1dD0ocm90YXRpb24pIjwwLDAsMCwwPiI7c3RyaW5nIGZ" +
                 "pcnN0O3N0cmluZyBzZWNvbmQ7c3RyaW5nIGFnYWluO29iamVjdCBhO29iamVjdCBiO29iamVjdCBjO2ludGVnZXIgcmVwZWF0ZW" +
                 "Q7aW50ZWdlciBsaXRlcmFsX21hdGNoO2ludGVnZXIgdGV4dF9tYXRjaDtpbnRlZ2VyIGFmdGVyX3BhaXI7aW50ZWdlciBnZW5lc" +
                 "mF0ZWRfbWF0Y2g7aW50ZWdlciBhZnRlcjtkZWZhdWx0e3N0YXRlX2VudHJ5KCl7Zmlyc3Q9KHN0cmluZykob2JqZWN0KWlucHV0" +
                 "O3NlY29uZD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXR" +
                 "jaD0oYT09KG9iamVjdCkiPDAuMDAwMDAwLCAwLjAwMDAwMCwgMC4wMDAwMDAsIDAuMDAwMDAwPiIpO3RleHRfbWF0Y2g9KGZpcn" +
                 "N0PT0iPDAuMDAwMDAwLCAwLjAwMDAwMCwgMC4wMDAwMDAsIDAuMDAwMDAwPiIpO2FnYWluPShzdHJpbmcpKG9iamVjdClpbnB1d" +
                 "DtjPWFnYWluO2FmdGVyX3BhaXI9KGE9PWIpO2dlbmVyYXRlZF9tYXRjaD0oYT09Yyk7YWZ0ZXI9MTt9fQ==")),
             true),
            ("list_empty",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVtdO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cml" +
                 "uZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaD" +
                 "tpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0Z" +
                 "XI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJzdDti" +
                 "PXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkiIik7dGV4dF9tYXRjaD0oZmlyc3Q9PSI" +
                 "iKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZn" +
                 "Rlcj0xO319")),
             true),
            ("object_list_empty",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVtdO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cml" +
                 "uZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaD" +
                 "tpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0Z" +
                 "XI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2JqZWN0" +
                 "KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIiIpO3RleHR" +
                 "fbWF0Y2g9KGZpcnN0PT0iIik7YWdhaW49KHN0cmluZykob2JqZWN0KWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2" +
                 "VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("list_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVsic2FtZSJdO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1m" +
                 "aXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkic2FtZSIpO3RleHRfbWF0Y2g" +
                 "9KGZpcnN0PT0ic2FtZSIpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbW" +
                 "F0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_list_string",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVsic2FtZSJdO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZyko" +
                 "b2JqZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpInN" +
                 "hbWUiKTt0ZXh0X21hdGNoPShmaXJzdD09InNhbWUiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpaW5wdXQ7Yz1hZ2FpbjthZnRlcl" +
                 "9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("list_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVs3XTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDtzdHJ" +
                 "pbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbWF0Y2" +
                 "g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyIGFmd" +
                 "GVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKWlucHV0O3NlY29uZD0oc3RyaW5nKWlucHV0O2E9Zmlyc3Q7" +
                 "Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpIjciKTt0ZXh0X21hdGNoPShmaXJzdD0" +
                 "9IjciKTthZ2Fpbj0oc3RyaW5nKWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yik7Z2VuZXJhdGVkX21hdGNoPShhPT1jKT" +
                 "thZnRlcj0xO319")),
             true),
            ("object_list_integer",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztsaXN0IGlucHV0PVs3XTtzdHJpbmcgZmlyc3Q7c3RyaW5nIHNlY29uZDtzdHJ" +
                 "pbmcgYWdhaW47b2JqZWN0IGE7b2JqZWN0IGI7b2JqZWN0IGM7aW50ZWdlciByZXBlYXRlZDtpbnRlZ2VyIGxpdGVyYWxfbWF0Y2" +
                 "g7aW50ZWdlciB0ZXh0X21hdGNoO2ludGVnZXIgYWZ0ZXJfcGFpcjtpbnRlZ2VyIGdlbmVyYXRlZF9tYXRjaDtpbnRlZ2VyIGFmd" +
                 "GVyO2RlZmF1bHR7c3RhdGVfZW50cnkoKXtmaXJzdD0oc3RyaW5nKShvYmplY3QpaW5wdXQ7c2Vjb25kPShzdHJpbmcpKG9iamVj" +
                 "dClpbnB1dDthPWZpcnN0O2I9c2Vjb25kO3JlcGVhdGVkPShhPT1iKTtsaXRlcmFsX21hdGNoPShhPT0ob2JqZWN0KSI3Iik7dGV" +
                 "4dF9tYXRjaD0oZmlyc3Q9PSI3Iik7YWdhaW49KHN0cmluZykob2JqZWN0KWlucHV0O2M9YWdhaW47YWZ0ZXJfcGFpcj0oYT09Yi" +
                 "k7Z2VuZXJhdGVkX21hdGNoPShhPT1jKTthZnRlcj0xO319")),
             true),
            ("string_literal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztzdHJpbmcgaW5wdXQ9InNhbWUiO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1m" +
                 "aXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkic2FtZSIpO3RleHRfbWF0Y2g" +
                 "9KGZpcnN0PT0ic2FtZSIpO2FnYWluPShzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbW" +
                 "F0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_string_literal",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztzdHJpbmcgaW5wdXQ9InNhbWUiO3N0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25" +
                 "kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF" +
                 "9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZ" +
                 "XIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZyko" +
                 "b2JqZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpInN" +
                 "hbWUiKTt0ZXh0X21hdGNoPShmaXJzdD09InNhbWUiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpaW5wdXQ7Yz1hZ2FpbjthZnRlcl" +
                 "9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("string_dynamic",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztzdHJpbmcgaW5wdXQ9bWFrZSgpO3N0cmluZyBtYWtlKCl7cmV0dXJuIGxsR2V" +
                 "0U3ViU3RyaW5nKCJ4c2FtZSIsMSwtMSk7fXN0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYT" +
                 "tvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y" +
                 "2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9l" +
                 "bnRyeSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0" +
                 "oYT09Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkic2FtZSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0ic2FtZSIpO2FnYWluPS" +
                 "hzdHJpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_string_dynamic",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztzdHJpbmcgaW5wdXQ9bWFrZSgpO3N0cmluZyBtYWtlKCl7cmV0dXJuIGxsR2V" +
                 "0U3ViU3RyaW5nKCJ4c2FtZSIsMSwtMSk7fXN0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYT" +
                 "tvYmplY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y" +
                 "2g7aW50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9l" +
                 "bnRyeSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2JqZWN0KWlucHV0O2E9Zmlyc3Q7Yj1" +
                 "zZWNvbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpInNhbWUiKTt0ZXh0X21hdGNoPShmaXJzdD" +
                 "09InNhbWUiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfb" +
                 "WF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("key_dynamic",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztrZXkgaW5wdXQ9bWFrZSgpO3N0cmluZyBtYWtlKCl7cmV0dXJuIGxsR2V0U3V" +
                 "iU3RyaW5nKCJ4c2FtZSIsMSwtMSk7fXN0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYm" +
                 "plY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7a" +
                 "W50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRy" +
                 "eSgpe2ZpcnN0PShzdHJpbmcpaW5wdXQ7c2Vjb25kPShzdHJpbmcpaW5wdXQ7YT1maXJzdDtiPXNlY29uZDtyZXBlYXRlZD0oYT0" +
                 "9Yik7bGl0ZXJhbF9tYXRjaD0oYT09KG9iamVjdCkic2FtZSIpO3RleHRfbWF0Y2g9KGZpcnN0PT0ic2FtZSIpO2FnYWluPShzdH" +
                 "JpbmcpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
             true),
            ("object_key_dynamic",
             System.Text.Encoding.UTF8.GetString(Convert.FromBase64String(
                 "WU9wdGlvbiBhcnJheXM7WU9wdGlvbiBjaGFycztrZXkgaW5wdXQ9bWFrZSgpO3N0cmluZyBtYWtlKCl7cmV0dXJuIGxsR2V0U3V" +
                 "iU3RyaW5nKCJ4c2FtZSIsMSwtMSk7fXN0cmluZyBmaXJzdDtzdHJpbmcgc2Vjb25kO3N0cmluZyBhZ2FpbjtvYmplY3QgYTtvYm" +
                 "plY3QgYjtvYmplY3QgYztpbnRlZ2VyIHJlcGVhdGVkO2ludGVnZXIgbGl0ZXJhbF9tYXRjaDtpbnRlZ2VyIHRleHRfbWF0Y2g7a" +
                 "W50ZWdlciBhZnRlcl9wYWlyO2ludGVnZXIgZ2VuZXJhdGVkX21hdGNoO2ludGVnZXIgYWZ0ZXI7ZGVmYXVsdHtzdGF0ZV9lbnRy" +
                 "eSgpe2ZpcnN0PShzdHJpbmcpKG9iamVjdClpbnB1dDtzZWNvbmQ9KHN0cmluZykob2JqZWN0KWlucHV0O2E9Zmlyc3Q7Yj1zZWN" +
                 "vbmQ7cmVwZWF0ZWQ9KGE9PWIpO2xpdGVyYWxfbWF0Y2g9KGE9PShvYmplY3QpInNhbWUiKTt0ZXh0X21hdGNoPShmaXJzdD09In" +
                 "NhbWUiKTthZ2Fpbj0oc3RyaW5nKShvYmplY3QpaW5wdXQ7Yz1hZ2FpbjthZnRlcl9wYWlyPShhPT1iKTtnZW5lcmF0ZWRfbWF0Y" +
                 "2g9KGE9PWMpO2FmdGVyPTE7fX0=")),
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
            object AllReferencePairs(XMRInstance i) {
                var objects = new List<(string path, object value)>();
                void Walk(string path, object value) {
                    objects.Add((path, value));
                    if (value is LSL.list list)
                        for (int n = 0; n < list.Data.Length; n++)
                            Walk(path + "/" + n, list.Data[n]);
                }
                foreach (var names in code.globalVarNames) {
                    var array = (Array) typeof(XMRInstArrays).GetField(names.Key).GetValue(i.glblVars);
                    var element = array.GetType().GetElementType();
                    if (element != typeof(object) && element != typeof(string) && element != typeof(LSL.list))
                        continue;
                    foreach (var entry in names.Value)
                        Walk("global/" + entry.Value, array.GetValue(entry.Key));
                }
                int eventIndex = 0;
                foreach (var e in i.m_EventQueue) {
                    for (int n = 0; n < e.Params.Length; n++)
                        Walk("queue" + eventIndex + "/" + n, e.Params[n]);
                    eventIndex++;
                }
                bool StringField(object value, out string text) {
                    if (value is string raw) {
                        text = raw;
                        return true;
                    }
                    if (value is LSL.LSLString boxed) {
                        text = boxed.m_string;
                        return true;
                    }
                    text = null;
                    return false;
                }
                var pairs = new List<object>();
                for (int a = 0; a < objects.Count; a++)
                    for (int c = a + 1; c < objects.Count; c++) {
                        var left = objects[a];
                        var right = objects[c];
                        bool lp = StringField(left.value, out string ls), rp = StringField(right.value, out string rs);
                        pairs.Add(new { left = left.path, right = right.path, left_clr = left.value?.GetType().FullName,
                                        right_clr = right.value?.GetType().FullName,
                                        same_object_reference = object.ReferenceEquals(left.value, right.value),
                                        left_has_string_field = lp, right_has_string_field = rp,
                                        same_string_field_reference =
                                            lp && rp ? (bool?)object.ReferenceEquals(ls, rs) : null });
                    }
                return pairs;
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
                             all_reference_pairs = AllReferencePairs(i),
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
            hashes["generator_assembly"] = Sha(typeof(ObjectStringConversionReference).Assembly.Location);
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
                conversion_call_lines = File.ReadAllLines(Path.Combine(folder, "original.il.txt"))
                                            .Where(l => l.Contains("ToString") || l.Contains("xmrTypeName"))
                                            .ToArray(),
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
                    "Actual original source primitive cast versus Object.ToString, xmrTypeName and CLR String " +
                    "reference identity. Runtime variable inputs prevent constant folding; literal/text equality " +
                    "flags distinguish interned literals from Number small-number caches, fresh formatting and " +
                    "special symbols. RealSleep captures preserve separate LIVE versus production binary migration " +
                    "semantics. Lone-char literal comparison uses UFFFD portable literal while runtime UTF16 retains " +
                    "the lone unit; it is not a same-text literal case. No content-based identity inference.",
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
