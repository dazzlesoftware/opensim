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
using OpenMetaverse;
using OpenSim.Data;
using OpenSim.Data.SQLite;

namespace OpenSim.Framework
{
    public static class DllmapConfigHelper
    {
        public static void RegisterAssembly(Assembly a)
        {
        }
    }
}

public static class AuthFixture
{
    public static int Tick;
}

class AuthenticationInitializerReference
{
    static FieldInfo Connection => typeof(SQLiteAuthenticationData).GetField("m_Connection", BindingFlags.Static | BindingFlags.NonPublic);
    static FieldInfo Initialized => typeof(SQLiteAuthenticationData).GetField("m_initialized", BindingFlags.Static | BindingFlags.NonPublic);

    static string Text(JsonElement v) => v.ValueKind == JsonValueKind.Null ? null : v.GetString() == "." ? "" : v.GetString();
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

    static string Snapshot(SQLiteConnection db) => Host.Hex(Scalar(db, "SELECT coalesce(group_concat(name||':'||quote(version),'|'),'') FROM (SELECT name,version FROM migrations ORDER BY name,version)")) + ":" + Host.Hex(Scalar(db, "SELECT coalesce(group_concat(UUID||':'||quote(passwordHash)||':'||quote(passwordSalt)||':'||quote(webLoginKey),'|'),'') FROM (SELECT * FROM auth ORDER BY UUID)")) + ":" + Host.Hex(Scalar(db, "SELECT coalesce(group_concat(UUID||':'||quote(token)||':'||CASE WHEN validity>datetime('now','localtime') THEN 'future' ELSE 'past' END,'|'),'') FROM (SELECT * FROM tokens ORDER BY rowid)"));
    static string Row(AuthenticationData r)
    {
        if (r == null)
            return "N";
        string result = r.PrincipalID + ":";
        foreach (var kv in r.Data)
            result += Host.Hex(kv.Key) + "=" + Host.Hex(Convert.ToString(kv.Value, CultureInfo.InvariantCulture)) + ",";
        return result;
    }

    static object Value(JsonElement x)
    {
        string kind = x.GetProperty("kind").GetString();
        var v = x.GetProperty("value");
        return kind switch
        {
            "null" => null,
            "int" => v.GetInt32(),
            "bool" => v.GetBoolean(),
            "double" => v.GetDouble(),
            "blob" => Convert.FromHexString(v.GetString()),
            _ => Text(v)};
    }

    static string Shape(AuthenticationData r)
    {
        if (r == null)
            return "N";
        if (r.Data == null)
            return "NULLDATA";
        string result = "";
        foreach (var kv in r.Data)
            result += Host.Hex(kv.Key) + "=" + (kv.Value == null ? "N" : kv.Value.GetType().Name) + ",";
        return result;
    }

    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        int index = 0;
        var stdout = Console.Out;
        while ((line = Console.ReadLine()) != null)
        {
            Host.Input = JsonDocument.Parse(Encoding.UTF8.GetString(Convert.FromHexString(line))).RootElement;
            Host.Trace = "";
            var input = Host.Input;
            string path = Path.Combine(Path.GetFullPath(args[0]), (index++).ToString() + ".db");
            using var db = new SQLiteConnection("Data Source=" + path + ";Pooling=False");
            db.Open();
            Exec(db, input.GetProperty("setup").GetString());
            Connection.SetValue(null, null);
            Initialized.SetValue(null, false);
            var slots = new Dictionary<int, SQLiteAuthenticationData>();
            var connections = new HashSet<SQLiteConnection>();
            string output = "";
            var id = new UUID("00000000-0000-0000-0000-000000000001");
            foreach (var op in input.GetProperty("ops").EnumerateArray())
            {
                string result = "-", shape = "-";
                int slot = op.TryGetProperty("slot", out var s) ? s.GetInt32() : 0;
                AuthFixture.Tick = op.TryGetProperty("tick", out var t) ? t.GetInt32() : 0;
                var capture = new StringWriter();
                Console.SetOut(capture);
                try
                {
                    switch (op.GetProperty("op").GetString())
                    {
                        case "new":
                            slots[slot] = new SQLiteAuthenticationData(op.GetProperty("connection").GetString().Replace("$DB", path), Text(op.GetProperty("realm")));
                            break;
                        case "get":
                            result = Row(slots[slot].Get(id));
                            break;
                        case "gc":
                            GC.Collect();
                            GC.WaitForPendingFinalizers();
                            GC.Collect();
                            break;
                        case "store":
                            if (!slots.ContainsKey(slot))
                                throw new Exception("Missing slot");
                            AuthenticationData row = null;
                            if (!op.GetProperty("nullrow").GetBoolean())
                            {
                                row = new AuthenticationData
                                {
                                    PrincipalID = id
                                };
                                var values = op.GetProperty("data");
                                if (values.ValueKind != JsonValueKind.Null)
                                {
                                    row.Data = new Dictionary<string, object>();
                                    foreach (var p in values.EnumerateArray())
                                        row.Data[Text(p[0])] = Value(p[1]);
                                }
                            }

                            try
                            {
                                result = slots[slot].Store(row) ? "1" : "0";
                            }
                            finally
                            {
                                shape = Shape(row);
                            }

                            break;
                        case "item":
                            result = slots[slot].SetDataItem(id, Text(op.GetProperty("item")), Text(op.GetProperty("value"))) ? "1" : "0";
                            break;
                        case "set":
                            result = slots[slot].SetToken(id, Text(op.GetProperty("token")), op.GetProperty("lifetime").GetInt32()) ? "1" : "0";
                            break;
                        case "check":
                            result = slots[slot].CheckToken(id, Text(op.GetProperty("token")), op.GetProperty("lifetime").GetInt32()) ? "1" : "0";
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
                finally
                {
                    Console.SetOut(stdout);
                }

                if (capture.ToString().Length > 0)
                    Host.Event("console");
                var conn = (SQLiteConnection)Connection.GetValue(null);
                if (conn != null)
                    connections.Add(conn);
                string last = slots.TryGetValue(slot, out var provider) ? Convert.ToString(typeof(SQLiteAuthenticationData).GetField("m_LastExpire", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(provider), CultureInfo.InvariantCulture) : "-";
                output += Host.Hex(result) + ":" + Host.Hex(shape) + ":" + last + ":" + ((bool)Initialized.GetValue(null) ? "1" : "0") + ":" + Snapshot(db) + ";";
            }

            foreach (var c in connections)
                c.Close();
            stdout.WriteLine(output + "|" + Host.Hex(Host.Trace));
        }
    }
}
