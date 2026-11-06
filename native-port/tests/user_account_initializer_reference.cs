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

class UserAccountInitializerReference
{
    static FieldInfo Connection => typeof(SQLiteGenericTableHandler<UserAccountData>).GetField("m_Connection", BindingFlags.Static | BindingFlags.NonPublic);
    static FieldInfo Initialized => typeof(SQLiteGenericTableHandler<UserAccountData>).GetField("m_initialized", BindingFlags.Static | BindingFlags.NonPublic);

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

    static string Snapshot(SQLiteConnection db) => Host.Hex(Scalar(db, "SELECT coalesce(group_concat(name||':'||quote(version),'|'),'') FROM (SELECT name,version FROM migrations ORDER BY name,version)")) + ":" + Host.Hex(Scalar(db, "SELECT coalesce(group_concat(PrincipalID||':'||ScopeID||':'||FirstName||':'||LastName,'|'),'') FROM (SELECT * FROM UserAccounts ORDER BY PrincipalID,FirstName)"));
    static string Text(JsonElement x) => x.ValueKind == JsonValueKind.Null ? null : x.GetString() == "." ? "" : x.GetString();
    static string Rows(UserAccountData[] rows)
    {
        if (rows == null)
            return "N";
        string result = rows.Length + "|";
        foreach (var r in rows)
        {
            result += Host.Hex(r.PrincipalID.ToString()) + ":" + Host.Hex(r.ScopeID.ToString()) + ":" + Host.Hex(r.FirstName) + ":" + Host.Hex(r.LastName) + ":";
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
            var slots = new Dictionary<int, SQLiteUserAccountData>();
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
                            slots[slot] = new SQLiteUserAccountData(op.GetProperty("connection").GetString().Replace("$DB", path).Replace("$OTHER", other), op.GetProperty("realm").GetString());
                            break;
                        case "get":
                            var rows = slots[slot].Get("PrincipalID", "00000000-0000-0000-0000-000000000001");
                            result = rows.Length + "|";
                            foreach (var r in rows)
                            {
                                result += Host.Hex(r.PrincipalID.ToString()) + ":" + Host.Hex(r.ScopeID.ToString()) + ":" + Host.Hex(r.FirstName) + ":" + Host.Hex(r.LastName) + ":";
                                foreach (var kv in r.Data)
                                    result += Host.Hex(kv.Key) + "=" + Host.Hex(kv.Value) + ",";
                                result += ";";
                            }

                            break;
                        case "search":
                            result = Rows(slots[slot].GetUsers(new OpenMetaverse.UUID(op.GetProperty("scope").GetString()), Text(op.GetProperty("query"))));
                            break;
                        case "where":
                            result = Rows(slots[slot].GetUsersWhere(new OpenMetaverse.UUID(op.GetProperty("scope").GetString()), Text(op.GetProperty("query"))));
                            break;
                        case "store":
                            result = slots[slot].Store(new UserAccountData { PrincipalID = new OpenMetaverse.UUID("00000000-0000-0000-0000-000000000001"), ScopeID = OpenMetaverse.UUID.Zero, FirstName = "First", LastName = "Last", Data = new Dictionary<string, string> { { "Email", "" }, { "Created", "7" }, { "UserLevel", "0" }, { "UserFlags", "0" }, { "ServiceURLs", "" } } }) ? "1" : "0";
                            break;
                        case "delete":
                            result = slots[slot].Delete("PrincipalID", "00000000-0000-0000-0000-000000000001") ? "1" : "0";
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
