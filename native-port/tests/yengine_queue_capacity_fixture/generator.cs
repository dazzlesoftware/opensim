// Queue capacity observations from unchanged original YEngine backend methods.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Xml;
using OpenSim.Region.ScriptEngine.Shared;
using OpenSim.Region.ScriptEngine.Yengine;
using LSL = OpenSim.Region.ScriptEngine.Shared.LSL_Types;

public static class QueueCapacityReference
{
    const BindingFlags PrivateInstance = BindingFlags.Instance | BindingFlags.NonPublic;
    const BindingFlags PrivateStatic = BindingFlags.Static | BindingFlags.NonPublic;

    static string Hash(byte[] bytes) => Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
    static string FileHash(string path) => Hash(File.ReadAllBytes(path));

    static XMRInstance Instance()
    {
        var instance = (XMRInstance)RuntimeHelpers.GetUninitializedObject(typeof(XMRInstance));
        GC.SuppressFinalize(instance);
        instance.m_IState = XMRInstState.RUNNING;
        instance.m_Running = true;
        instance.m_QueueLock = new object();
        instance.m_EventQueue = new LinkedList<EventParams>();
        typeof(XMRInstance).GetField("m_EventCounts", PrivateInstance).SetValue(instance,
            new int[(int)ScriptEventCode.Size]);
        var handlers = new bool[(int)ScriptEventCode.Size];
        Array.Fill(handlers, true);
        typeof(XMRInstance).GetField("m_HaveEventHandlers", PrivateInstance).SetValue(instance, handlers);
        return instance;
    }

    static string SaveQueue(XMRInstance instance)
    {
        var document = new XmlDocument();
        var root = document.CreateElement("EventQueue");
        document.AppendChild(root);
        foreach (var queued in instance.m_EventQueue)
        {
            var node = document.CreateElement("Event");
            node.SetAttribute("Name", queued.EventName);
            root.AppendChild(node);
            typeof(XMRInstance).GetMethod("AppendXMLObjectArray", PrivateStatic).Invoke(null,
                new object[] { document, node, queued.Params, "param" });
            typeof(XMRInstance).GetMethod("AppendXMLDetectArray", PrivateStatic).Invoke(null,
                new object[] { document, node, queued.DetectParams });
        }
        return document.OuterXml;
    }

    static LinkedList<EventParams> LoadQueue(XMRInstance instance, string xml)
    {
        var document = new XmlDocument();
        document.LoadXml(xml);
        return (LinkedList<EventParams>)typeof(XMRInstance).GetMethod("RestoreEventQueue", PrivateInstance)
            .Invoke(instance, new object[] { document.DocumentElement });
    }

    static object Describe(object value)
    {
        string clr = value.GetType().FullName;
        if (value is LSL.list list)
        {
            using var stream = new MemoryStream();
            using var writer = new BinaryWriter(stream, Encoding.UTF8, true);
            foreach (object item in list.Data)
            {
                int number = item is LSL.LSLInteger boxed ? boxed.value : (int)item;
                writer.Write(number);
            }
            writer.Flush();
            return new { kind = "list", clr, length = list.Length,
                cellsSha256 = Hash(stream.ToArray()), first = list.Data[0].ToString(),
                middle = list.Data[list.Length / 2].ToString(), last = list.Data[^1].ToString(),
                firstClr = list.Data[0].GetType().FullName, lastClr = list.Data[^1].GetType().FullName };
        }
        string text = value is LSL.LSLString sdk ? sdk.m_string : value as string;
        if (text != null)
            return new { kind = "string", clr, length = text.Length,
                cellsSha256 = Hash(Encoding.UTF8.GetBytes(text)), utf8Bytes = Encoding.UTF8.GetByteCount(text),
                first = (int)text[0], middle = (int)text[text.Length / 2], last = (int)text[^1] };
        return new { kind = "scalar", clr, text = value.ToString() };
    }

    static void Require(bool condition, string message)
    {
        if (!condition) throw new Exception(message);
    }

