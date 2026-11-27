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

    sealed class GraphStats
    {
        public long listOccurrences { get; set; }
        public long uniqueLists { get; set; }
        public long leaves { get; set; }
        public bool allLeavesOne { get; set; }
        public string cellsSha256 { get; set; }
        public bool rootChildrenAlias { get; set; }
    }

    static GraphStats Describe(object value)
    {
        var seen = new HashSet<object>(ReferenceEqualityComparer.Instance);
        var pending = new Stack<object>();
        pending.Push(value);
        long lists = 0, leaves = 0;
        using var stream = new MemoryStream();
        using var writer = new BinaryWriter(stream, Encoding.UTF8, true);
        while (pending.Count != 0)
        {
            object current = pending.Pop();
            if (current is LSL.list list)
            {
                lists++;
                seen.Add(list);
                Require(list.Length == 2, "DAG list arity changed");
                pending.Push(list.Data[1]);
                pending.Push(list.Data[0]);
            }
            else
            {
                Require(current is int && (int)current == 1, "DAG leaf type/value changed");
                leaves++;
                writer.Write((int)current);
            }
        }
        writer.Flush();
        var root = (LSL.list)value;
        return new GraphStats { listOccurrences = lists, uniqueLists = seen.Count,
            leaves = leaves, allLeavesOne = true, cellsSha256 = Hash(stream.ToArray()),
            rootChildrenAlias = object.ReferenceEquals(root.Data[0], root.Data[1]) };
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
        string name = "shared_list_dag20";
        string folder = Path.Combine(output, name);
        Directory.CreateDirectory(folder);
        object graph = 1;
        for (int depth = 0; depth < 20; ++depth)
            graph = new LSL.list(new object[] { graph, graph });
        var instance = Instance();
        bool accepted = instance.PostEvent(new EventParams("touch_start", new object[] { graph },
            Array.Empty<DetectParams>()));
        Require(accepted && instance.m_EventQueue.Count == 1, "Original PostEvent rejected DAG20");
        var before = Describe(instance.m_EventQueue.First.Value.Params[0]);
        Console.WriteLine("Admitted DAG20; writing production XML");
        string xml = SaveQueue(instance);
        File.WriteAllText(Path.Combine(folder, "event-queue.xml"), xml, new UTF8Encoding(false));
        Console.WriteLine("Wrote " + Encoding.UTF8.GetByteCount(xml) + " XML bytes; restoring");
        var restored = LoadQueue(Instance(), xml);
        Require(restored.Count == 1, "Queue restore lost DAG20 event");
        var after = Describe(restored.First.Value.Params[0]);
        Require(before.listOccurrences == 1048575 && before.uniqueLists == 20 && before.leaves == 1048576,
            "Input DAG20 shape changed");
        Require(after.listOccurrences == 1048575 && after.uniqueLists == 1048575 && after.leaves == 1048576,
            "Restored XML tree shape changed");
        Require(before.rootChildrenAlias && !after.rootChildrenAlias && before.cellsSha256 == after.cellsSha256,
            "Original XML expansion did not split aliases and preserve every leaf");
        var record = new {
            name, postAccepted = accepted, queuedCount = instance.m_EventQueue.Count, restoredCount = restored.Count,
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
            scope = "Actual unchanged original PostEvent and production XML helpers; backend queue argument only, no source/event dispatch claim"
        };
        File.WriteAllText(Path.Combine(folder, "metadata.json"),
            JsonSerializer.Serialize(record, new JsonSerializerOptions { WriteIndented = true }));
        rows.Add(record);
        File.WriteAllText(Path.Combine(output, "metadata.json"),
            JsonSerializer.Serialize(rows, new JsonSerializerOptions { WriteIndented = true }));
        Console.WriteLine("PASS original DAG20 XML restores every leaf and independently allocates every List occurrence");
    }
}
