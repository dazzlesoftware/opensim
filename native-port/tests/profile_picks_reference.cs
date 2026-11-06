using System;
using System.Collections.Generic;
using System.Reflection;
using System.Globalization;
using OpenMetaverse;
using OpenMetaverse.StructuredData;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    public static partial class Util
    {
        public static double GetTimeStampMS() => PicksFixture.Now;
    }
}
namespace System.Threading
{
    public sealed class Timer : IDisposable
    {
        public Timer(TimerCallback callback, object state, int due, int period)
        {
            F.Trace += "T:" + due + ";";
        }
        public bool Change(int due, int period)
        {
            F.Trace += "TC:" + due + ";";
            return true;
        }
        public void Dispose()
        {
            F.Trace += "TD;";
        }
    }
}
partial class Original
{
    const double PROFILECACHEEXPIRE = 300;
    readonly ExpiringCacheOS<UUID, UserProfileCacheEntry> m_profilesCache = new(60000);
    partial void SetupCache()
    {
        GC.SuppressFinalize(m_profilesCache);
    }
    public ExpiringCacheOS<UUID, UserProfileCacheEntry> Cache => m_profilesCache;
}
partial class Rpc
{
    public bool JsonRpcRequest(ref OSD data, string method, string uri, string id)
    {
        string idKey = method == "avatarclassifiedsrequest" ? "classifieduuid" : "pickuuid";
        var map = (OSDMap)data;
        F.Trace += "osd:" + method + ":" + F.T(uri) + ":" +
                   map[method == "picks_delete" ? "pickId" : "creatorId"].AsUUID() + ":" +
                   (UUID.TryParse(id, out UUID guid) && !guid.IsZero() ? 1 : 0) + ";";
        if (F.Mode == 7)
            throw new Exception("rpc");
        if (F.Mode == 0)
            return false;
        if (F.Mode == 4)
        {
            data = new OSDArray();
            return true;
        }
        if (F.Mode == 5)
        {
            data = null;
            return true;
        }
        var response = new OSDMap();
        data = response;
        if (F.Mode == 2)
            return true;
        if (F.Mode == 3)
        {
            response["result"] = new OSDString("bad");
            return true;
        }
        if (F.Mode == 17)
        {
            response["result"] = null;
            return true;
        }
        var list = new OSDArray();
        response["result"] = list;
        OSDMap Row(UUID key, string value) =>
            new OSDMap { { idKey, OSD.FromUUID(key) }, { "name", OSD.FromString(value) } };
        if (F.Mode == 6)
            list.Add(null);
        else if (F.Mode == 8)
        {
            list.Add(Row(F.Id(3), "old"));
            list.Add(Row(F.Id(3), F.Text));
            list.Add(new OSDMap());
        }
        else if (F.Mode == 10)
            list.Add(new OSDMap { { idKey, new OSDString("bad") }, { "name", new OSDInteger(42) } });
        else if (F.Mode == 11)
            list.Add(new OSDString("bad"));
        else if (F.Mode == 13)
        {
            list.Add(Row(F.Id(3), F.Text));
            list.Add(new OSDInteger(7));
        }
        else if (F.Mode == 14)
        {
            list.Add(new OSDMap { { "name", OSD.FromString(F.Text) } });
            list.Add(new OSDMap { { idKey, new OSDString(F.Id(4).ToString()) } });
        }
        else if (F.Mode != 12)
        {
            list.Add(Row(F.Id(3), F.Text));
            list.Add(Row(F.Id(4), "second"));
        }
        return F.Mode != 9;
    }
}
class PicksClient : Client
{
    public override void SendAvatarPicksReply(UUID target, Dictionary<UUID, string> picks)
    {
        F.Trace +=
            "picks:" + target + ":" + PicksFixture.Dump(picks) + ":" + PicksFixture.Identity(picks) + ";";
        PicksFixture.Saved = picks;
        if ((F.Flags & 16777216) != 0)
            picks[F.Id(9)] = F.Text;
        if ((F.Flags & 33554432) != 0)
        {
            var again = PicksFixture.Current.GetPicks(target);
            F.Trace += "again:" + PicksFixture.Identity(again) + ":" + PicksFixture.Dump(again) + ";";
        }
        F.Fail(16384, "picks");
    }
}
static class PicksFixture
{
    public static Original Current;
    public static double Now;
    public static Dictionary<UUID, string> Saved;
    static Dictionary<object, int> identities;
    public static string Identity(object o)
    {
        if (o == null)
            return "N";
        if (!identities.TryGetValue(o, out int id))
            identities[o] = id = identities.Count + 1;
        return id.ToString();
    }
    public static string Dump(Dictionary<UUID, string> picks)
    {
        if (picks == null)
            return "N";
        var rows = new List<string>();
        foreach (var pair in picks)
            rows.Add(pair.Key + "=" + F.T(pair.Value));
        return string.Join(',', rows);
    }
    public static string Group(GroupMembershipData g) =>
        g.GroupID + ":" + F.T(g.GroupName) + ":" + (g.AllowPublish ? 1 : 0) + ":" +
        (g.MaturePublish ? 1 : 0) + ":" + F.T(g.Charter) + ":" + g.FounderID + ":" + g.GroupPicture +
        ":" + g.MembershipFee + ":" + (g.OpenEnrollment ? 1 : 0) + ":" + (g.ShowInList ? 1 : 0) + ":" +
        (g.AcceptNotices ? 1 : 0) + ":" + g.Contribution + ":" + g.GroupPowers + ":" + (g.Active ? 1 : 0) +
        ":" + g.ActiveRole + ":" + (g.ListInProfile ? 1 : 0) + ":" + F.T(g.GroupTitle);
    public static string State(Original module, UUID target)
    {
        module.Cache.TryGetValue(target, out var e);
        if (e == null)
            return module.Cache.Count + ":N";
        int mask = (e.picksList != null ? 1 : 0) | (e.picks != null ? 2 : 0) |
                   (e.classifiedsLists != null ? 4 : 0) | (e.classifieds != null ? 8 : 0) |
                   (e.props != null ? 16 : 0) | (e.avatarGroups != null ? 32 : 0) |
                   (e.born != null ? 64 : 0) | (e.membershipType != null ? 128 : 0) |
                   (e.ClientsWaitingProps != null ? 256 : 0);
        return module.Cache.Count + ":" + Identity(e) + ":" + mask + ":" + e.flags + ":" + F.T(e.born) + ":" +
               (e.props == null ? "N" : e.props.UserId.ToString()) + ":" +
               (e.membershipType == null ? "N" : Convert.ToHexString(e.membershipType).ToLowerInvariant()) +
               ":" + (e.ClientsWaitingProps == null ? "N" : e.ClientsWaitingProps.Count.ToString()) + ":" +
               (e.avatarGroups == null ? "N" : Group(e.avatarGroups[0])) + ":" + Identity(e.picksList) + ":" +
               Dump(e.picksList);
    }
    public static string Decode(string value)
    {
        if (value == "-")
            return null;
        if (value == ".")
            return "";
        var bytes = Convert.FromHexString(value);
        var chars = new char[bytes.Length / 2];
        for (int i = 0; i < chars.Length; i++)
            chars[i] = (char)(bytes[i * 2] | bytes[i * 2 + 1] << 8);
        return new string(chars);
    }
    public static void Reset()
    {
        Now = 0;
        Saved = null;
        identities = new(ReferenceEqualityComparer.Instance);
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            F.Mode = int.Parse(p[0]);
            F.Flags = int.Parse(p[1]);
            F.Uri = Decode(p[2]);
            string arg = Decode(p[3]);
            F.Text = Decode(p[4]);
            F.Trace = "";
            Now = 0;
            Saved = null;
            identities = new(ReferenceEqualityComparer.Instance);
            var module = new Original();
            Current = module;
            IClientAPI client = (F.Flags & 1) != 0 ? null : new PicksClient();
            UUID.TryParse(arg, out UUID target);
            var output = new List<string>();
            foreach (var token in p[5..])
            {
                string result = "OK";
                try
                {
                    if (token[0] == 't')
                        Now = double.Parse(token[1..], CultureInfo.InvariantCulture);
                    else if (token[0] == 'm')
                        F.Mode = int.Parse(token[1..]);
                    else
                        switch (token)
                        {
                        case "R":
                            var args = (F.Flags & 4) != 0   ? null
                                       : (F.Flags & 8) != 0 ? new List<string>()
                                                            : new List<string> { arg };
                            module.PicksRequest((F.Flags & 2) != 0 ? new object() : client, "ignored", args);
                            break;
                        case "G":
                            Saved = module.GetPicks(target);
                            result = Identity(Saved) + ":" + Dump(Saved);
                            break;
                        case "M":
                            Saved[F.Id(8)] = F.Text;
                            break;
                        case "W":
                            Saved.Clear();
                            break;
                        case "C":
                            module.Cache.Clear();
                            break;
                        case "P":
                            module.Cache.GetType()
                                .GetMethod("Purge", BindingFlags.Instance | BindingFlags.NonPublic)
                                .Invoke(module.Cache, new object[] { null });
                            break;
                        case "D":
                            result = module.Cache.Remove(target) ? "1" : "0";
                            break;
                        case "N":
                            module.Cache.AddOrUpdate(target, null, 300.0);
                            break;
                        default:
                            var e = new UserProfileCacheEntry();
                            if (token == "L" || token == "K")
                            {
                                e.picksList = new();
                                if (token == "K")
                                {
                                    e.picksList[F.Id(3)] = "seed1";
                                    e.picksList[F.Id(4)] = "seed2";
                                }
                            }
                            if (token == "O")
                            {
                                e.picks = new();
                                e.classifiedsLists = new();
                                e.classifieds = new();
                                e.props = new UserProfileProperties { UserId = F.Id(7) };
                                e.avatarGroups = new[] { new GroupMembershipData() };
                                e.born = "seed";
                                e.membershipType = new byte[] { 1, 2 };
                                e.flags = 55;
                                e.ClientsWaitingProps = new() { client, client };
                            }
                            module.Cache.AddOrUpdate(target, e, 300.0);
                            break;
                        }
                }
                catch (Exception error)
                {
                    if (error is TargetInvocationException && error.InnerException != null)
                        error = error.InnerException;
                    result = "EX:" + F.H(error.Message);
                }
                output.Add(result + ":" + State(module, target));
            }
            Console.WriteLine(string.Join('/', output) + "|" + F.H(F.Trace));
        }
    }
}
