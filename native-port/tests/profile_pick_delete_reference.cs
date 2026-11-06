using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using OpenMetaverse;
using OpenSim.Framework;
static class DeleteFixture
{
    public static string State(Original m)
    {
        string One(UUID key)
        {
            m.Cache.TryGetValue(key, out var e);
            string count = m.Cache.Count.ToString();
            if (e == null)
                return count + ":N";
            string identity = PicksFixture.Identity(e), listId = PicksFixture.Identity(e.picksList);
            string rows = "";
            if (e.picks != null)
                foreach (var row in e.picks)
                {
                    if (rows != "")
                        rows += ",";
                    rows += row.Key + "=" + PicksFixture.Identity(row.Value) + ":" +
                            (row.Value == null ? "N" : row.Value.PickId + ":" + F.T(row.Value.Name));
                }
            string props = PicksFixture.Identity(e.props), ads = PicksFixture.Identity(e.classifiedsLists);
            return count + ":" + identity + ":" + e.flags + ":" + F.T(e.born) + ":" + listId + ":" +
                   PicksFixture.Dump(e.picksList) + ":" + (e.picks == null ? "N" : rows) + ":" + props + ":" +
                   (e.props == null ? "N" : e.props.UserId.ToString()) + ":" + ads + ":" +
                   PicksFixture.Dump(e.classifiedsLists);
        }
        string a = One(F.Id(1)), b = One(F.Id(2)), saved = PicksFixture.Identity(PicksFixture.Saved);
        return a + ";" + b + ";" + saved + ":" + PicksFixture.Dump(PicksFixture.Saved);
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
            UUID.TryParse(PicksFixture.Decode(p[3]), out UUID query);
            F.Text = PicksFixture.Decode(p[4]);
            F.Trace = "";
            PicksFixture.Reset();
            var m = new Original();
            PicksFixture.Current = m;
            IClientAPI client = (F.Flags & 1) != 0 ? null : new PicksClient();
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
                        case "D":
                            m.PickDelete(client, query);
                            break;
                        case "Z":
                            m.PickDelete(client, F.Id(4));
                            break;
                        case "0":
                            m.PickDelete(client, UUID.Zero);
                            break;
                        case "G":
                            PicksFixture.Saved = m.GetPicks(F.Id(1));
                            break;
                        case "R":
                            m.PicksRequest(client, "ignored", new() { F.Id(1).ToString() });
                            break;
                        case "N":
                            m.Cache.AddOrUpdate(F.Id(1), null, 300.0);
                            break;
                        case "C":
                            m.Cache.Clear();
                            break;
                        case "M":
                            PicksFixture.Saved[F.Id(8)] = F.Text;
                            break;
                        case "W":
                            PicksFixture.Saved.Clear();
                            break;
                        case "P":
                            m.Cache.GetType()
                                .GetMethod("Purge", BindingFlags.Instance | BindingFlags.NonPublic)
                                .Invoke(m.Cache, new object[] { null });
                            break;
                        default:
                            var e = new UserProfileCacheEntry {
                                flags = 55, born = "seed",
                                props = new UserProfileProperties { UserId = F.Id(7) },
                                classifiedsLists = new Dictionary<UUID, string> { { F.Id(9), "ad" } }
                            };
                            if (token != "L")
                            {
                                e.picksList = new();
                                e.picks = new();
                                if (token != "E")
                                {
                                    e.picksList[query] = F.Text;
                                    e.picksList[F.Id(4)] = "second";
                                    e.picks[query] = new UserProfilePick { PickId = query, Name = F.Text };
                                    e.picks[F.Id(4)] = null;
                                }
                            }
                            m.Cache.AddOrUpdate(token == "B" ? F.Id(2) : F.Id(1), e, 300.0);
                            break;
                        }
                }
                catch (Exception error)
                {
                    if (error is TargetInvocationException && error.InnerException != null)
                        error = error.InnerException;
                    result = "EX:" + F.H(error.Message);
                }
                output.Add(result + ":" + State(m));
            }
            Console.WriteLine(string.Join('/', output) + "|" + F.H(F.Trace));
        }
    }
}
