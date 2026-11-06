using System;
using System.IO;
using System.Xml;
using System.Text;
using System.Reflection;
using System.Collections.Generic;
sealed class WriterSink : TextWriter
{
    public readonly List<string> Calls = new();
    public int Fail;
    public Action<int>? OnCall;
    public int ArrayMode;
    public char[]? LastArray;
    readonly List<char[]> arrays = new();
    int Remember(char[]? value)
    {
        LastArray = value;
        if (value == null)
            return 0;
        for (int i = 0; i < arrays.Count; i++)
            if (ReferenceEquals(arrays[i], value))
                return i + 1;
        arrays.Add(value);
        return arrays.Count;
    }
    public readonly int EncodingMode;
    public string? EncodingName = "native-x";
    readonly Encoding custom;
    public WriterSink(int fail, int mode)
    {
        Fail = fail;
        EncodingMode = mode;
        custom = new CustomEncoding(this);
    }
    sealed class CustomEncoding : UnicodeEncoding
    {
        readonly WriterSink owner;
        public CustomEncoding(WriterSink value)
        {
            owner = value;
        }
        public override string WebName
        {
            get {
                owner.Emit("B");
                return owner.EncodingName!;
            }
        }
    }
    public static string Hex(string? s)
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
    public void Emit(string kind, string? s = null)
    {
        Calls.Add(kind + ":" + Hex(s));
        int index = Calls.Count;
        OnCall?.Invoke(index);
        if (index == Fail)
            throw new IOException("injected writer failure");
    }
    public override Encoding Encoding
    {
        get {
            Emit("G");
            return EncodingMode == 1 ? null! : EncodingMode == 2 ? custom : Encoding.Unicode;
        }
    }
    public override void Write(char c) => Emit("C", new string(c, 1));
    public override void Write(string? s) => Emit("T", s);
    public override void Write(ReadOnlySpan<char> s) => Emit("S", s.ToString());
    public override void Write(char[] s, int index, int count)
    {
        int id = Remember(s);
        if (ArrayMode == 2)
        {
            Emit($"Z{id}:{s.Length}:{index}:{count}");
            return;
        }
        string value = new string(s, index, count);
        Emit(ArrayMode == 1 ? $"A{id}:{s.Length}:{index}:{count}" : "A", value);
    }
    public override void Write(char[]? s)
    {
        if (ArrayMode != 1)
        {
            base.Write(s);
            return;
        }
        int id = Remember(s);
        Emit($"H{id}:{s?.Length??-1}", s == null ? null : new string(s));
    }
    public override void WriteLine() => Emit("L");
    public override void Flush() => Emit("F");
    protected override void Dispose(bool disposing) => Emit("X");
}
class WriterProgram
{
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
    static string Error(Exception e)
    {
        while (e is TargetInvocationException && e.InnerException != null)
            e = e.InnerException;
        string parameter = "~", actual = "~";
        if (e is ArgumentException arg)
            parameter = WriterSink.Hex(arg.ParamName);
        if (e is ArgumentOutOfRangeException range && range.ActualValue != null)
            actual = range.ActualValue.ToString()!;
        return e.GetType().FullName + ":" + e.HResult.ToString("x8") + ":" + WriterSink.Hex(e.Message) + ":" +
               parameter + ":" + actual;
    }
    static string? Execute(XmlTextWriter w, WriterSink sink, string command)
    {
        var p = command.Split(',');
        string ? S(int i) => Decode(p[i]);
        int I(int i) => int.Parse(p[i]);
        switch (p[0])
        {
        case "encname":
            sink.EncodingName = S(1);
            break;
        case "mutate":
            sink.LastArray![I(1)] = S(2)![0];
            break;
        case "peek":
            return WriterSink.Hex(new string(sink.LastArray![I(1)], 1)) + ":" + sink.LastArray.Length;
        case "doc":
            if (I(1) < 0)
                w.WriteStartDocument();
            else
                w.WriteStartDocument(I(1) != 0);
            break;
        case "enddoc":
            w.WriteEndDocument();
            break;
        case "doctype":
            w.WriteDocType(S(1)!, S(2), S(3), S(4));
            break;
        case "elem":
            w.WriteStartElement(S(1), S(2)!, S(3));
            break;
        case "end":
            w.WriteEndElement();
            break;
        case "full":
            w.WriteFullEndElement();
            break;
        case "attr":
            w.WriteStartAttribute(S(1), S(2)!, S(3));
            break;
        case "endattr":
            w.WriteEndAttribute();
            break;
        case "comment":
            w.WriteComment(S(1));
            break;
        case "cdata":
            w.WriteCData(S(1));
            break;
        case "pi":
            w.WriteProcessingInstruction(S(1)!, S(2));
            break;
        case "entity":
            w.WriteEntityRef(S(1)!);
            break;
        case "char":
            w.WriteCharEntity(S(1)![0]);
            break;
        case "surrogate":
            w.WriteSurrogateCharEntity(S(1)![0], S(1)![1]);
            break;
        case "ws":
            w.WriteWhitespace(S(1));
            break;
        case "str":
            w.WriteString(S(1));
            break;
        case "raw":
            w.WriteRaw(S(1)!);
            break;
        case "chars":
            w.WriteChars(S(1)?.ToCharArray()!, I(2), I(3));
            break;
        case "rawchars":
            w.WriteRaw(S(1)?.ToCharArray()!, I(2), I(3));
            break;
        case "base64":
            w.WriteBase64(p[1] == "~"   ? null!
                          : p[1] == "-" ? Array.Empty<byte>()
                                        : Convert.FromHexString(p[1]),
                          I(2), I(3));
            break;
        case "binhex":
            w.WriteBinHex(p[1] == "~"   ? null!
                          : p[1] == "-" ? Array.Empty<byte>()
                                        : Convert.FromHexString(p[1]),
                          I(2), I(3));
            break;
        case "name":
            w.WriteName(S(1)!);
            break;
        case "nmtoken":
            w.WriteNmToken(S(1)!);
            break;
        case "qualified":
            w.WriteQualifiedName(S(1)!, S(2));
            break;
        case "lookup":
            return WriterSink.Hex(w.LookupPrefix(S(1)!));
        case "ns":
            w.Namespaces = I(1) != 0;
            break;
        case "fmt":
            w.Formatting = (Formatting)I(1);
            break;
        case "indent":
            w.Indentation = I(1);
            break;
        case "ic":
            w.IndentChar = S(1)![0];
            break;
        case "quote":
            w.QuoteChar = S(1)![0];
            break;
        case "flush":
            w.Flush();
            break;
        case "close":
            w.Close();
            break;
        }
        return "~";
    }
    static string State(XmlTextWriter w) => ((int)w.WriteState) + "," + ((int)w.XmlSpace) +
                                            "," + WriterSink.Hex(w.XmlLang) + "," + (w.Namespaces ? 1 : 0) +
                                            "," + ((int)w.Formatting) + "," + w.Indentation + "," +
                                            ((int)w.IndentChar) + "," + ((int)w.QuoteChar);
    static void Main()
    {
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            var commands = line.Split(';');
            var header = commands[0].Split(',');
            var sink = new WriterSink(int.Parse(header[0]), header.Length > 2 ? int.Parse(header[2]) : 0);
            XmlTextWriter? w = null;
            var snapshots = new List<string>();
            int reenter = header.Length > 3 ? int.Parse(header[3]) : 0;
            int start = header.Length > 3 ? 2 : 1;
            sink.OnCall = index =>
            {
                if (index == reenter)
                    Execute(w!, sink, commands[1]);
            };
            sink.ArrayMode = header.Length > 4 ? int.Parse(header[4]) : 0;
            try
            {
                if (header[1] == "1")
                    w = (XmlTextWriter)Activator.CreateInstance(
                        typeof(XmlTextWriter).Assembly.GetType("System.Xml.XmlDOMTextWriter")!,
                        BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance, null,
                        new object[] { sink }, null)!;
                else
                    w = new XmlTextWriter(sink);
            }
            catch (Exception e)
            {
                snapshots.Add("NEW@" + Error(e));
            }
            if (w != null)
                for (int i = start; i < commands.Length; i++)
                {
                    string error = "-", result = "~";
                    try
                    {
                        result = Execute(w, sink, commands[i])!;
                    }
                    catch (Exception e)
                    {
                        error = Error(e);
                    }
                    snapshots.Add(State(w) + "@" + result + "@" + error);
                }
            Console.WriteLine(string.Join("|", sink.Calls) + ";" + string.Join(";", snapshots));
        }
    }
}
