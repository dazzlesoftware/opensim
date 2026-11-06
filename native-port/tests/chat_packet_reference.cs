using System;
using OpenMetaverse;
enum ThrottleOutPacketType
{
    Unknown
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
class Client
{
    public object RemoteEndPoint;
}
class Server
{
    public void SendUDPPacket(Client c, UDPPacketBuffer b, ThrottleOutPacketType t)
    {
        Console.WriteLine(Convert.ToHexString(b.Data, 0, b.DataLength).ToLowerInvariant());
    }
}
static class Util
{
    public static byte[] StringToBytes1024(ReadOnlySpan<char> s) => Utils.StringToBytes(s, 1024);
    // Encoding adapter for valid Unicode: equivalent to source Util's bounded helper.
    public static int osUTF8Getbytes(ReadOnlySpan<char> s, byte[] target, int offset, int max, bool term)
    {
        var bytes = Utils.StringToBytes(s, max);
        Buffer.BlockCopy(bytes, 0, target, offset, bytes.Length);
        return bytes.Length;
    }
}
partial class ChatPacketReference
{
    Client m_udpClient = new Client();
    Server m_udpServer = new Server();
    static void Main()
    {
        string line;
        var host = new ChatPacketReference();
        while ((line = Console.ReadLine()) != null)
        {
            var words = line.Split(' ');
            int seed = int.Parse(words[0]), mode = int.Parse(words[1]);
            string token = mode % 2 == 0 ? "a" : "雪";
            string message = "", name = "";
            for (int i = 0; i < seed; ++i)
            {
                message += token;
                if (i < 300)
                    name += token;
            }
            if ((mode & 4) != 0)
            {
                message = message.Insert(message.Length / 2, "\0");
                name = name.Insert(name.Length / 2, "\0");
            }
            host.SendChatMessage(message, (byte)mode, new Vector3(seed * .25f, -seed * .125f, 23), name,
                                 new UUID("00112233-4455-6677-8899-aabbccddeeff"),
                                 new UUID("11223344-5566-7788-99aa-bbccddeeff00"), 2, 1);
        }
    }
}
