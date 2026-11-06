using System;
using System.Text;
using System.Reflection;
using System.Collections.Generic;
class FlowProgram
{
    const BindingFlags Flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance;
    static readonly Type EncoderType =
        typeof(System.Xml.XmlDocument).Assembly.GetType("System.Xml.XmlTextEncoder")!;
    static string Hex(string? s)
    {
        if (s == null)
            return "~";
        if (s.Length == 0)
            return "-";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.ToString();
    }
    static string? Decode(string s)
    {
        if (s == "~")
            return null;
        if (s == "-")
            return "";
        var b = new StringBuilder();
        for (int i = 0; i < s.Length; i += 4)
            b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
        return b.ToString();
    }
    static void Call(object e, string name,
                     params object?[] args) => EncoderType.GetMethod(name, Flags)!.Invoke(e, args);
    static void Execute(object e, string instruction)
    {
        var p = instruction.Split(',');
        string? s = p.Length > 1 ? Decode(p[1]) : null;
        switch (p[0])
        {
        case "A":
            Call(e, "StartAttribute", true);
            break;
        case "B":
            Call(e, "StartAttribute", false);
            break;
        case "E":
            Call(e, "EndAttribute");
            break;
        case "Q":
            EncoderType.GetProperty("QuoteChar", Flags)!.SetValue(e, s![0]);
            break;
        case "W":
            ((SpanWrite)
                 EncoderType.GetMethod("Write", Flags, null, new[] { typeof(ReadOnlySpan<char>) },
                                       null)!.CreateDelegate(typeof(SpanWrite), e))(s.AsSpan());
            break;
        case "R":
            Call(e, "WriteRawWithSurrogateChecking", s);
            break;
        case "C":
            Call(e, "WriteCharEntity", s![0]);
            break;
        case "N":
            Call(e, "WriteEntityRef", s);
            break;
        case "S":
            Call(e, "WriteSurrogateCharEntity", s![0], s[1]);
            break;
        case "D":
            Call(e, "WriteSurrogateChar", s![0], s[1]);
            break;
        case "V":
        case "T":
            EncoderType
                .GetMethod(
                    p[0] == "V" ? "Write" : "WriteRaw", Flags, null,
                    new[] { typeof(char[]), typeof(int), typeof(int) },
                    null)!.Invoke(e, new object?[] { s?.ToCharArray(), int.Parse(p[2]), int.Parse(p[3]) });
            break;
        }
    }
    static string Error(Exception e)
    {
        while (e is TargetInvocationException && e.InnerException != null)
            e = e.InnerException;
        string parameter = "~", actual = "~";
        if (e is ArgumentException arg)
            parameter = Hex(arg.ParamName);
        if (e is ArgumentOutOfRangeException range && range.ActualValue != null)
            actual = range.ActualValue.ToString()!;
        return e.GetType().FullName + ":" + e.HResult.ToString("x8") + ":" + Hex(e.Message) + ":" +
               parameter + ":" + actual;
    }
    static void Main()
    {
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            var commands = line.Split(';');
            var header = commands[0].Split(',');
            var writer = new RecordingWriter(int.Parse(header[0]));
            var encoder = Activator.CreateInstance(EncoderType, Flags, null, new object[] { writer }, null)!;
            int reenter = int.Parse(header[1]);
            writer.OnCall = index =>
            {
                if (index == reenter)
                    Execute(encoder, commands[1]);
            };
            var snapshots = new List<string>();
            for (int i = 2; i < commands.Length; i++)
            {
                string error = "-";
                try
                {
                    Execute(encoder, commands[i]);
                }
                catch (Exception e)
                {
                    error = Error(e);
                }
                snapshots.Add(
                    Hex((string)EncoderType.GetProperty("AttributeValue", Flags)!.GetValue(encoder)!) + "@" +
                    error);
            }
            Console.WriteLine(string.Join("|", writer.Calls) + ";" + string.Join(";", snapshots));
        }
    }
}
