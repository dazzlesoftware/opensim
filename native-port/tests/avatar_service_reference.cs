using System;
using System.IO;
using System.Linq;
using System.Text;
using System.Collections.Generic;
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Framework;
using OpenSim.Services.Interfaces;
public partial class AvatarServiceReference
{
    Database m_Database;
    Logger m_log = new Logger();
    static string Hex(byte[] b) => Convert.ToHexString(b).ToLowerInvariant();
    static string H(string s) => Hex(Encoding.UTF8.GetBytes(s));
    static string Number(float n) =>
        (float.IsNaN(n)? 0x7fc00000u: BitConverter.SingleToUInt32Bits(n)).ToString("x");
    static UUID Id(int n) => new UUID("00000000-0000-0000-0000-" + n.ToString("x12"));
    static string Optional(string s) => s == null ? "null" : "s" + H(s);
    static string Data(AvatarData a)
    {
        string s = a.AvatarType + ":";
        if (a.Data == null)
            return s + "null";
        foreach (var p in a.Data)
            s += H(p.Key) + "=" + Optional(p.Value) + ";";
        return s;
    }
    class Database
    {
        public SortedDictionary<string, SortedDictionary<string, string>> rows = new(StringComparer.Ordinal);
        public List<AvatarBaseData> held = new();
        public string trace = "";
        public int outcome, target, mutation, step, stores;
        public AvatarData avatar;
        SortedDictionary<string, string> Rows(UUID id)
        {
            string k = id.ToString();
            if (!rows.ContainsKey(k))
                rows[k] = new(StringComparer.Ordinal);
            return rows[k];
        }
        bool Touch()
        {
            ++step;
            if (outcome == 2 && step == target)
                throw new Exception();
            return !(outcome == 1 && step == target);
        }
        public AvatarBaseData[] Get(string field, string principal)
        {
            trace += "get:" + principal + ";";
            if (!Touch())
                return Array.Empty<AvatarBaseData>();
            if (mutation == 6)
                return new AvatarBaseData[] { null };
            if (mutation == 7)
                return new[] { new AvatarBaseData() };
            var id = new UUID(principal);
            return Rows(id)
                .Select(p => new AvatarBaseData {
                    PrincipalID = id,
                    Data = new Dictionary<string, string> { { "Name", p.Key }, { "Value", p.Value } }
                })
                .ToArray();
        }
        public bool Store(AvatarBaseData row)
        {
            ++stores;
            held.Add(row);
            string name = row.Data["Name"], value = row.Data["Value"];
            trace += "store:" + row.PrincipalID + ":" + Optional(name) + ":" + Optional(value) + ";";
            if (!Touch())
                return false;
            Rows(row.PrincipalID)[name ?? "<null>"] = value;
            if (mutation == 2 && stores == 1)
                avatar.Data = new Dictionary<string, string> { { "replacement", "new" } };
            if (mutation == 3 && stores == 2)
                avatar.Data["added"] = "late";
            if (mutation == 4 && stores == 2)
                avatar.Data.Remove("Custom");
            if (mutation == 5 && stores == 2)
                avatar.Data.Clear();
            if (mutation == 6)
                row.PrincipalID = Id(8);
            if ((mutation == 8 && stores == 1) || (mutation == 10 && stores == 2))
                row.Data = null;
            if (mutation == 9 && stores == 1)
                avatar.Data = null;
            return true;
        }
        public bool Delete(string field, string principal)
        {
            trace += "delete:" + principal + ";";
            if (!Touch())
                return false;
            var r = Rows(new UUID(principal));
            bool result = r.Count > 0;
            r.Clear();
            if (mutation == 1)
            {
                avatar.AvatarType = 44;
                avatar.Data = new Dictionary<string, string> { { "replacement", "new" } };
            }
            return result;
        }
        public bool Delete(UUID principal, string name)
        {
            trace += "remove:" + principal + ":" + Optional(name) + ";";
            return Touch() && Rows(principal).Remove(name ?? "<null>");
        }
        public string Snapshot()
        {
            string s = "";
            foreach (var r in rows)
                foreach (var p in r.Value)
                    s += r.Key + ":" + H(p.Key) + "=" + Optional(p.Value) + ";";
            s += '|';
            foreach (var r in held)
                s += r.PrincipalID + ":" +
                     (r.Data == null ? "null" : Optional(r.Data["Name"]) + ":" + Optional(r.Data["Value"])) +
                     ";";
            return s;
        }
        public void Seed(UUID principal, string key, string value) => Rows(principal)[key] = value;
        public void Remove(UUID principal, string key) => Rows(principal).Remove(key);
        public void Clear(UUID principal) => Rows(principal).Clear();
    }
    public static void Main()
    {
        Culture.SetCurrentCulture();
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]);
            string height = p[4] == "-"   ? null
                            : p[4] == "." ? ""
                                          : Encoding.UTF8.GetString(Convert.FromHexString(p[4]));
            var a = new AvatarData { AvatarType = 2, Data = new Dictionary<string, string> {
                                        { "Serial", "7" }, { "AvatarHeight", height }, { "Custom", "value" }
                                    } };
            var db = new Database { outcome = int.Parse(p[1]), target = int.Parse(p[2]),
                                    mutation = int.Parse(p[3]), avatar = a };
            db.Seed(Id(7), "AvatarType", "9");
            db.Seed(Id(7), "old", "kept");
            if (mode == 4 || mode == 6)
            {
                if (db.mutation == 1)
                    db.Seed(Id(7), "AvatarType", "bad");
                if (db.mutation == 2)
                    db.Seed(Id(7), "AvatarType", null);
                if (db.mutation == 3)
                    db.Remove(Id(7), "AvatarType");
                if (db.mutation == 4)
                    db.Seed(Id(7), "custom", null);
                if (db.mutation == 5)
                    db.Clear(Id(7));
            }
            var service = new AvatarServiceReference { m_Database = db };
            string result = "EX";
            try
            {
                switch (mode)
                {
                case 0:
                    if (db.mutation == 7)
                        a.Data = null;
                    result = service.SetAvatar(Id(7), a) ? "1" : "0";
                    break;
                case 1:
                    string[] names = { "first", "AvatarHeight", "nullField" },
                             values = { "value", height, null };
                    if (db.mutation == 8)
                        Array.Resize(ref values, 2);
                    result = service.SetItems(Id(7), db.mutation == 7 ? null : names, values) ? "1" : "0";
                    break;
                case 2:
                    result =
                        service.RemoveItems(Id(7), db.mutation == 7 ? null : new[] { "old", "missing", null })
                            ? "1"
                            : "0";
                    break;
                case 3:
                    result = service.ResetAvatar(Id(7)) ? "1" : "0";
                    break;
                case 4:
                    result = Data(service.GetAvatar(Id(7)));
                    break;
                case 5:
                    result = service.SetAppearance(
                                 Id(7), db.mutation == 7 ? null : new AvatarAppearance { Serial = 12 })
                                 ? "1"
                                 : "0";
                    break;
                case 6:
                    var ap = service.GetAppearance(Id(7));
                    result = ap == null ? "null" : Snapshot(ap);
                    break;
                }
            }
            catch
            {
                result = "EX";
            }
            Console.WriteLine(H(result) + "|" + H(db.trace) + "|" + H(db.Snapshot()) + "|" + H(Data(a)));
        }
    }
}
