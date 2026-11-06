using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Reflection.Emit;
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
    delegate object Case(char c, RegexOptions options, CultureInfo culture, ref int behavior);
    static string Dec(string s)
    {
        if (s == "-")
            return null;
        if (s == ".")
            return "";
        var c = new char[s.Length / 4];
        for (int i = 0; i < c.Length; i++)
            c[i] = (char)Convert.ToUInt16(s.Substring(i * 4, 4), 16);
        return new string(c);
    }
    static string Hex(string s)
    {
        if (s == null)
            return "-";
        if (s.Length == 0)
            return ".";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static string B(bool b) => b ? "1" : "0";
    static string Capture(Func<string> f)
    {
        try
        {
            return f();
        }
        catch (Exception e)
        {
            if (e is TargetInvocationException && e.InnerException != null)
                e = e.InnerException;
            return "E:" + e.GetType().FullName + ":" + unchecked((uint)e.HResult).ToString("x8") + ":" +
                   Hex(e.Message);
        }
    }
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        var type = typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexNode");
        var kindType = typeof(Regex).Assembly.GetType("System.Text.RegularExpressions.RegexNodeKind");
        var flags =
            BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
        var target = type.GetMethod("CreateOneWithCaseConversion", flags);
        var dm = new DynamicMethod(
            "CaseNode", typeof(object),
            new[] { typeof(char), typeof(RegexOptions), typeof(CultureInfo), typeof(int).MakeByRefType() },
            typeof(Program).Module, true);
        var il = dm.GetILGenerator();
        var behavior = il.DeclareLocal(target.GetParameters()[3].ParameterType.GetElementType());
        var result = il.DeclareLocal(type);
        il.Emit(OpCodes.Ldarg_3);
        il.Emit(OpCodes.Ldind_I4);
        il.Emit(OpCodes.Stloc, behavior);
        il.Emit(OpCodes.Ldarg_0);
        il.Emit(OpCodes.Ldarg_1);
        il.Emit(OpCodes.Ldarg_2);
        il.Emit(OpCodes.Ldloca, behavior);
        il.Emit(OpCodes.Call, target);
        il.Emit(OpCodes.Stloc, result);
        il.Emit(OpCodes.Ldarg_3);
        il.Emit(OpCodes.Ldloc, behavior);
        il.Emit(OpCodes.Stind_I4);
        il.Emit(OpCodes.Ldloc, result);
        il.Emit(OpCodes.Ret);
        var factory = (Case)dm.CreateDelegate(typeof(Case));
        object Call(object n, string name, params object[] args) =>
            type.GetMethod(name, flags).Invoke(n, args);
        object Prop(object n, string name) => type.GetProperty(name, flags).GetValue(n);
        object Field(object n, string name) => type.GetField(name, flags).GetValue(n);
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            var nodes = new Dictionary<int, object>();
            string Id(object n)
            {
                if (n == null)
                    return "-";
                foreach (var item in nodes)
                    if (ReferenceEquals(item.Value, n))
                        return item.Key.ToString();
                return "?";
            }
            var output = new List<string>();
            foreach (string command in row.Split(';'))
                output.Add(Capture(
                    () =>
                    {
                        var f = command.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                        string op = f[0];
                        int key = int.Parse(f[1]);
                        if (op == "N")
                        {
                            object kind = Enum.ToObject(kindType, (byte) int.Parse(f[2]));
                            var options = (RegexOptions) int.Parse(f[3]);
                            Type[] types;
                            object[] values;
                            switch (f[4])
                            {
                            case "z":
                                types = new[] { kindType, typeof(RegexOptions) };
                                values = new[] { kind, (object)options };
                                break;
                            case "c":
                                types = new[] { kindType, typeof(RegexOptions), typeof(char) };
                                values = new[] { kind, (object)options, (char)int.Parse(f[5]) };
                                break;
                            case "s":
                                types = new[] { kindType, typeof(RegexOptions), typeof(string) };
                                values = new[] { kind, (object)options, Dec(f[5]) };
                                break;
                            case "m":
                                types = new[] { kindType, typeof(RegexOptions), typeof(int) };
                                values = new[] { kind, (object)options, int.Parse(f[5]) };
                                break;
                            default:
                                types = new[] { kindType, typeof(RegexOptions), typeof(int), typeof(int) };
                                values = new[] { kind, (object)options, int.Parse(f[5]), int.Parse(f[6]) };
                                break;
                            }
                            nodes[key] = type.GetConstructor(types).Invoke(values);
                            return op;
                        }
                        if (op == "C")
                        {
                            int state = int.Parse(f[5]);
                            var culture = new NamedCulture(Dec(f[4]));
                            nodes[key] = factory((char)int.Parse(f[2]), (RegexOptions) int.Parse(f[3]),
                                                 culture, ref state);
                            return "C:" + state + ":" + culture.Reads;
                        }
                        if (op == "U")
                        {
                            nodes[key] =
                                Call(nodes[int.Parse(f[2])], f[3] == "g"   ? "ReduceGroup"
                                                             : f[3] == "l" ? "ReduceLoops"
                                                             : f[3] == "c" ? "ReduceConcatenation"
                                                                           : "ReplaceNodeIfUnnecessary");
                            return op;
                        }
                        var node = nodes[key];
                        if (op == "K")
                        {
                            nodes[999] =
                                Call(node, "MakeQuantifier", f[2] == "1", int.Parse(f[3]), int.Parse(f[4]));
                            return op;
                        }
                        if (op == "B")
                        {
                            nodes[999] = Call(node, "FinalOptimize");
                            return op;
                        }
                        if (op == "P")
                        {
                            int other = int.Parse(f[3]);
                            object adopted = other < 0 ? null : nodes[other];
                            if (f[2] == "a")
                                Call(node, "AddChild", adopted);
                            else
                                Call(node, f[2] == "i" ? "InsertChild" : "ReplaceChild", int.Parse(f[4]),
                                     adopted);
                            return op;
                        }
                        if (op == "Y")
                        {
                            string method = f[2] == "r"   ? "Reduce"
                                            : f[2] == "a" ? "ReduceAlternation"
                                            : f[2] == "t" ? "ReduceAtomic"
                                            : f[2] == "l" ? "ReduceLookaround"
                                            : f[2] == "e" ? "ReduceExpressionConditional"
                                            : f[2] == "b" ? "ReduceBackreferenceConditional"
                                                          : "EliminateEndingBacktracking";
                            var result = Call(node, method);
                            nodes[999] = f[2] == "z" ? node : result;
                            return op;
                        }
                        if (op == "S")
                        {
                            object[] values = { int.Parse(f[2]), int.Parse(f[3]), 123, "sentinel",
                                                f[4] == "1" };
                            bool success = (bool)Call(node, "TryGetOrdinalCaseInsensitiveString", values);
                            return B(success) + ":" + values[2] + ":" + Hex((string)values[3]);
                        }
                        if (op == "D")
                        {
                            var objects = new List<object>();
                            string Add(object value)
                            {
                                if (value == null)
                                    return "-";
                                int id = objects.IndexOf(value);
                                if (id < 0)
                                {
                                    id = objects.Count;
                                    objects.Add(value);
                                }
                                return id.ToString();
                            }
                            string result = "";
                            foreach (var entry in new SortedDictionary<int, object>(nodes))
                                result += entry.Key + "=" + Add(entry.Value) + ",";
                            for (int i = 0; i < objects.Count; i++)
                            {
                                object current = objects[i];
                                int count = (int)Call(current, "ChildCount");
                                for (int j = 0; j < count; j++)
                                    Add(Call(current, "Child", j));
                                Add(Field(current, "Parent"));
                            }
                            string Ref(object value) =>
                                value == null ? "-" : objects.IndexOf(value).ToString();
                            for (int i = 0; i < objects.Count; i++)
                            {
                                object current = objects[i], children = Field(current, "Children");
                                string shape = children == null                  ? "-"
                                               : type.IsInstanceOfType(children) ? "o:" + Ref(children)
                                                                                 : "l:";
                                if (children is IList list)
                                {
                                    var refs = new List<string>();
                                    foreach (object child in list)
                                        refs.Add(Ref(child));
                                    shape += string.Join(",", refs);
                                }
                                string text = (string)Prop(current, "Str");
                                int first = 0;
                                for (; first < objects.Count; first++)
                                    if (ReferenceEquals(Prop(objects[first], "Str"), text))
                                        break;
                                result += "/" + i + "=" + Convert.ToInt32(Prop(current, "Kind")) + ":" +
                                          Convert.ToInt32(Field(current, "Options")) + ":" +
                                          (int)(char)Prop(current, "Ch") + ":" + Prop(current, "M") + ":" +
                                          Prop(current, "N") + ":" + Hex(text) + ":" +
                                          Ref(Field(current, "Parent")) + ":" + shape + ":" +
                                          B((bool)Prop(current, "IsOneFamily")) +
                                          B((bool)Prop(current, "IsNotoneFamily")) +
                                          B((bool)Prop(current, "IsSetFamily")) + "@" + first;
                            }
                            return result;
                        }
                        if (op == "W")
                            return B((bool)Call(null, "CanBeMadeAtomic", node, nodes[int.Parse(f[2])],
                                                f[3] == "1", f[4] == "1"));
                        if (op == "E")
                        {
                            if (f[2] == "f")
                                return Id(Call(node, "FindLastExpressionInLoopForAutoAtomic"));
                            Call(node, "FindAndMakeLoopsAtomic");
                            return op;
                        }
                        if (op == "O")
                        {
                            string target = f[2] == "s" ? "ReduceSingleLetterAndNestedAlternations"
                                                        : "RemoveRedundantEmptiesAndNothings";
                            MethodInfo method = null;
                            foreach (var candidate in type.GetMethods(flags))
                                if (candidate.Name.Contains("g__" + target + "|"))
                                {
                                    method = candidate;
                                    break;
                                }
                            if (method == null)
                                throw new MissingMethodException(target);
                            var result =
                                method.Invoke(method.IsStatic ? null : node,
                                              method.IsStatic ? new[] { node } : Array.Empty<object>());
                            return f[2] == "s" ? op : Id(result);
                        }
                        if (op == "F")
                        {
                            object value = f[2] == "Str"      ? Dec(f[3])
                                           : f[2] == "Parent" ? (f[3] == "-" ? null : nodes[int.Parse(f[3])])
                                           : f[2] == "Kind" ? Enum.ToObject(kindType, (byte) int.Parse(f[3]))
                                           : f[2] == "Options" ? (RegexOptions) int.Parse(f[3])
                                           : f[2] == "Ch"      ? (object)(char)int.Parse(f[3])
                                                               : int.Parse(f[3]);
                            if (f[2] == "Options" || f[2] == "Parent")
                                type.GetField(f[2], flags).SetValue(node, value);
                            else
                                type.GetProperty(f[2], flags).SetValue(node, value);
                            return op;
                        }
                        if (op == "H")
                        {
                            int shape = int.Parse(f[2]);
                            object children = null;
                            if (shape == 1)
                                children = f[3] == "-" ? null : nodes[int.Parse(f[3])];
                            else if (shape == 2)
                            {
                                var list =
                                    (IList)Activator.CreateInstance(typeof(List<>).MakeGenericType(type));
                                for (int i = 3; i < f.Length; i++)
                                    list.Add(f[i] == "-" ? null : nodes[int.Parse(f[i])]);
                                children = list;
                            }
                            type.GetField("Children", flags).SetValue(node, children);
                            return op;
                        }
                        if (op == "Q")
                        {
                            object children = Field(node, "Children");
                            string shape = children == null                  ? "-"
                                           : type.IsInstanceOfType(children) ? "o:" + Id(children)
                                                                             : "l:";
                            if (children is IList list)
                            {
                                var ids = new List<string>();
                                foreach (object child in list)
                                    ids.Add(Id(child));
                                shape += string.Join(",", ids);
                            }
                            return Convert.ToInt32(Prop(node, "Kind")) + ":" +
                                   Convert.ToInt32(Field(node, "Options")) + ":" +
                                   (int)(char)Prop(node, "Ch") + ":" + Prop(node, "M") + ":" +
                                   Prop(node, "N") + ":" + Hex((string)Prop(node, "Str")) + ":" +
                                   Id(Field(node, "Parent")) + ":" + shape + ":" +
                                   B((bool)Prop(node, "IsOneFamily")) +
                                   B((bool)Prop(node, "IsNotoneFamily")) + B((bool)Prop(node, "IsSetFamily"));
                        }
                        if (op == "I")
                            return B(ReferenceEquals(Prop(node, "Str"), Prop(nodes[int.Parse(f[2])], "Str")));
                        if (op == "V")
                        {
                            Call(node, f[2] == "s" ? "ReduceConcatenationWithAdjacentStrings"
                                                   : "ReduceConcatenationWithAdjacentLoops");
                            return op;
                        }
                        if (op == "J")
                            return Id(Call(node, "Child", int.Parse(f[2])));
                        if (op == "Z")
                        {
                            Call(node, "ReduceSet");
                            return op;
                        }
                        if (op == "T")
                        {
                            Call(node, "MakeLoopAtomic");
                            return op;
                        }
                        if (op == "R")
                        {
                            Call(node, "ReverseConcatenationIfRightToLeft");
                            return op;
                        }
                        if (op == "L")
                        {
                            object literal = Call(node, "FindStartingLiteral", int.Parse(f[2]));
                            if (literal == null)
                                return "-";
                            var t = literal.GetType();
                            var range = ((char, char))t.GetField("Range").GetValue(literal);
                            var ascii = (char[])t.GetField("AsciiChars").GetValue(literal);
                            return (int)range.Item1 + "," + (int)range.Item2 + ":" +
                                   Hex((string)t.GetField("String").GetValue(literal)) + ":" +
                                   Hex((string)t.GetField("SetChars").GetValue(literal)) + ":" +
                                   Hex(ascii == null ? null : new string(ascii)) + ":" +
                                   B((bool)t.GetField("Negated").GetValue(literal));
                        }
                        if (op == "A")
                        {
                            var data = new List<string>();
                            data.Add(Capture(() => Call(node, "ComputeMinLength").ToString()));
                            data.Add(Capture(() =>
                                             {
                                                 var v = Call(node, "ComputeMaxLength");
                                                 return v == null ? "-" : v.ToString();
                                             }));
                            object[] reason = { "sentinel" };
                            var support = Capture(() => B((bool)Call(node, "SupportsCompilation", reason)));
                            data.Add(support + ":" + Hex((string)reason[0]));
                            data.Add(Capture(() => Id(Call(node, "FindBranchOneOrMultiStart"))));
                            data.Add(
                                Capture(() => ((int)(char)Call(node, "FirstCharOfOneOrMulti")).ToString()));
                            data.Add(Capture(() => Id(Call(node, "FindStartingLiteralNode"))));
                            return string.Join(",", data);
                        }
                        throw new Exception("Unknown node probe operation");
                    }));
            Console.WriteLine(string.Join("|", output));
        }
    }
}
