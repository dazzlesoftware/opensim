using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net;
using OpenMetaverse;
using OpenMetaverse.Packets;
using OpenSim.Region.Framework.Scenes.Animation;
namespace log4net
{
}
enum ThrottleOutPacketType
{
    Task
}
class ClientStub
{
    public IPEndPoint RemoteEndPoint = new(IPAddress.Loopback, 9000);
}
class OpenSimUDPBase
{
    public static UDPPacketBuffer GetNewUDPBuffer(IPEndPoint endpoint) => new UDPPacketBuffer(endpoint);
}
class ServerStub
{
    public byte[] Raw;
    public void SendUDPPacket(ClientStub client, UDPPacketBuffer buffer, ThrottleOutPacketType type)
    {
        Raw = buffer.Data.Take(buffer.DataLength).ToArray();
    }
}
partial class AvatarAnimationReference
{
    UUID m_agentId;
    ClientStub m_udpClient = new();
    ServerStub m_udpServer = new();
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    static void SafeUuid(UUID value, byte[] data, int pos)
    {
        if (pos < 0 || pos > data.Length - 16)
            throw new IndexOutOfRangeException();
        value.ToBytes(data, pos);
    }
    static void SafeInt(int value, byte[] data, int pos)
    {
        if (pos < 0 || pos > data.Length - 4)
            throw new IndexOutOfRangeException();
        Utils.IntToBytesSafepos(value, data, pos);
    }
    static void Entries(int count, int mode, out UUID[] animations, out UUID[] objects, out int[] sequences)
    {
        animations = new UUID[count];
        objects = new UUID[count];
        sequences = new int[count];
        int[] values = { 0, -1, int.MinValue, int.MaxValue, 0x12345678 };
        for (int n = 0; n < count; n++)
        {
            int choice = (mode >> 1) % 4;
            UUID obj = UUID.Zero;
            if (choice == 1)
                obj = Id(999);
            if (choice == 2)
                obj = Id(777 + n % 3);
            if (choice == 3)
                obj = (n % 4) switch {
                    0 => Id(777),
                    1 => Id(999),
                    2 => UUID.Zero,
                    _ => Id(778)
                };
            animations[n] = Id(n % 13 + 1);
            objects[n] = obj;
            sequences[n] = values[(n + mode) % 5];
        }
    }
    static string Packet(int count, int mode)
    {
        var host = new AvatarAnimationReference { m_agentId = (mode & 1) != 0 ? Id(999) : Id(998) };
        Entries(count, mode, out var animations, out var objects, out var sequences);
        try
        {
            host.SendAnimations(animations, sequences, Id(999), objects);
            int i = 0;
            var sdk = new AvatarAnimationPacket(host.m_udpServer.Raw, ref i);
            if (!host.m_udpServer.Raw.SequenceEqual(sdk.ToBytes()))
                throw new Exception("SDK raw packet round trip differs");
            return Convert.ToHexString(host.m_udpServer.Raw).ToLowerInvariant();
        }
        catch (IndexOutOfRangeException)
        {
            return "BUFFER";
        }
    }
    static string Codec255()
    {
        Entries(255, 0, out var animations, out var objects, out var sequences);
        var packet = new AvatarAnimationPacket();
        packet.Sender.ID = Id(999);
        packet.AnimationList = new AvatarAnimationPacket.AnimationListBlock[255];
        packet.AnimationSourceList = Array.Empty<AvatarAnimationPacket.AnimationSourceListBlock>();
        packet.PhysicalAvatarEventList = Array.Empty<AvatarAnimationPacket.PhysicalAvatarEventListBlock>();
        for (int n = 0; n < 255; n++)
            packet.AnimationList[n] =
                new AvatarAnimationPacket.AnimationListBlock { AnimID = animations[n],
                                                               AnimSequenceID = sequences[n] };
        return Convert.ToHexString(packet.ToBytes()).ToLowerInvariant();
    }
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        if (args.Length > 0 && args[0] == "--codec255")
        {
            Console.WriteLine(Codec255());
            return;
        }
        if (args.Length > 0 && args[0] == "--catalog")
        {
            foreach (var p in DefaultAvatarAnimations.AnimsUUIDbyName.OrderBy(p => p.Key,
                              StringComparer.Ordinal))
                Console.WriteLine("N:" + p.Key + ":" + p.Value);
            foreach (var p in DefaultAvatarAnimations.AnimsNamesbyUUID.OrderBy(p => p.Key.ToString(),
                              StringComparer.Ordinal))
                Console.WriteLine("I:" + p.Key + ":" + p.Value);
            foreach (var p in DefaultAvatarAnimations.AnimStateNames.OrderBy(p => p.Key.ToString(),
                              StringComparer.Ordinal))
                Console.WriteLine("S:" + p.Key + ":" + p.Value);
            string line;
            while ((line = Console.ReadLine()) != null)
            {
                var parts = line.Split(' ');
                Console.WriteLine(parts[0] == "R"
                                      ? DefaultAvatarAnimations
                                            .GetDefaultAnimation(parts[1] == "-"
                                                                     ? ""
                                                                     : System.Text.Encoding.UTF8.GetString(
                                                                           Convert.FromHexString(parts[1])))
                                            .ToString()
                                      : DefaultAvatarAnimations.GetDefaultAnimationName(new UUID(parts[1])));
            }
            return;
        }
        string input;
        while ((input = Console.ReadLine()) != null)
        {
            var p = input.Split(' ');
            Console.WriteLine(Packet(int.Parse(p[0]), int.Parse(p[1])));
        }
    }
}
