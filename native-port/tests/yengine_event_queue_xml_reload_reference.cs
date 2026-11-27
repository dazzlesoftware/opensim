// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Text.Json;
using System.Xml;
using OpenSim.Region.ScriptEngine.Yengine;
using OpenSim.Region.ScriptEngine.Shared;
using LSL = OpenSim.Region.ScriptEngine.Shared.LSL_Types;

public static class EventQueueXmlReloadReference {
    static readonly BindingFlags priv = BindingFlags.Instance | BindingFlags.NonPublic;
    static readonly BindingFlags stat = BindingFlags.Static | BindingFlags.NonPublic;
    static string Sha(string p) =>
        Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(p))).ToLowerInvariant();

    static object Error(Exception e) {
        while (e is TargetInvocationException && e.InnerException != null)
            e = e.InnerException;
        return new { type = e.GetType().FullName, message = e.Message, stack = e.StackTrace };
    }

    static object Typed(object o) {
        if (o == null)
            return new { clr = (string)null, value = (object)null };
        string clr = o.GetType().FullName;
        if (o is LSL.LSLString ls)
            return new { clr, value = ls.m_string,
                         utf16 =
                             ls.m_string == null ? null : Array.ConvertAll(ls.m_string.ToCharArray(), c => (int)c) };
        if (o is string s)
            return new { clr, value = s, utf16 = Array.ConvertAll(s.ToCharArray(), c => (int)c) };
        if (o is float f)
            return new { clr, value = float.IsFinite(f) ? (object)f : null,
                         bits = BitConverter.SingleToUInt32Bits(f).ToString("x8") };
        if (o is double d)
            return new { clr, value = double.IsFinite(d) ? (object)d : null,
                         bits = BitConverter.DoubleToUInt64Bits(d).ToString("x16") };
        return new { clr, value = o.ToString() };
    }

    static void Main(string[] args) {
        string bin = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, r) => {
            string p = Path.Combine(bin, new AssemblyName(r.Name).Name + ".dll");
            return File.Exists(p) ? Assembly.LoadFrom(p) : null;
        };
        Run(Path.GetFullPath(args[1]), Path.GetFullPath(args[2]), Path.GetFullPath(args[3]));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run(string output, string generator, string sourceRoot) {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        Directory.CreateDirectory(output);
        var samples = new List<(string, object)>();
        foreach (var t in new(string, string)[] { ("nul", "a" + '\0' + "b"), ("pair", char.ConvertFromUtf32(0x1f600)),
                                                  ("high", new string((char)0xd800, 1)),
                                                  ("low", new string((char)0xdc00, 1)), ("lf", "a\nb"), ("cr", "a\rb"),
                                                  ("tab", "a\tb"), ("crlf", "a\r\nb") }) {
            samples.Add(("raw_" + t.Item1, t.Item2));
            samples.Add(("boxed_" + t.Item1, new LSL.LSLString(t.Item2)));
        }
        samples.Add(("raw_single", 1.234567891f));
        samples.Add(("raw_bool", true));
        samples.Add(("raw_uuid", new OpenMetaverse.UUID("11111111-2222-3333-4444-555555555555")));
        samples.Add(("raw_null", null));
        samples.Add(("boxed_null_string", new LSL.LSLString(null)));
        var inst = (XMRInstance)RuntimeHelpers.GetUninitializedObject(typeof(XMRInstance));
        GC.SuppressFinalize(inst);
        var rows = new List<object>();
        foreach (var spec in samples) {
            string folder = Path.Combine(output, spec.Item1);
            Directory.CreateDirectory(folder);
            var doc = new XmlDocument();
            var root = doc.CreateElement("EventQueue");
            doc.AppendChild(root);
            var ev = doc.CreateElement("Event");
            ev.SetAttribute("Name", "touch_start");
            root.AppendChild(ev);
            object appendError = null, outerError = null, loadError = null, restoreError = null, readerError = null,
                   restored = null, readerRestored = null;
            string xml = null;
            try {
                typeof(XMRInstance)
                    .GetMethod("AppendXMLObjectArray", stat)
                    .Invoke(null, new object[] { doc, ev, new object[] { spec.Item2 }, "param" });
            } catch (Exception e) {
                appendError = Error(e);
            }
            if (appendError == null)
                try {
                    xml = doc.OuterXml;
                    File.WriteAllText(Path.Combine(folder, "serialized.xml"), xml);
                } catch (Exception e) {
                    outerError = Error(e);
                }
            if (xml != null) {
                var fresh = new XmlDocument();
                try {
                    fresh.LoadXml(xml);
                } catch (Exception e) {
                    loadError = Error(e);
                }
                if (loadError == null)
                    try {
                        var q = (LinkedList<EventParams>)typeof(XMRInstance)
                                    .GetMethod("RestoreEventQueue", priv)
                                    .Invoke(inst, new object[] { fresh.DocumentElement });
                        restored = Typed(q.First.Value.Params[0]);
                    } catch (Exception e) {
                        restoreError = Error(e);
                    }
                // Separate strict XmlReader control, NOT the original LoadScriptState route.
                try {
                    var other = new XmlDocument();
                    using var reader = XmlReader.Create(new StringReader(xml));
                    other.Load(reader);
                    var q = (LinkedList<EventParams>)typeof(XMRInstance)
                                .GetMethod("RestoreEventQueue", priv)
                                .Invoke(inst, new object[] { other.DocumentElement });
                    readerRestored = Typed(q.First.Value.Params[0]);
                } catch (Exception e) {
                    readerError = Error(e);
                }
            }
            var hashes = new Dictionary<string, string>();
            foreach (string f in Directory.GetFiles(folder))
                if (!f.EndsWith("metadata.json"))
                    hashes[Path.GetFileName(f)] = Sha(f);
            var row = new { name = spec.Item1,
                            input = Typed(spec.Item2),
                            append_exception = appendError,
                            outer_xml_exception = outerError,
                            load_xml_exception = loadError,
                            restore_event_queue_exception = restoreError,
                            restored,
                            strict_xml_reader_control_exception = readerError,
                            strict_xml_reader_control_restored = readerRestored,
                            sha256 = hashes };
            File.WriteAllText(Path.Combine(folder, "metadata.json"),
                              JsonSerializer.Serialize(row, new JsonSerializerOptions { WriteIndented = true }));
            rows.Add(row);
        }
        var provenance = new {
            originalRoute = "XMRInstCtor.cs382-384 new XmlDocument().LoadXml(xml), then LoadScriptState -> " +
                            "RestoreEventQueue; strict XmlReader is separate control",
            controlScope =
                "Actual private original serialization/restore of backend CLR inputs; no invented LSL source types",
            generatorSha256 = Sha(generator),
            assemblySha256 = Sha(typeof(EventQueueXmlReloadReference).Assembly.Location),
            originalAssemblySha256 = Sha(typeof(XMRInstance).Assembly.Location),
            runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            architecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            originalCaptureSha256 =
                Sha(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine/XMRInstCapture.cs")),
            originalCtorSha256 = Sha(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine/XMRInstCtor.cs")),
            cases = rows
        };
        File.WriteAllText(Path.Combine(output, "metadata.json"),
                          JsonSerializer.Serialize(provenance, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine("Completed " + rows.Count + " actual original reload controls");
    }
}
