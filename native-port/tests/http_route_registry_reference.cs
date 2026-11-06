using System;
using System.Text;
using System.Text.Json;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Linq;
using System.Globalization;
public interface IRequestHandler
{
    string Path { get; }
    string HttpMethod { get; }
}
public interface ISimpleStreamHandler
{
    string Path { get; }
}
public class PollServiceEventArgs
{
    public string id;
    public string Url { get; set; }
}
public class RouteContext
{
    public JsonElement command;
    public string trace = "";
    public int failures;
    public bool mutated;
    public Dictionary<string, RouteHandler> handlers = new();
    public void Log(string op, string label)
    {
        trace += op + ":" + label + ";";
        var failure = command.TryGetProperty("failure", out var f) ? f.GetString() : "";
        if ((failure == op || failure == op + ":" + label) &&
            ++failures == (command.TryGetProperty("fail_nth", out var n) ? n.GetInt32() : 1))
            throw new Exception();
        if (!mutated && command.TryGetProperty("mutation", out var m) && m.GetString() == op + ":" + label)
        {
            mutated = true;
            handlers["A"] = handlers["B"];
        }
    }
}
public class RouteHandler : IRequestHandler, ISimpleStreamHandler
{
    public RouteContext ctx;
    public string id, verb;
    public List<string> paths;
    public int reads;
    public string Path
    {
        get {
            ctx.Log("path", id);
            return paths[Math.Min(reads++, paths.Count - 1)];
        }
    }
    public string HttpMethod
    {
        get {
            ctx.Log("method", id);
            return verb;
        }
    }
}
public partial class RouteReference
{
    ConcurrentDictionary<string, IRequestHandler> m_streamHandlers = new();
    ConcurrentDictionary<string, ISimpleStreamHandler> m_simpleStreamHandlers = new(),
                                                       m_simpleStreamVarPath = new();
    ConcurrentDictionary<string, PollServiceEventArgs> m_pollHandlers = new(), m_pollHandlersVarPath = new();
    IRequestHandler m_RootDefaultGET;
    static string Hex(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Label(object h) => h is RouteHandler handler        ? handler.id
                                     : h is PollServiceEventArgs poll ? poll.id
                                                                      : "-";
    static string Keys(List<string> values) =>
        string.Concat(values.Select(Hex).OrderBy(s => s, StringComparer.Ordinal).Select(s => s + ","));
    static string Opt(JsonElement e) =>
        e.ValueKind == JsonValueKind.Object
            ? Encoding.UTF8.GetString(Convert.FromHexString(e.GetProperty("__route_string").GetString()))
        : e.ValueKind == JsonValueKind.Null ? null
                                            : e.GetString();
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var ctx =
                new RouteContext { command = JsonDocument.Parse(Convert.FromHexString(line)).RootElement };
            var owner = new RouteReference();
            var polls = new Dictionary<string, PollServiceEventArgs>();
            foreach (string name in new[] { "A", "B", "C", "S" })
            {
                var has = ctx.command.TryGetProperty(name, out var spec);
                var paths = new List<string>();
                if (has && spec.TryGetProperty("paths", out var seq))
                    foreach (var item in seq.EnumerateArray())
                        paths.Add(Opt(item));
                if (paths.Count == 0)
                    paths.Add(has && spec.TryGetProperty("path", out var path) ? Opt(path) : "/x");
                ctx.handlers[name] =
                    new RouteHandler { ctx = ctx, id = name, paths = paths,
                                       verb = has && spec.TryGetProperty("method", out var verb) ? Opt(verb)
                                                                                                 : "GET" };
                polls[name] = new PollServiceEventArgs {
                    id = name, Url = has && spec.TryGetProperty("path", out var url) ? Opt(url) : "/x"
                };
            }
            string results = "";
            foreach (var op in ctx.command.GetProperty("ops").EnumerateArray())
            {
                var method = op.GetProperty("op").GetString();
                var h = op.TryGetProperty("h", out var hv)
                            ? (hv.ValueKind == JsonValueKind.Null ? null : ctx.handlers[hv.GetString()])
                            : ctx.handlers["A"];
                var p = op.TryGetProperty("h", out hv)
                            ? (hv.ValueKind == JsonValueKind.Null ? null : polls[hv.GetString()])
                            : polls["A"];
                var path = op.TryGetProperty("path", out var pv) ? Opt(pv) : "/x";
                var verb = op.TryGetProperty("method", out var mv) ? Opt(mv) : "GET";
                string value = "OK";
                IRequestHandler stream = ctx.handlers["S"];
                ISimpleStreamHandler simple = ctx.handlers["S"];
                var poll = polls["S"];
                try
                {
                    switch (method)
                    {
                    case "add":
                        owner.AddStreamHandler(h);
                        break;
                    case "generic":
                        owner.AddGenericStreamHandler(h);
                        break;
                    case "simple":
                        owner.AddSimpleStreamHandler(h, op.TryGetProperty("variable", out var v) &&
                                                            v.GetBoolean());
                        break;
                    case "poll":
                        value = owner.AddPollServiceHTTPHandler(p) ? "1" : "0";
                        break;
                    case "poll_at":
                        value = owner.AddPollServiceHTTPHandler(path, p) ? "1" : "0";
                        break;
                    case "poll_variable":
                        value = owner.AddPollServiceHTTPHandlerVarPath(p) ? "1" : "0";
                        break;
                    case "remove":
                        owner.RemoveStreamHandler(verb, path);
                        break;
                    case "remove_key":
                        owner.RemoveStreamHandler(path);
                        break;
                    case "remove_simple":
                        owner.RemoveSimpleStreamHandler(path);
                        break;
                    case "remove_poll":
                        owner.RemovePollServiceHTTPHandler(path);
                        break;
                    case "remove_poll_method":
                        owner.RemovePollServiceHTTPHandler(verb, path);
                        break;
                    case "get":
                        value = owner.TryGetStreamHandler(path, out stream) ? "1" : "0";
                        break;
                    case "get_simple":
                        value = owner.TryGetSimpleStreamHandler(path, out simple) ? "1" : "0";
                        break;
                    case "get_poll":
                        value = owner.TryGetPollServiceHTTPHandler(path, out poll) ? "1" : "0";
                        break;
                    case "key":
                        value = Hex(GetHandlerKey(verb, path));
                        break;
                    case "mutate_poll":
                        p.Url = path;
                        break;
                    }
                }
                catch
                {
                    value = "EX";
                }
                if (method == "get")
                    value += ":" + Label(stream);
                else if (method == "get_simple")
                    value += ":" + Label(simple);
                else if (method == "get_poll")
                    value += ":" + Label(poll);
                results += value + ",";
            }
            var normal = string.Concat(owner.GetStreamHandlerKeys()
                                           .Select(k => Hex(k) + ":" + Label(owner.m_streamHandlers[k]))
                                           .OrderBy(s => s, StringComparer.Ordinal)
                                           .Select(s => s + ","));
            Console.WriteLine(results + "|" + Hex(ctx.trace) + "|" + normal + "|" +
                              Keys(owner.GetSimpleStreamHandlerKeys()) + "|" +
                              Keys(owner.GetPollServiceHandlerKeys()) + "|" + Label(owner.m_RootDefaultGET));
        }
    }
}
