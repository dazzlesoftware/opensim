using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Text;
using System.Text.RegularExpressions;
class NamedCulture : CultureInfo
{
    string name;
    public int Reads;
    public NamedCulture(string n) : base("")
    {
        name = n;
    }
    public override string Name
    {
        get {
            Reads++;
            return name;
        }
    }
}
class Program
{
    static string Dec(string s)
    {
        if (s == ".")
            return "";
        var c = new char[s.Length / 4];
        for (int i = 0; i < c.Length; i++)
            c[i] = (char)Convert.ToUInt16(s.Substring(i * 4, 4), 16);
        return new string(c);
    }
    static string Hex(string s)
    {
        if (s.Length == 0)
            return ".";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static void Main(string[] args)
    {
        var type = typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexCharClass");
        var flags =
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        if (args.Length > 0 && args[0] == "--names")
        {
            var categories = new Dictionary<string, string>();
            foreach (System.Collections.DictionaryEntry item in (System.Collections.IDictionary)type
                         .GetField("s_definedCategories", flags)
                         .GetValue(null))
                categories.Add((string)item.Key, Hex((string)item.Value));
            var properties = new Dictionary<string, string>();
            foreach (var pair in (string[][])type.GetField("s_propTable", flags).GetValue(null))
                properties.Add(pair[0], Hex(pair[1]));
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(new { categories, properties }));
            return;
        }
        if (args.Length > 0 && args[0] == "--classes")
        {
            var parser = typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexParser");
            var method =
                parser.GetMethod("Parse", flags, null,
                                 new[] { typeof(string), typeof(RegexOptions), typeof(CultureInfo) }, null);
            var parse = (Func<string, RegexOptions, CultureInfo, object>)method.CreateDelegate(
                typeof(Func<string, RegexOptions, CultureInfo, object>));
            var classes = new HashSet<string>();
            void Visit(object node)
            {
                var t = node.GetType();
                if (t.GetProperty("Kind").GetValue(node).ToString().StartsWith("Set"))
                    classes.Add(Hex((string)t.GetProperty("Str").GetValue(node)));
                int count = (int)t.GetMethod("ChildCount").Invoke(node, null);
                for (int i = 0; i < count; i++)
                    Visit(t.GetMethod("Child").Invoke(node, new object[] { i }));
            }
            var patterns = new List<string> { @"[a-z]",
                                              @"[a-z-[aeiou]]",
                                              @"[^a-z-[A-Z]]",
                                              @"\w",
                                              @"\W",
                                              @"\d",
                                              @"\D",
                                              @"\s",
                                              @"\S",
                                              @"[\p{Lu}\p{Ll}]",
                                              @"[\p{L}-[A-Z]]",
                                              @"[^\p{L}-[\p{N}]]",
                                              @"[\p{IsBasicLatin}\p{IsGreek}]",
                                              @"[\uD800-\uDFFF]",
                                              @"[\x00-\uffff]",
                                              @"[a-z-[d-f-[e]]]",
                                              @"[^\p{M}\p{Zs}]" };
            foreach (System.Collections.DictionaryEntry pair in (System.Collections.IDictionary)type
                         .GetField("s_definedCategories", flags)
                         .GetValue(null))
                if ((string)pair.Key != "__InternalRegexIgnoreCase__")
                    patterns.Add(@"\p{" + (string)pair.Key + "}");
            foreach (string pattern in patterns)
                foreach (int option in new[] { 0, 1, 256, 257 })
                {
                    var tree = parse(pattern, (RegexOptions)option, CultureInfo.InvariantCulture);
                    Visit(tree.GetType().GetField("Root").GetValue(tree));
                }
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(classes));
            return;
        }
        object Call(object target, string name, params object[] args) =>
            type.GetMethod(name, BindingFlags.Public | BindingFlags.Instance | BindingFlags.Static)
                .Invoke(target, args);
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            var objects = new Dictionary<int, object>();
            var output = new List<string>();
            foreach (string command in row.Split(';'))
            {
                try
                {
                    var f = command.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                    string op = f[0];
                    if (op == "C")
                    {
                        output.Add("C:" +
                                   Hex((string)Call(null, "ConvertOldStringsToClass", Dec(f[1]), Dec(f[2]))));
                        continue;
                    }
                    int id = int.Parse(f[1]);
                    string result = op;
                    switch (op)
                    {
                    case "N":
                        objects[id] = Activator.CreateInstance(type);
                        break;
                    case "P":
                        objects[id] = Call(null, "Parse", Dec(f[2]));
                        break;
                    case "O":
                        string text = (string)Call(objects[id], "ToStringClass");
                        result = "O:" + Hex(text) + ":" +
                                 ((bool)type.GetProperty("CanMerge").GetValue(objects[id]) ? 1 : 0) + ":" +
                                 (type.GetField("_rangelist", flags).GetValue(objects[id]) != null ? 1 : 0) +
                                 ":" +
                                 (type.GetField("_categories", flags).GetValue(objects[id]) != null ? 1 : 0) +
                                 ":" +
                                 Convert.ToInt32(type.GetField("_caseBehavior", flags).GetValue(objects[id]));
                        break;
                    case "R":
                        Call(objects[id], "AddRange", (char)int.Parse(f[2]), (char)int.Parse(f[3]));
                        break;
                    case "A":
                        Call(objects[id], "AddChar", (char)int.Parse(f[2]));
                        break;
                    case "V":
                        type.GetProperty("Negate").SetValue(objects[id], f[2] != "0");
                        break;
                    case "X":
                        Call(objects[id], "AddSubtraction",
                             int.Parse(f[2]) < 0 ? null : objects[int.Parse(f[2])]);
                        break;
                    case "M":
                        if (f[3] == "1")
                            result =
                                "M:" + ((bool)Call(objects[id], "TryAddCharClass", objects[int.Parse(f[2])])
                                            ? 1
                                            : 0);
                        else
                            Call(objects[id], "AddCharClass", objects[int.Parse(f[2])]);
                        break;
                    case "G":
                        Call(objects[id], "AddCategoryFromName", Dec(f[2]), f[3] != "0", f[4] != "0",
                             Dec(f[5]), int.Parse(f[6]));
                        break;
                    case "B":
                        for (int i = 0; i < int.Parse(f[3]); i++)
                            Call(objects[id], "AddCategoryFromName", Dec(f[2]), false, false, "", 0);
                        break;
                    case "U":
                        for (int i = 0; i < 65536; i += 2)
                            Call(objects[id], "AddChar", (char)i);
                        break;
                    case "E":
                        var culture = new NamedCulture(Dec(f[2]));
                        Call(objects[id], "AddCaseEquivalences", culture);
                        result = "E:" + culture.Reads;
                        break;
                    case "W":
                        Call(objects[id], "AddWord", f[2] != "0", f[3] != "0");
                        break;
                    case "S":
                        Call(objects[id], "AddSpace", f[2] != "0", f[3] != "0");
                        break;
                    case "D":
                        Call(objects[id], "AddDigit", f[2] != "0", f[3] != "0", "", 0);
                        break;
                    }
                    output.Add(result);
                }
                catch (Exception e)
                {
                    if (e is TargetInvocationException && e.InnerException != null)
                        e = e.InnerException;
                    output.Add("E:" + e.GetType().FullName + ":" + unchecked((uint)e.HResult).ToString("x8") +
                               ":" + Hex(e.Message) + ":" +
                               (e is RegexParseException r ? ((int)r.Error).ToString() : "-") + ":" +
                               (e is RegexParseException r2 ? r2.Offset.ToString() : "-"));
                }
            }
            Console.WriteLine(string.Join("|", output));
        }
    }
}