    static void Main(string[] args)
    {
        string bin = Path.GetFullPath(args[0]);
        AppDomain.CurrentDomain.AssemblyResolve += (_, request) =>
        {
            string path = Path.Combine(bin, new AssemblyName(request.Name).Name + ".dll");
            return File.Exists(path) ? Assembly.LoadFrom(path) : null;
        };
        Run(Path.GetFullPath(args[1]), Path.GetFullPath(args[2]), Path.GetFullPath(args[3]));
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static void Run(string output, string generator, string sourceRoot)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        Directory.CreateDirectory(output);
        var rows = new List<object>();
        foreach (string name in new[] { "list_65537", "list_100000", "raw_string_4194305", "sdk_string_4194305" })
        {
            string folder = Path.Combine(output, name);
            Directory.CreateDirectory(folder);
            object value;
            string kind;
            int length;
            if (name.StartsWith("list_"))
            {
                kind = "list";
                length = int.Parse(name.Substring(5), CultureInfo.InvariantCulture);
                object[] cells = new object[length];
                Array.Fill(cells, new LSL.LSLInteger(19));
                cells[^1] = new LSL.LSLInteger(-7);
                value = new LSL.list(cells);
            }
            else
            {
                kind = "string";
                length = 4194305;
                char[] cells = new string('x', length).ToCharArray();
                cells[0] = 'A'; cells[length / 2] = 'B'; cells[^1] = 'Z';
                string text = new string(cells);
                value = name.StartsWith("sdk_") ? new LSL.LSLString(text) : text;
            }
            // List is a backend PostEvent parameter capacity control. It is not
            // claimed to match any typed source event handler's argument list.
            string eventName = kind == "list" ? "touch_start" : "listen";
            object[] parameters = kind == "list" ? new object[] { value } :
                new object[] { new LSL.LSLInteger(0), new LSL.LSLString("queue"),
                    new LSL.key("11111111-2222-3333-4444-555555555555"), value };
            var instance = Instance();
            bool accepted = instance.PostEvent(new EventParams(eventName, parameters, Array.Empty<DetectParams>()));
            Require(accepted && instance.m_EventQueue.Count == 1, "Original PostEvent rejected " + name);
            var before = Describe(instance.m_EventQueue.First.Value.Params[^1]);
            string xml = SaveQueue(instance);
            File.WriteAllText(Path.Combine(folder, "event-queue.xml"), xml, new UTF8Encoding(false));
            var restored = LoadQueue(Instance(), xml);
            Require(restored.Count == 1 && restored.First.Value.EventName == eventName, "Queue restore lost event");
            Require(restored.First.Value.Params.Length == parameters.Length, "Queue restore lost arguments");
            var after = Describe(restored.First.Value.Params[^1]);
            Require(JsonSerializer.Serialize(before) == JsonSerializer.Serialize(after), "Queue restore changed payload");
            var record = new {
                name, kind, length, eventName, postAccepted = accepted, queuedCount = instance.m_EventQueue.Count,
                restoredCount = restored.Count, parameterCount = parameters.Length,
                before, after, xmlBytes = Encoding.UTF8.GetByteCount(xml),
                xmlSha256 = FileHash(Path.Combine(folder, "event-queue.xml")),
                generatorSha256 = FileHash(generator),
                originalAssemblySha256 = FileHash(typeof(XMRInstance).Assembly.Location),
                originalSharedAssemblySha256 = FileHash(typeof(LSL.list).Assembly.Location),
                originalSourceSha256 = new Dictionary<string, string> {
                    ["XMRInstRun.cs"] = FileHash(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine/XMRInstRun.cs")),
                    ["XMRInstCapture.cs"] = FileHash(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine/XMRInstCapture.cs")),
                    ["XMRInstCtor.cs"] = FileHash(Path.Combine(sourceRoot, "OpenSim/Region/ScriptEngine/YEngine/XMRInstCtor.cs")) },
                runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
                architecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
                processId = Environment.ProcessId,
                scope = "Actual unchanged original PostEvent plus production AppendXMLObjectArray/AppendXMLDetectArray and RestoreEventQueue; no source compilation or event dispatch claim"
            };
            File.WriteAllText(Path.Combine(folder, "metadata.json"),
                JsonSerializer.Serialize(record, new JsonSerializerOptions { WriteIndented = true }));
            rows.Add(record);
            Console.WriteLine(name + ": accepted=True written=True restored=True");
        }
        File.WriteAllText(Path.Combine(output, "metadata.json"),
            JsonSerializer.Serialize(rows, new JsonSerializerOptions { WriteIndented = true }));
    }
}
