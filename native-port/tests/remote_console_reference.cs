using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Xml;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Nini.Config;
using OpenMetaverse;
using System.Timers;
using log4net;
using System.Reflection;
namespace System
{
    public static class Environment
    {
        public static int TickCount;
    }
}
namespace log4net
{
    public interface ILog
    {
    }
    public class Log : ILog
    {
    }
    public static class LogManager
    {
        public static ILog GetLogger(object value) => new Log();
    }
}
public interface ICommands
{
}
public delegate void CommandDelegate(string module, string[] words);
PARSER
COMMANDS
public class ConsoleLevel
{
    public override string ToString() => "INFO";
}
public class CommandConsole
{
    public Commands Commands = new();
    public CommandConsole(string prompt)
    {
        Commands.AddCommand("Help", false, "help", "help", "Help", (module, words) => Trace(module, words));
        Commands.AddCommand("General", false, "quit", "quit", "Quit",
                            (module, words) => Trace(module, words));
    }
    public static List<string> Logs = new();
    protected void FireOnOutput(string value) => Logs.Add("out:" + value);
    static void Trace(string module,
                      string[] words) => Logs.Add("cmd:" + module +
                                                  string.Concat(words.Select(word => "|" + word)));
    public virtual void ReadConfig(IConfigSource config)
    {
    }
    public virtual void Output(string format)
    {
    }
    public virtual void Output(string format, params object[] components)
    {
    }
    public virtual string ReadLine(string prompt, bool command, bool echo) => "";
}
public static class MainConsole
{
    public static CommandConsole Instance;
}
public class PollServiceEventArgs
{
    public string Url;
    public UUID Id;
    public int Timeout;
    public object Request, Drop;
    public Func<UUID, UUID, bool> Has;
    public Func<UUID, UUID, Hashtable> Get, No;
    public PollServiceEventArgs(object request, string url, Func<UUID, UUID, bool> has,
                                Func<UUID, UUID, Hashtable> get, Func<UUID, UUID, Hashtable> no, object drop,
                                UUID id, int timeout)
    {
        Request = request;
        Url = url;
        Has = has;
        Get = get;
        No = no;
        Drop = drop;
        Id = id;
        Timeout = timeout;
    }
}
public interface IHttpServer
{
    void AddHTTPHandler(string path, Func<Hashtable, Hashtable> callback);
    bool AddPollServiceHTTPHandler(PollServiceEventArgs args);
    void RemovePollServiceHTTPHandler(string method, string path);
}
public class Server : IHttpServer
{
    public List<UUID> Ids = new();
    public void AddHTTPHandler(string path, Func<Hashtable, Hashtable> callback)
    {
    }
    public bool AddPollServiceHTTPHandler(PollServiceEventArgs value)
    {
        Ids.Add(value.Id);
        CommandConsole.Logs.Add("add:" + value.Url + ":" + value.Timeout + ":" +
                                (value.Request == null ? "0" : "1") + ":" + (value.Drop == null ? "0" : "1"));
        return true;
    }
    public void RemovePollServiceHTTPHandler(string method,
                                             string path) => CommandConsole.Logs.Add("remove:" + method +
                                                                                     ":" + path);
}
REMOTE
public class Reference : RemoteConsole, IDisposable
{
    public Reference() : base("Region")
    {
        m_expireTimer.Stop();
        m_expireTimer.Dispose();
        Commands.AddCommand("Test", false, "echo", "echo <text>", "Echo",
                            (module, words) =>
                            {
                                Logs.Add("cmd:" + module + string.Concat(words.Select(word => "|" + word)));
                                Output("ECHO:" + string.Join(" ", words.Skip(1)));
                            });
    }
    public Hashtable Start(string body) => HandleHttpStartSession(new Hashtable { { "body", body } });
    public Hashtable Close(string body) => HandleHttpCloseSession(new Hashtable { { "body", body } });
    public Hashtable Input(string body) => HandleHttpSessionCommand(new Hashtable { { "body", body } });
    public void Emit(string text, string level, int flags) => Output(text, level, (flags & 1) != 0,
                                                                     (flags & 2) != 0, (flags & 4) != 0);
    public bool Has(UUID id) => HasEvents(UUID.Zero, id);
    public Hashtable Get(UUID id) => GetEvents(UUID.Zero, id);
    public Hashtable No() => NoEvents(UUID.Zero, UUID.Zero);
    public void Expire() => DoExpire(null, null);
    public Hashtable Decode(string text) => DecodePostString(text);
    public string State() => m_lineNumber + "," + m_Scrollback.Count + "," + m_InputData.Count +
                             "," + m_Connections.Count + "," + (m_expectingInput ? "1" : "0") + "," +
                             (m_expectingCommand ? "1" : "0") + "," + Program.Hex(m_lastPromptUsed);
    public int Pending => m_InputData.Count;
    public void Dispose()
    {
        m_DataEvent.Dispose();
    }
}
public class Capture : TextWriter
{
    public override Encoding Encoding => Encoding.UTF8;
    public override void WriteLine(string value) => CommandConsole.Logs.Add("debug:" + value);
}
public class Program
{
    public static string Hex(string value) => string.Concat(value.Select(c => ((int)c).ToString("x4")));
    static string Decode(string value) =>
        new string(Enumerable.Range(0, value.Length / 4)
                       .Select(i => (char)Convert.ToUInt16(value.Substring(i * 4, 4), 16))
                       .ToArray());
    static string Normalize(string value, Server server)
    {
        for (int i = 0; i < server.Ids.Count; i++)
            value = value.Replace(server.Ids[i].ToString(), "@" + i);
        return value;
    }
    static string Expand(string value, Server server)
    {
        for (int i = server.Ids.Count - 1; i >= 0; i--)
            value = value.Replace("@" + i, server.Ids[i].ToString());
        return value;
    }
    static UUID Selected(string value, Server server)
    {
        int i = int.Parse(value);
        return i >= 0 && i < server.Ids.Count ? server.Ids[i] : UUID.Zero;
    }
    static string Reply(Hashtable value, Server server) =>
        value["int_response_code"] + "," + Hex((string)value["content_type"]) + "," +
        (value.ContainsKey("keepalive") ? ((bool)value["keepalive"] ? "1" : "0") : "-") + "," +
        (value.ContainsKey("access_control_allow_origin") ? Hex((string)value["access_control_allow_origin"])
                                                          : "-") +
        "," + Hex(Normalize((string)value["str_response_string"], server));
    static void Config(Reference console, string user, string password, string origin)
    {
        var config = new IniConfigSource();
        var section = config.AddConfig("Network");
        section.Set("ConsoleUser", user);
        section.Set("ConsolePass", password);
        section.Set("ConsoleAllowedOrigin", origin);
        console.ReadConfig(config);
    }
    static void Main()
    {
        Thread.CurrentThread.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        var stdout = Console.Out;
        Console.SetOut(new Capture());
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            using (var console = new Reference())
            {
                System.Environment.TickCount = 0;
                var server = new Server();
                console.SetServer(server);
                MainConsole.Instance = console;
                Config(console, "user", "pass", "");
                bool first = true;
                foreach (var action in row.Split(';'))
                {
                    var fields = action.Split(',');
                    CommandConsole.Logs.Clear();
                    string result;
                    try
                    {
                        switch (fields[0])
                        {
                        case "S":
                            result = Reply(console.Start(Expand(Decode(fields[1]), server)), server);
                            break;
                        case "C":
                            result = Reply(console.Close(Expand(Decode(fields[1]), server)), server);
                            break;
                        case "I":
                            result = Reply(console.Input(Expand(Decode(fields[1]), server)), server);
                            break;
                        case "O":
                            console.Emit(fields[1] == "-" ? null : Decode(fields[1]),
                                         fields[2] == "-" ? null : Decode(fields[2]), int.Parse(fields[3]));
                            result = "O";
                            break;
                        case "T":
                            System.Environment.TickCount = int.Parse(fields[1]);
                            result = "T";
                            break;
                        case "H":
                            result = console.Has(Selected(fields[1], server)) ? "1" : "0";
                            break;
                        case "G":
                            result = Reply(console.Get(Selected(fields[1], server)), server);
                            break;
                        case "N":
                            result = Reply(console.No(), server);
                            break;
                        case "E":
                            console.Expire();
                            result = "E";
                            break;
                        case "Z":
                            console.CloseConnection(Selected(fields[1], server));
                            result = "Z";
                            break;
                        case "A":
                            Config(console, Decode(fields[1]), Decode(fields[2]), Decode(fields[3]));
                            result = "A";
                            break;
                        case "M":
                            console.ReadConfig(new IniConfigSource());
                            result = "M";
                            break;
                        case "L":
                            result = console.Pending > 0
                                         ? "L:" + Hex(console.ReadLine(Decode(fields[1]), fields[2] != "0",
                                                                       fields[3] != "0"))
                                         : "WAIT";
                            break;
                        case "W":
                            for (int i = 0; i < int.Parse(fields[1]); i++)
                                console.Output("line " + i);
                            result = "W";
                            break;
                        case "D":
                            var table = console.Decode(Decode(fields[1]));
                            result = "D";
                            foreach (string key in table.Keys.Cast<string>().OrderBy(x => x,
                                                   StringComparer.Ordinal))
                                result += "," + Hex(key) + ":" + Hex((string)table[key]);
                            break;
                        default:
                            throw new Exception();
                        }
                    }
                    catch (Exception)
                    {
                        result = "EX";
                    }
                    if (!first)
                        stdout.Write("~");
                    first = false;
                    stdout.Write(result + "!" + console.State());
                    foreach (var log in CommandConsole.Logs)
                        stdout.Write("!" + Hex(Normalize(log, server)));
                }
                stdout.WriteLine();
            }
        }
    }
}
