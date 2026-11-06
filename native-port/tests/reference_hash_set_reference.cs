using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
class Actor
{
    public int Id, Serial;
}
class Program
{
    static Dictionary<int, Actor> actors;
    static Dictionary<int, int> serials;
    static HashSet<Actor> set;
    static HashSet<Actor>.Enumerator?[] e;
    static string trace;
    static Actor Get(int id, bool replace = false)
    {
        if (id == 0)
            return null;
        if (!replace && actors.TryGetValue(id, out var actor))
            return actor;
        int n = serials.TryGetValue(id, out var count) ? count + 1 : 1;
        serials[id] = n;
        var value = new Actor { Id = id, Serial = n };
        actors[id] = value;
        return value;
    }
    static string Name(Actor value) => value == null ? "0" : value.Id + "." + value.Serial;
    static string State()
    {
        var rows = new List<string>();
        if (set != null)
            foreach (var actor in set)
                rows.Add(Name(actor));
        return (set == null ? "N" : set.Count + ":" + string.Join(',', rows)) + ";" +
               (e[0].HasValue ? Name(e[0].Value.Current) : "N") + ";" +
               (e[1].HasValue ? Name(e[1].Value.Current) : "N");
    }
    static string H(string s) => Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static void Main(string[] args)
    {
        if (args.Length > 0)
        {
            var assembly = typeof(HashSet<Actor>).Assembly.Location;
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(
                new { runtime = Environment.Version.ToString(), assembly,
                      sha256 = Convert
                                   .ToHexString(System.Security.Cryptography.SHA256.HashData(
                                       System.IO.File.ReadAllBytes(assembly)))
                                   .ToLowerInvariant() }));
            return;
        }
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            set = new();
            actors = new();
            serials = new();
            e = new HashSet<Actor>.Enumerator?[2];
            trace = "";
            var output = new List<string>();
            foreach (var token in line.Split(' '))
            {
                string result = "OK";
                try
                {
                    var p = token.Split(':');
                    int n = p.Length > 1 && p[0] != "F" ? int.Parse(p[1]) : 0;
                    switch (p[0])
                    {
                    case "A":
                        result = set.Add(Get(n)) ? "1" : "0";
                        break;
                    case "R":
                        result = set.Remove(Get(n)) ? "1" : "0";
                        break;
                    case "H":
                        result = set.Contains(Get(n)) ? "1" : "0";
                        break;
                    case "X":
                        Get(n, true);
                        break;
                    case "C":
                        set.Clear();
                        break;
                    case "N":
                        set = null;
                        break;
                    case "S":
                        set = new();
                        break;
                    case "E":
                        e[n] = set.GetEnumerator();
                        break;
                    case "M":
                        var move = e[n].Value;
                        result = move.MoveNext() ? "1" : "0";
                        e[n] = move;
                        break;
                    case "G":
                        result = Name(e[n].Value.Current);
                        break;
                    case "B":
                        result = Name((Actor)((IEnumerator)e[n].Value).Current);
                        break;
                    case "Z":
                        IEnumerator reset = e[n].Value;
                        reset.Reset();
                        e[n] = (HashSet<Actor>.Enumerator)reset;
                        break;
                    case "D":
                        var disposed = e[n].Value;
                        disposed.Dispose();
                        e[n] = disposed;
                        break;
                    case "K":
                        e[n] = e[1 - n];
                        break;
                    case "F":
                        string action = p[1];
                        int id = int.Parse(p[2]);
                        foreach (var actor in set)
                        {
                            trace += "walk:" + Name(actor) + ";";
                            switch (action)
                            {
                            case "A":
                                set.Add(Get(id));
                                break;
                            case "R":
                                set.Remove(Get(id));
                                break;
                            case "V":
                                set.Remove(actor);
                                break;
                            case "C":
                                set.Clear();
                                break;
                            case "M":
                                set.Clear();
                                set.Add(Get(id));
                                break;
                            case "S":
                                set = new();
                                break;
                            }
                        }
                        break;
                    }
                }
                catch (Exception error)
                {
                    result = "EX:" + H(error.Message);
                }
                output.Add(result + ":" + State());
            }
            Console.WriteLine(string.Join('/', output) + "|" + H(trace));
        }
    }
}
