using System;
using System.Collections.Generic;
using OpenMetaverse;
using OpenMetaverse.Packets;
enum ThrottleOutPacketType
{
    Task
}
interface IEventQueue
{
    void ScriptRunningEvent(UUID obj, UUID item, bool running, UUID agent);
}
class Trace
{
    public List<string> Values = new();
    public int Mode;
    public bool Stored = true;
    public void Add(string value) => Values.Add(value);
}
class SceneStub
{
    public IEventQueue Eq;
    public T RequestModuleInterface<T>()
        where T : class => Eq as T;
}
class EqStub : IEventQueue
{
    public Trace Trace;
    public void ScriptRunningEvent(UUID obj, UUID item, bool running, UUID agent)
    {
        Trace.Add("EQ:" + obj + "," + item + "," + (running ? "1" : "0") + "," + agent);
        if ((Trace.Mode & 32) != 0)
            throw new Exception();
    }
}
class InventoryItem
{
    public Trace Trace;
    public bool ScriptRunning
    {
        set {
            Trace.Add(value ? "S1" : "S0");
            if ((Trace.Mode & 16) != 0)
                throw new Exception();
            Trace.Stored = value;
        }
    }
}
class XMRInstance
{
    public Trace Trace;
    public InventoryItem m_Item;
    public bool Running
    {
        get {
            Trace.Add("G");
            if ((Trace.Mode & 8) != 0)
                throw new Exception();
            return (Trace.Mode & 1) != 0;
        }
    }
}
partial class Client
{
    public SceneStub Scene = new();
    public UUID m_agentId;
    public Trace Trace;
    public void OutPacket(Packet packet, ThrottleOutPacketType throttle)
    {
        Trace.Add("UDP:" + Convert.ToHexString(packet.ToBytes()).ToLowerInvariant());
        if ((Trace.Mode & 32) != 0)
            throw new Exception();
    }
}
partial class ScriptRunningReference
{
    Trace Trace;
    bool TryGetInstance(UUID item, out XMRInstance instance)
    {
        Trace.Add("F:" + item);
        if ((Trace.Mode & 4) != 0)
            throw new Exception();
        instance = null;
        if ((Trace.Mode & 2) != 0 || item != Id(10))
            return false;
        instance = new() { Trace = Trace, m_Item = new() { Trace = Trace } };
        return true;
    }
    void TraceCalls(string format, params object[] args)
    {
        Trace.Add("T:" + args[0] + "," + args[1]);
        if ((Trace.Mode & 128) != 0)
            throw new Exception();
    }
    public static UUID Id(int n) => n == 0 ? UUID.Zero
                                           : new UUID("11223344-5566-7788-99aa-" +
                                                      n.ToString().PadLeft(12, '0'));
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]), obj = int.Parse(p[1]), item = int.Parse(p[2]),
                agent = int.Parse(p[3]);
            var trace = new Trace { Mode = mode };
            var host = new ScriptRunningReference { Trace = trace };
            var client = new Client { Trace = trace, m_agentId = Id(agent) };
            if ((mode & 64) != 0)
                client.Scene.Eq = new EqStub { Trace = trace };
            string status = "OK";
            try
            {
                host.OnGetScriptRunning(client, Id(obj), Id(item));
            }
            catch (Exception)
            {
                status = "ERROR";
            }
            Console.WriteLine(status + ":" + (trace.Stored ? "1" : "0") + "|" +
                              string.Join('|', trace.Values));
        }
    }
}
