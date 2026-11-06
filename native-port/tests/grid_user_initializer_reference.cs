// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using System.Data.SQLite;
using OpenSim.Data;
using OpenSim.Data.SQLite;

namespace OpenSim.Region.Framework.Interfaces
{
    public class Placeholder
    {
    }
}

namespace OpenSim.Framework
{
    public static class DllmapConfigHelper
    {
        public static void RegisterAssembly(Assembly a)
        {
        }
    }
}

class GridUserInitializerReference
{
    static FieldInfo Connection => typeof(SQLiteGenericTableHandler<GridUserData>).GetField("m_Connection", BindingFlags.Static | BindingFlags.NonPublic);
    static FieldInfo Initialized => typeof(SQLiteGenericTableHandler<GridUserData>).GetField("m_initialized", BindingFlags.Static | BindingFlags.NonPublic);

    static void Exec(SQLiteConnection db, string sql)
    {
        using var c = db.CreateCommand();
        c.CommandText = sql;
        c.ExecuteNonQuery();
    }

    static string Scalar(SQLiteConnection db, string sql)
    {
        try
        {
            using var c = db.CreateCommand();
            c.CommandText = sql;
            return Convert.ToString(c.ExecuteScalar(), CultureInfo.InvariantCulture);
        }
        catch
        {
            return "X";
        }
    }

    static string Snapshot(SQLiteConnection db) => Host.Hex(Scalar(db, "SELECT coalesce(group_concat(name||':'||quote(version),'|'),'') FROM (SELECT name,version FROM migrations ORDER BY name,version)")) + ":" + Host.Hex(Scalar(db, "SELECT coalesce(group_concat(UserID||':'||Online||':'||Login,'|'),'') FROM (SELECT * FROM GridUser ORDER BY UserID)"));
    static string Text(JsonElement x) => x.ValueKind == JsonValueKind.Null ? null : x.GetString() == "." ? "" : x.GetString();
    static string[] Strings(JsonElement x)
    {
        if (x.ValueKind == JsonValueKind.Null)
            return null;
        var rows = new List<string>();
        foreach (var v in x.EnumerateArray())
            rows.Add(Text(v));
        return rows.ToArray();
    }

    static Dictionary<string, string> Fields(JsonElement x)
    {
        if (x.ValueKind == JsonValueKind.Null)
            return null;
        var d = new Dictionary<string, string>();
        foreach (var pair in x.EnumerateArray())
            d[Text(pair[0])] = Text(pair[1]);
        return d;
    }

    static string Rows(GridUserData[] rows)
    {
        string result = rows.Length + "|";
        foreach (var r in rows)
        {
            result += Host.Hex(r.UserID) + ":";
            foreach (var kv in r.Data)
                result += Host.Hex(kv.Key) + "=" + Host.Hex(kv.Value) + ",";
            result += ";";
        }

        return result;
    }

    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        int index = 0;
        while ((line = Console.ReadLine()) != null)
        {
            Host.Input = JsonDocument.Parse(Encoding.UTF8.GetString(Convert.FromHexString(line))).RootElement;
            Host.Trace = "";
            var input = Host.Input;
            var path = Path.Combine(Path.GetFullPath(args[0]), (index++).ToString() + ".db");
            var other = path + ".other";
            using var db = new SQLiteConnection("Data Source=" + path + ";Pooling=False");
            using var db2 = new SQLiteConnection("Data Source=" + other + ";Pooling=False");
            db.Open();
            db2.Open();
            Exec(db, input.GetProperty("setup").GetString());
            Connection.SetValue(null, null);
            Initialized.SetValue(null, false);
            var slots = new Dictionary<int, SQLiteGridUserData>();
            var connections = new HashSet<SQLiteConnection>();
            string output = "";
            foreach (var op in input.GetProperty("ops").EnumerateArray())
            {
                string result = "-";
                try
                {
                    int slot = op.TryGetProperty("slot", out var s) ? s.GetInt32() : 0;
                    switch (op.GetProperty("op").GetString())
                    {
                        case "new":
                            slots[slot] = new SQLiteGridUserData(op.GetProperty("connection").GetString().Replace("$DB", path).Replace("$OTHER", other), op.GetProperty("realm").GetString());
                            break;
                        case "get":
                            var rows = slots[slot].GetAll("a");
                            result = rows.Length + "|";
                            foreach (var r in rows)
                            {
                                result += Host.Hex(r.UserID) + ":";
                                foreach (var kv in r.Data)
                                    result += Host.Hex(kv.Key) + "=" + Host.Hex(kv.Value) + ",";
                                result += ";";
                            }

                            break;
                        case "exact":
                            var found = slots[slot].Get(Text(op.GetProperty("user")));
                            result = Rows(found == null ? Array.Empty<GridUserData>() : new[] { found });
                            break;
                        case "all":
                            result = Rows(slots[slot].GetAll(Text(op.GetProperty("user"))));
                            break;
                        case "where":
                            result = Rows(((SQLiteGenericTableHandler<GridUserData>)slots[slot]).Get(Text(op.GetProperty("where"))));
                            break;
                        case "fields":
                            result = Rows(slots[slot].Get(Strings(op.GetProperty("fields")), Strings(op.GetProperty("keys"))));
                            break;
                        case "save":
                            result = slots[slot].Store(op.GetProperty("nullrow").GetBoolean() ? null : new GridUserData { UserID = Text(op.GetProperty("user")), Data = Fields(op.GetProperty("data")) }) ? "1" : "0";
                            break;
                        case "dto":
                            var a = new GridUserData();
                            result = (a.UserID == null ? "1" : "0") + ":" + a.Data.Count;
                            var b = new GridUserData
                            {
                                Data = a.Data
                            };
                            b.Data["A"] = null;
                            result += a.Data["A"] == null ? ":1" : ":0";
                            a.Data["B"] = "two";
                            b.Data.Remove("A");
                            b.Data["C"] = "three";
                            result += ":" + (ReferenceEquals(a.Data, b.Data) ? "1" : "0");
                            foreach (var kv in a.Data)
                                result += ":" + kv.Key + "=" + kv.Value;
                            break;
                        case "store":
                            result = slots[slot].Store(new GridUserData { UserID = "a", Data = new Dictionary<string, string> { { "Online", "True" }, { "Login", "7" } } }) ? "1" : "0";
                            break;
                        case "delete":
                            result = slots[slot].Delete("UserID", "a") ? "1" : "0";
                            break;
                        case "raw":
                            Exec(db, op.GetProperty("sql").GetString());
                            break;
                    }
                }
                catch
                {
                    result = "EX";
                }

                var conn = (SQLiteConnection)Connection.GetValue(null);
                if (conn != null)
                    connections.Add(conn);
                output += Host.Hex(result) + ":" + ((bool)Initialized.GetValue(null) ? "1" : "0") + ":" + Snapshot(db) + ":" + Snapshot(db2) + ";";
            }

            foreach (var c in connections)
                c.Close();
            Console.WriteLine(output + "|" + Host.Hex(Host.Trace));
        }
    }
}
