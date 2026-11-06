// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Text.Json;
using OpenSim.Data;
namespace log4net
{
    public interface ILog
    {
        void WarnFormat(string f, params object[] args);
        void InfoFormat(string f, params object[] args);
        void Info(object value);
        void DebugFormat(string f, params object[] args);
        void Debug(object value);
        void ErrorFormat(string f, params object[] args);
        void Error(object value, Exception error);
    }
    public class Log : ILog
    {
        public void WarnFormat(string f,
                               params object[] args) => Host.Event("warn:" + string.Format(f, args));
        public void InfoFormat(string f,
                               params object[] args) => Host.Event("info:" + string.Format(f, args));
        public void Info(object v) => Host.Event("info:" + v);
        public void DebugFormat(string f, params object[] args) => Host.Debug(string.Format(f, args));
        public void Debug(object v) => Host.Debug(Convert.ToString(v));
        public void ErrorFormat(string f,
                                params object[] args) => Host.Event("error:" + string.Format(f, args));
        public void Error(object value, Exception error) => Host.Event("error:" + value);
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type t) => new Log();
    }
}
public static class Host
{
    public static JsonElement Input;
    public static string Trace;
    public static void Event(string text)
    {
        Trace += text + ";";
        if (Input.TryGetProperty("fail", out var f) && f.GetString() == text)
            throw new Exception("injected");
    }
    public static void Debug(string value)
    {
        const string prefix = "[MIGRATIONS]: Cmd was ";
        if (value.StartsWith(prefix))
        {
            int at = value.IndexOf(" in SQL: ");
            if (at >= 0)
                value = prefix + "SQLERROR" + value.Substring(at);
        }
        Event("debug:" + value);
    }
    public static string Hex(string text) =>
        Convert.ToHexString(Encoding.UTF8.GetBytes(text)).ToLowerInvariant();
}
class Resources : Assembly
{
    public override string[] GetManifestResourceNames()
    {
        Host.Event("names");
        var names = new List<string>();
        foreach (var r in Host.Input.GetProperty("resources").EnumerateArray())
            names.Add(r.GetProperty("name").GetString());
        return names.ToArray();
    }
    public override Stream GetManifestResourceStream(string name)
    {
        Host.Event("read:" + name);
        foreach (var r in Host.Input.GetProperty("resources").EnumerateArray())
            if (r.GetProperty("name").GetString() == name)
                return r.GetProperty("data").ValueKind == JsonValueKind.Null
                           ? null
                           : new MemoryStream(r.GetProperty("data").GetString() == "."
                                                  ? Array.Empty<byte>()
                                                  : Convert.FromHexString(r.GetProperty("data").GetString()));
        return null;
    }
}
class MigrationParserReference
{
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        var parse =
            typeof(Migration).GetMethod("GetMigrationsAfter", BindingFlags.NonPublic | BindingFlags.Instance);
        while ((line = Console.ReadLine()) != null)
        {
            Host.Input = JsonDocument.Parse(Encoding.UTF8.GetString(Convert.FromHexString(line))).RootElement;
            Host.Trace = "";
            string result = "OK";
            try
            {
                var input = Host.Input;
                var migration = new Migration(null, new Resources(), input.GetProperty("sub").GetString(),
                                              input.GetProperty("type").GetString());
                var rows = (SortedList<int, string[]>)parse.Invoke(
                    migration, new object[] { input.GetProperty("after").GetInt32() });
                result += "|";
                foreach (var row in rows)
                {
                    result += row.Key + "=";
                    foreach (var sql in row.Value)
                        result += Host.Hex(sql) + ",";
                    result += ";";
                }
            }
            catch
            {
                result = "EX";
            }
            Console.WriteLine(result + "|" + Host.Hex(Host.Trace));
        }
    }
}
