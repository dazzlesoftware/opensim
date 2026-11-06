using System;
using System.Collections.Generic;
using System.Reflection;
using System.Globalization;
using OpenMetaverse;
using OpenSim.Framework;
partial class Rpc
{
    partial void Update(ref object data, string method, string uri, string id, ref bool? handled);
    partial void Detail(ref object data, string method, string uri, string id, ref bool? handled)
    {
        Update(ref data, method, uri, id, ref handled);
        if (!handled.HasValue && data is UserClassifiedAdd)
            handled = InfoFixture.Rpc(ref data, method, uri, id);
    }
}
class InfoClient : Client
{
    public override void SendClassifiedInfoReply(UUID classified, UUID creator, uint created, uint expires,
                                                 uint category, string name, string desc, UUID parcel,
                                                 uint estate, UUID snapshot, string sim, Vector3 pos,
                                                 string parcelName, byte flags, int price)
    {
        F.Trace += "detail:" + classified + ":" + creator + ":" + created + ":" + expires + ":" + category +
                   ":" + F.T(name) + ":" + F.T(desc) + ":" + parcel + ":" + estate + ":" + snapshot + ":" +
                   F.T(sim) + ":" + InfoFixture.Vector(pos) + ":" + F.T(parcelName) + ":" + flags + ":" +
                   price + ";";
        F.Fail(268435456, "details");
    }
}
static class InfoFixture
{
    public static string Position;
    public static object Last;
    public static string Vector(Vector3 v) => BitConverter.SingleToInt32Bits(v.X).ToString("x8") +
                                              ":" + BitConverter.SingleToInt32Bits(v.Y).ToString("x8") +
                                              ":" + BitConverter.SingleToInt32Bits(v.Z).ToString("x8");
    public static UserClassifiedAdd Fill(UUID query)
    {
        return new UserClassifiedAdd { ClassifiedId = query,    CreatorId = F.Id(7),
                                       CreationDate = -1,       ExpirationDate = int.MaxValue,
                                       Category = int.MinValue, Name = F.Text,
                                       Description = F.Text,    ParcelId = F.Id(5),
                                       ParentEstate = -7,       SnapshotId = F.Id(9),
                                       SimName = F.Text,        GlobalPos = Position,
                                       ParcelName = F.Text,     Flags = 128,
                                       Price = int.MinValue };
    }
    public static string Dump(object value)
    {
        if (value == null)
            return "N";
        if (value is not UserClassifiedAdd a)
            return "X";
        return a.ClassifiedId + ":" + a.CreatorId + ":" + a.CreationDate + ":" + a.ExpirationDate + ":" +
               a.Category + ":" + F.T(a.Name) + ":" + F.T(a.Description) + ":" + a.ParcelId + ":" +
               a.ParentEstate + ":" + a.SnapshotId + ":" + F.T(a.SimName) + ":" + F.T(a.GlobalPos) + ":" +
               F.T(a.ParcelName) + ":" + a.Flags + ":" + a.Price;
    }
    public static bool Rpc(ref object data, string method, string uri, string id)
    {
        F.Trace += "ad-rpc:" + method + ":" + F.T(uri) + ":" + Dump(data) + ":" +
                   (UUID.TryParse(id, out var guid) && !guid.IsZero() ? 1 : 0) + ";";
        Last = data;
        if (F.Mode == 7)
            throw new Exception("rpc");
        if (F.Mode == 0)
            return false;
        if (F.Mode != 8)
        {
            var query = ((UserClassifiedAdd)data).ClassifiedId;
            var filled = Fill(query);
            if (F.Mode == 3 || F.Mode == 4)
                filled.ClassifiedId = F.Id(4);
            if (F.Mode == 10)
                filled.CreatorId = UUID.Zero;
            if (F.Mode == 11)
                filled.SnapshotId = UUID.Zero;
            if (F.Mode == 12)
                filled.GlobalPos = "bad";
            if (F.Mode == 13)
                filled.ClassifiedId = UUID.Zero;
            if (F.Mode == 14)
                filled.CreatorId = F.Id(2);
            if (F.Mode == 3 || F.Mode == 4)
                data = filled;
            else
            {
                foreach (var field in typeof(UserClassifiedAdd).GetFields())
                    field.SetValue(data, field.GetValue(filled));
            }
        }
        if (F.Mode == 5 || F.Mode == 9)
            data = null;
        if (F.Mode == 6)
            data = new UserPreferences();
        Last = data;
        return F.Mode != 2 && F.Mode != 4 && F.Mode != 9;
    }
    public static string State(Original m)
    {
        string One(UUID target)
        {
            m.Cache.TryGetValue(target, out var entry);
            if (entry == null)
                return m.Cache.Count + ":N";
            string head = m.Cache.Count + ":" + PicksFixture.Identity(entry) + ":" + entry.flags + ":" +
                          F.T(entry.born) + ":" +
                          (entry.picksList == null ? "N" : PicksFixture.Dump(entry.picksList)) + ":" +
                          (entry.classifiedsLists == null ? "N" : PicksFixture.Dump(entry.classifiedsLists)) +
                          ":";
            var rows = new List<string>();
            if (entry.classifieds != null)
                foreach (var pair in entry.classifieds)
                    rows.Add(pair.Key + "=" + PicksFixture.Identity(pair.Value) + ":" + Dump(pair.Value));
            return head + (entry.classifieds == null ? "N" : string.Join(',', rows));
        }
        string a = One(F.Id(1)), b = One(F.Id(2));
        var owners = new List<string>();
        foreach (var row in m.Owners)
            owners.Add(row.Key + "=" + row.Value);
        var counts = new List<string>();
        foreach (var row in m.Interests)
            counts.Add(row.Key + "=" + row.Value);
        return a + ";" + b + ";" + string.Join(',', owners) + ";" + string.Join(',', counts) + ";" +
               PicksFixture.Identity(Last) + ":" + Dump(Last);
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            if (p[0] == "V")
            {
                try
                {
                    bool valid = Vector3.TryParse(PicksFixture.Decode(p[1]), out var value);
                    Console.WriteLine((valid ? 1 : 0) + ":" + Vector(value));
                }
                catch (Exception e)
                {
                    Console.WriteLine("EX:" + e.Message);
                }
                continue;
            }
            F.Mode = int.Parse(p[0]);
            F.Flags = int.Parse(p[1]);
            F.Uri = PicksFixture.Decode(p[2]);
            string arg = PicksFixture.Decode(p[3]);
            F.Text = PicksFixture.Decode(p[4]);
            Position = PicksFixture.Decode(p[5]);
            F.AssetUri = PicksFixture.Decode(p[6]);
            F.Trace = "";
            Last = null;
            PicksFixture.Reset();
            var m = new Original();
            PicksFixture.Current = m;
            ClassifiedsFixture.Current = m;
            IClientAPI client = (F.Flags & 1) != 0 ? null : new InfoClient();
            UUID.TryParse(arg, out var query);
            var output = new List<string>();
            foreach (var token in p[7..])
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
                        case "X":
                            m.ClassifiedDelete(query, client);
                            break;
                        case "Q":
                            m.ClassifiedInfoRequest(query, client);
                            break;
                        case "4":
                            m.ClassifiedInfoRequest(F.Id(4), client);
                            break;
                        case "0":
                            m.ClassifiedInfoRequest(UUID.Zero, client);
                            break;
                        case "F":
                            m.ForeignImage(F.Id(2), query);
                            break;
                        case "L":
                            m.ClassifiedsRequest(client, "ignored", new List<string> { F.Id(2).ToString() });
                            break;
                        case "C":
                            m.Cache.Clear();
                            break;
                        case "Z":
                            m.Owners.Clear();
                            m.Interests.Clear();
                            break;
                        case "P":
                            m.Cache.GetType()
                                .GetMethod("Purge", BindingFlags.Instance | BindingFlags.NonPublic)
                                .Invoke(m.Cache, new object[] { null });
                            break;
                        case "I":
                            m.Owners[query] = F.Id(2);
                            m.Interests.Remove(query);
                            break;
                        case "J":
                            m.Owners[query] = F.Id(2);
                            m.Interests[query] = 0;
                            break;
                        case "T":
                            m.Owners[query] = F.Id(2);
                            m.Interests[query] = int.MinValue;
                            break;
                        case "K":
                            m.Owners[query] = F.Id(2);
                            m.Interests[query] = 2;
                            break;
                        case "M":
                            if (Last is UserClassifiedAdd ad)
                            {
                                ad.Name = "mutated";
                                ad.GlobalPos = Position;
                            }
                            break;
                        case "S":
                            m.Cache.AddOrUpdate(F.Id(1), null, 300.0);
                            break;
                        default:
                            UUID target = token == "B" ? F.Id(2) : F.Id(1);
                            var e = new UserProfileCacheEntry {
                                flags = 55, born = "seed",
                                picksList = new Dictionary<UUID, string> { { F.Id(7), "other" } },
                                classifieds = new(),
                                classifiedsLists =
                                    new Dictionary<UUID, string> { { F.Id(8), "seed list" } }
                            };
                            var seed = Fill(query);
                            if (token == "D")
                                seed.GlobalPos = "bad";
                            e.classifieds[query] = token == "E" ? null : seed;
                            m.Cache.AddOrUpdate(target, e, 300.0);
                            if (token == "B")
                            {
                                m.Owners[query] = target;
                                m.Interests[query] = 1;
                            }
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
partial class Original
{
    public void ForeignImage(UUID agent, UUID image) => cacheForeignImage(agent, image);
}
