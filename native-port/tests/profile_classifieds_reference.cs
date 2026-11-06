using System;
using System.Collections.Generic;
using System.Reflection;
using System.Globalization;
using OpenMetaverse;
using OpenSim.Framework;
partial class Original
{
    readonly Dictionary<UUID, UUID> m_classifiedCache = new();
    readonly Dictionary<UUID, int> m_classifiedInterest = new();
    public Dictionary<UUID, UUID> Owners => m_classifiedCache;
    public Dictionary<UUID, int> Interests => m_classifiedInterest;
}
class ClassifiedClient : Client
{
    bool nested;
    public override void SendAvatarClassifiedReply(UUID target, Dictionary<UUID, string> value)
    {
        F.Trace += "classifieds:" + target + ":" + PicksFixture.Dump(value) + ":" +
                   PicksFixture.Identity(value) + ";";
        PicksFixture.Saved = value;
        if ((F.Flags & 16777216) != 0)
            value[F.Id(9)] = F.Text;
        if ((F.Flags & 33554432) != 0 && !nested)
        {
            nested = true;
            try
            {
                ClassifiedsFixture.Current.ClassifiedsRequest(this, "ignored",
                                                              new List<string> { target.ToString() });
            }
            finally
            {
                nested = false;
            }
        }
        F.Fail(16384, "classifieds");
    }
}
static class ClassifiedsFixture
{
    public static Original Current;
    static string State(Original m, UUID a)
    {
        string StateFor(UUID id)
        {
            m.Cache.TryGetValue(id, out var e);
            string before = PicksFixture.State(m, id);
            return before + ":" + PicksFixture.Identity(e?.classifiedsLists) + ":" +
                   PicksFixture.Dump(e?.classifiedsLists);
        }
        var owners = new List<string>();
        foreach (var p in m.Owners)
            owners.Add(p.Key + "=" + p.Value);
        var interests = new List<string>();
        foreach (var p in m.Interests)
            interests.Add(p.Key + "=" + p.Value);
        string first = StateFor(a), second = StateFor(F.Id(2));
        return first + ";" + second + ";" + string.Join(',', owners) + ";" + string.Join(',', interests);
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
            F.Uri = PicksFixture.Decode(p[2]);
            string arg = PicksFixture.Decode(p[3]);
            F.Text = PicksFixture.Decode(p[4]);
            F.Trace = "";
            PicksFixture.Reset();
            var m = new Original();
            Current = m;
            PicksFixture.Current = m;
            IClientAPI client = (F.Flags & 1) != 0 ? null : new ClassifiedClient();
            UUID.TryParse(arg, out UUID target);
            var output = new List<string>();
            foreach (var token in p[5..])
            {
                string result = "OK";
                try
                {
                    if (token[0] == 't')
                        PicksFixture.Now = double.Parse(token[1..], CultureInfo.InvariantCulture);
                    else if (token[0] == 'm')
                        F.Mode = int.Parse(token[1..]);
                    else
                        switch (token)
                        {
                        case "A":
                        case "B":
                            string query = token == "B" ? F.Id(2).ToString() : arg;
                            var args = (F.Flags & 4) != 0   ? null
                                       : (F.Flags & 8) != 0 ? new List<string>()
                                                            : new List<string> { query };
                            m.ClassifiedsRequest((F.Flags & 2) != 0 ? new object() : client, "ignored", args);
                            break;
                        case "G":
                            PicksFixture.Saved = m.GetPicks(target);
                            result = PicksFixture.Identity(PicksFixture.Saved) + ":" +
                                     PicksFixture.Dump(PicksFixture.Saved);
                            break;
                        case "M":
                            PicksFixture.Saved[F.Id(8)] = F.Text;
                            break;
                        case "W":
                            PicksFixture.Saved.Clear();
                            break;
                        case "H":
                            m.Cache.Clear();
                            break;
                        case "C":
                            m.Cache.Clear();
                            m.Owners.Clear();
                            m.Interests.Clear();
                            break;
                        case "P":
                            m.Cache.GetType()
                                .GetMethod("Purge", BindingFlags.Instance | BindingFlags.NonPublic)
                                .Invoke(m.Cache, new object[] { null });
                            break;
                        case "D":
                            result = m.Cache.Remove(target) ? "1" : "0";
                            break;
                        case "I":
                            m.Owners[F.Id(3)] = target;
                            m.Interests.Remove(F.Id(3));
                            break;
                        case "J":
                            m.Owners.Remove(F.Id(3));
                            m.Interests[F.Id(3)] = 7;
                            break;
                        case "V":
                            m.Owners[F.Id(3)] = F.Id(2);
                            m.Interests[F.Id(3)] = int.MaxValue;
                            break;
                        case "Z":
                            m.Owners.Clear();
                            m.Interests.Clear();
                            break;
                        case "N":
                            m.Cache.AddOrUpdate(target, null, 300.0);
                            break;
                        default:
                            var e = new UserProfileCacheEntry();
                            if (token == "L" || token == "K")
                            {
                                e.classifiedsLists = new();
                                if (token == "K")
                                {
                                    e.classifiedsLists[F.Id(3)] = "seed1";
                                    e.classifiedsLists[F.Id(4)] = "seed2";
                                }
                            }
                            if (token == "O")
                            {
                                e.picksList = new() { { F.Id(7), "other picks" } };
                                e.props = new UserProfileProperties { UserId = F.Id(7) };
                                e.flags = 55;
                                e.born = "seed";
                            }
                            m.Cache.AddOrUpdate(target, e, 300.0);
                            break;
                        }
                }
                catch (Exception error)
                {
                    if (error is TargetInvocationException && error.InnerException != null)
                        error = error.InnerException;
                    result = "EX:" + F.H(error.Message);
                }
                output.Add(result + ":" + State(m, target));
            }
            Console.WriteLine(string.Join('/', output) + "|" + F.H(F.Trace));
        }
    }
}
