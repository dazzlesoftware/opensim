using System;
using System.Collections.Generic;
using OpenMetaverse;
enum ThrottleOutPacketType
{
    Task,
    Asset
}
static class Util
{
    public static int Now;
    public static long GetTimeStampMS() => Now;
}
static class LLUDPServer
{
    public const int MAXPAYLOAD = 1200;
}
class UDPPacketBuffer
{
    public byte[] Data = new byte[65536];
    public int DataLength;
}
static class OpenSimUDPBase
{
    public static UDPPacketBuffer GetNewUDPBuffer(object endpoint) => new UDPPacketBuffer();
}
class UdpClient
{
    public object RemoteEndPoint;
}
class UdpServer
{
    public void SendUDPPacket(UdpClient c, UDPPacketBuffer b, ThrottleOutPacketType t)
    {
        Program.Trace.Add("P" + Convert.ToHexString(b.Data, 0, b.DataLength).ToLowerInvariant());
    }
}
interface IClientAPI
{
    int PingTimeMS { get; }
    int GetAgentThrottleSilent(int t);
    void SendXferPacket(ulong id, uint packet, byte[] data, int offset, int length, bool task);
}
partial class Client : IClientAPI
{
    UdpClient m_udpClient = new UdpClient();
    UdpServer m_udpServer = new UdpServer();
    public int PingTimeMS { get; set; }
    public int Throttle;
    public int GetAgentThrottleSilent(int t) => Throttle;
}
class Log
{
    public void WarnFormat(string f, object x)
    {
    }
}
partial class XferReference
{
    Dictionary<string, FileData> NewFiles = new Dictionary<string, FileData>();
    Dictionary<ulong, XferDownLoad> Transfers = new Dictionary<ulong, XferDownLoad>();
    int lastFilesExpire;
    Log m_log = new Log();
    public XferReference()
    {
        lastFilesExpire = unchecked(Util.Now + 210000);
    }
    public int Files => NewFiles.Count;
    public int Count => Transfers.Count;
}
static class Program
{
    public static List<string> Trace;
    static byte[] Bytes(int size, int salt = 0)
    {
        var b = new byte[size];
        for (int i = 0; i < size; ++i)
            b[i] = (byte)(i * 17 + salt);
        return b;
    }
    static void Run(int kind, int size, int burst, int ping, int mode)
    {
        Trace = new List<string>();
        Util.Now = mode == 5 ? int.MaxValue - 1500 : 100;
        var client = new Client { PingTimeMS = ping, Throttle = burst };
        if (kind == 3)
        {
            var x = new XferReference();
            x.AddNewFile("a", Bytes(size));
            x.AddNewFile("b", Bytes(2048, 1));
            x.AddNewFile("c", Bytes(1, 2));
            x.RequestXfer(client, 10, "a");
            x.RequestXfer(client, 20, "b");
            x.RequestXfer(client, 30, "c");
            Util.Now = unchecked(Util.Now + 12001);
            x.transfersTimeTick(Util.Now);
            x.AbortXfer(client, 20);
            x.AddNewFile("d", Bytes(1025, 3));
            x.RequestXfer(client, 40, "d");
            Util.Now = unchecked(Util.Now + 12001);
            x.transfersTimeTick(Util.Now);
            Trace.Add("S" + x.Files + "," + x.Count);
        }
        else if (kind == 2)
        {
            client.SendTaskInventory(new UUID("11223344-5566-7788-99aa-bbccddeeff00"), (short)burst,
                                     Bytes(size));
        }
        else if (kind == 0)
        {
            var x =
                new XferReference.XferDownLoad("contents", Bytes(size), 0xfedcba9876543210, client, burst);
            x.StartSend();
            int last = Math.Max(0, (size - 1) >> 10);
            Action<uint> ack = n => Trace.Add("A" + (x.AckPacket(n) ? "1" : "0"));
            Action<int> tick = n =>
            {
                Util.Now = unchecked(Util.Now + n);
                Trace.Add("T" + (x.checkTime(Util.Now) ? "1" : "0"));
            };
            if (mode == 0 || mode == 5)
            {
                for (uint n = 0; n <= last; ++n)
                    ack(n);
                ack((uint)last | 0x80000000u);
            }
            else if (mode == 1)
            {
                ack((uint)last | 0x80000000u);
                ack((uint)last);
            }
            else if (mode == 2)
            {
                ack(0);
                ack(0);
                ack(0);
                tick(1000);
                tick(1);
            }
            else if (mode == 3)
            {
                tick(2000);
                tick(1);
                for (int n = 0; n < 5; ++n)
                    tick(12001);
                ack(0);
            }
            else if (mode == 4)
            {
                x.done();
                Trace.Add("D" + (x.isDeleted ? "1" : "0"));
                ack(0);
                tick(50000);
            }
            else if (mode == 6)
            {
                for (int n = 0; n < 3; ++n)
                {
                    tick(12001);
                    ack((uint)n);
                }
                tick(12001);
            }
            else if (mode == 7)
            {
                ack(0x7ffffffe);
                tick(12001);
            }
            Trace.Add("Z" + (x.isDeleted ? "1" : "0"));
        }
        else
        {
            var x = new XferReference();
            x.AddNewFile("a", Bytes(size));
            if (mode != 0)
                x.AddNewFile("a", Bytes(size, 3));
            x.AddNewFile("b", Bytes(1));
            Action state = () => Trace.Add("S" + x.Files + "," + x.Count);
            state();
            x.RequestXfer(client, 10, "missing");
            state();
            x.RequestXfer(client, 10, "a");
            state();
            x.RequestXfer(client, 10, "b");
            state();
            if (mode == 0 || mode == 1)
            {
                for (uint n = 0; n <= Math.Max(0, (size - 1) >> 10); ++n)
                    x.AckPacket(client, 10, n);
                state();
                x.AckPacket(client, 10, (uint)Math.Max(0, (size - 1) >> 10));
                state();
            }
            else if (mode == 2)
            {
                x.AbortXfer(client, 10);
                state();
                x.AbortXfer(client, 10);
                state();
            }
            else
            {
                for (int i = 0; i < 7; ++i)
                {
                    Util.Now = unchecked(Util.Now + 12001);
                    x.transfersTimeTick(Util.Now);
                    state();
                }
            }
            x.RequestXfer(client, 11, "a");
            state();
            x.RequestXfer(client, 12, "b");
            state();
            x.expireFiles(unchecked(Util.Now + 500000));
            state();
        }
        Console.WriteLine(string.Join("|", Trace));
    }
    static void Main()
    {
        string s;
        while ((s = Console.ReadLine()) != null)
        {
            var w = s.Split(' ');
            Run(int.Parse(w[0]), int.Parse(w[1]), int.Parse(w[2]), int.Parse(w[3]), int.Parse(w[4]));
        }
    }
}
