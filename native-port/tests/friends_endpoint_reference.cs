using System;
using System.IO;
using System.Xml;
using System.Text;
using System.Collections.Generic;
using OpenMetaverse;
using OpenSim.Services.Interfaces;
using FriendInfo = OpenSim.Services.Interfaces.FriendInfo;
public class IOSHttpRequest
{
}
public class IOSHttpResponse
{
}
public class EndpointLogger
{
    public void DebugFormat(string f, params object[] a)
    {
    }
    public void WarnFormat(string f, params object[] a)
    {
    }
}
public static class EndpointUtil
{
    public static Encoding UTF8NoBomEncoding = new UTF8Encoding(false);
    public static byte[] DocToBytes(XmlDocument d)
    {
        using var stream = new MemoryStream();
        using (var writer = new XmlTextWriter(stream, null) { Formatting = Formatting.Indented })
        {
            d.WriteTo(writer);
            writer.Flush();
        }
        return stream.ToArray();
    }
}
public partial class FriendsHandler
{
    IFriendsService m_FriendsService;
    EndpointLogger m_log = new();
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    class Service : IFriendsService
    {
        public string trace = "";
        FriendInfo[] Data(UUID id)
        {
            if (id.ToString().EndsWith("9"))
                throw new Exception();
            if (id.ToString().EndsWith("8"))
                return null;
            if (id == UUID.Zero)
                return Array.Empty<FriendInfo>();
            if (id.ToString().EndsWith("1"))
                return new FriendInfo[] { null };
            return new[] { new FriendInfo { PrincipalID = id, Friend = "friend<&>\r\n\t", MyFlags = 7,
                                            TheirFlags = 5 },
                           new FriendInfo { MyFlags = -2147483647, TheirFlags = 2147483647 } };
        }
        bool Okay(string id)
        {
            if (id.EndsWith("9"))
                throw new Exception();
            return !id.EndsWith("8");
        }
        public FriendInfo[] GetFriends(UUID id)
        {
            trace += "get:" + id + ";";
            return Data(id);
        }
        public FriendInfo[] GetFriends(string id)
        {
            trace += "string:" + H(id) + ";";
            UUID.TryParse(id, out UUID uuid);
            return Data(uuid);
        }
        public bool StoreFriend(string id, string friend, int flags)
        {
            trace += "store:" + H(id) + ":" + H(friend) + ":" + flags + ";";
            return Okay(id);
        }
        public bool Delete(UUID id, string friend)
        {
            trace += "delete:" + id + ":" + H(friend) + ";";
            return Okay(id.ToString());
        }
        public bool Delete(string id, string friend)
        {
            trace += "delete-string:" + H(id) + ":" + H(friend) + ";";
            return Okay(id);
        }
    }
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var service = new Service();
            var handler = new FriendsHandler { m_FriendsService = service };
            var bytes = handler.ProcessRequest("/friends", new MemoryStream(Convert.FromHexString(line)),
                                               new(), new());
            Console.WriteLine(Convert.ToHexString(bytes).ToLowerInvariant() + "|" + H(service.trace));
        }
    }
}
namespace OpenSim.Framework
{
    public class Placeholder
    {
    }
}
