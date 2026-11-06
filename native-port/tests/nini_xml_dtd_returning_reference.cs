using System;
using System.IO;
using System.Xml;
using System.Text;
using System.Reflection;
using System.Collections;
using System.Globalization;
public class NameProxy : XmlNameTable
{
    public XmlNameTable Target = null!;
    public AdapterProxy Owner = null!;
    public override string Add(string v)
    {
        Owner.Mark("Intern");
        var r = Target.Add(v);
        return Owner.Flags.Length > 6 && Owner.Flags[6] == '1' ? null! : r;
    }
    public override string Add(char[] v, int o, int n)
    {
        Owner.Mark("Intern");
        var r = Target.Add(v, o, n);
        return Owner.Flags.Length > 6 && Owner.Flags[6] == '1' ? null! : r;
    }
    public override string? Get(string v) => Target.Get(v);
    public override string? Get(char[] v, int o, int n) => Target.Get(v, o, n);
}
public class NamespaceProxy : IXmlNamespaceResolver
{
    public IXmlNamespaceResolver Target = null!;
    public AdapterProxy Owner = null!;
    public System.Collections.Generic.IDictionary<string, string> GetNamespacesInScope(XmlNamespaceScope s) =>
        Target.GetNamespacesInScope(s);
    public string? LookupNamespace(string p)
    {
        Owner.Mark("LookupNamespace");
        return Target.LookupNamespace(p);
    }
    public string? LookupPrefix(string n) => Target.LookupPrefix(n);
}
public class AdapterProxy : DispatchProxy
{
    public object Target = null!;
    public string Flags = "01100";
    public int Fail = 0, Count = 0, Returned = 0;
    public System.Collections.Generic.List<string> Trace = new();
    public object Handler = null!;
    public string? ExternalText;
    public void Mark(string name)
    {
        Trace.Add(name);
        if (++Count == Fail)
        {
            string text = "fixture-" + name + "-" + Count;
            if (Flags.Length > 7 && Flags[7] == '1')
                throw new XmlException(text);
            throw new InvalidOperationException(text);
        }
    }
    protected override object? Invoke(MethodInfo? method, object?[]? args)
    {
        var n = method!.Name;
        if (n != "get_NameTable" && n != "get_NamespaceResolver")
            Mark(n);
        switch (n)
        {
        case "get_V1CompatibilityMode":
            return Flags[0] == '1';
        case "get_Normalization":
            return Flags[1] == '1';
        case "get_Namespaces":
            return Flags[2] == '1';
        case "get_DtdValidation":
            return Flags[3] == '1';
        case "get_ValidationEventHandling":
            return Flags[4] == '1' ? Handler : null;
        }

        if (n == "Throw" && Flags.Length > 8 && Flags[8] == '1' &&
            Returned++ < (Flags.Length > 9 ? Flags[9] - '0' : 1))
            return null;
        try
        {
            if (n == "PushExternalSubset" && ExternalText != null)
            {
                Target.GetType()
                    .Assembly.GetType("System.Xml.IDtdParserAdapter")!.GetMethod("PushInternalDtd")!.Invoke(
                        Target, new object?[] { "urn:external", ExternalText });
                return true;
            }
            if (n == "PushEntity" && ExternalText != null)
            {
                var entity = args![0]!;
                var et = entity.GetType();
                var isExternal = et.GetProperty("IsExternal", BindingFlags.Public | BindingFlags.NonPublic |
                                                                  BindingFlags.Instance)!;
                if ((bool)isExternal.GetValue(entity)!)
                {
                    et.GetProperty("Text", BindingFlags.Public | BindingFlags.NonPublic |
                                               BindingFlags.Instance)!.SetValue(entity, ExternalText);
                    try
                    {
                        return method.Invoke(Target, args);
                    }
                    finally
                    {
                        isExternal.SetValue(entity, true);
                    }
                }
            }
            var result = method.Invoke(Target, args);
            if (Flags.Length > 5 && Flags[5] == '1')
            {
                if (n == "OnNewLine")
                {
                    var i = Target.GetType().Assembly.GetType("System.Xml.IDtdParserAdapter")!;
                    var chars = (char[])i.GetProperty("ParsingBuffer")!.GetValue(Target)!;
                    int p = (int)args![0]!;
                    if (p < (int)i.GetProperty("ParsingBufferLength")!.GetValue(Target)!)
                        chars[p] = 'X';
                }
                if (n == "ParseNumericCharRef" || n == "ParsePI" || n == "ParseComment")
                {
                    if (args![0] is StringBuilder b)
                        b.Append("!");
                }
                else if (n == "ParseNamedCharRef" && args![1] is StringBuilder b)
                    b.Append("!");
            }
            if (n == "get_NameTable")
                return new NameProxy { Target = (XmlNameTable)result!, Owner = this };
            if (n == "get_NamespaceResolver")
                return new NamespaceProxy { Target = (IXmlNamespaceResolver)result!, Owner = this };
            return result;
        }
        catch (TargetInvocationException e) when (e.InnerException != null)
        {
            System.Runtime.ExceptionServices.ExceptionDispatchInfo.Capture(e.InnerException).Throw();
            throw;
        }
    }
}
public class HandlerProxy : DispatchProxy
{
    public AdapterProxy Owner = null!;
    protected override object? Invoke(MethodInfo? m, object?[]? a)
    {
        if (m!.Name == "SendEvent")
        {
            var e = (Exception)a![0]!;
            var x = (System.Xml.Schema.XmlSchemaException)e;
            Owner.Mark(
                "Validation:" + e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" + x.LineNumber +
                ":" + x.LinePosition + ":" +
                (x.SourceUri == null
                     ? "~"
                     : Convert.ToHexString(Encoding.Unicode.GetBytes(x.SourceUri)).ToLowerInvariant()) +
                ":" + (int)a[1]! + ":" + e.Message);
        }
        return null;
    }
}
class Program
{
    const BindingFlags F =
        BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.Static;
    static string D(string s)
    {
        if (s == "-")
            return "";
        var c = new char[s.Length / 4];
        for (int i = 0; i < c.Length; i++)
            c[i] = (char)Convert.ToUInt16(s.Substring(i * 4, 4), 16);
        return new string(c);
    }
    static string E(string? s)
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
    static object? P(object o, string n)
    {
        var p = o.GetType().GetProperty(n, F);
        if (p?.GetMethod != null)
            return p.GetValue(o);
        foreach (var i in o.GetType().GetInterfaces())
        {
            p = i.GetProperty(n, F);
            if (p?.GetMethod != null)
                return p.GetValue(o);
        }
        throw new Exception("Missing getter " + o.GetType() + "." + n);
    }
    static string N(object? n)
    {
        if (n == null)
            return "~";
        var q = (XmlQualifiedName)n;
        return E(q.Name) + "," + E(q.Namespace);
    }
    static string T(object? v)
    {
        if (v == null)
            return "~";
        if (v is string s)
            return "s" + E(s);
        if (v is string[] l)
            return "a" + string.Join(",", Array.ConvertAll(l, E));
        if (v is XmlQualifiedName q)
            return "q" + N(q);
        return "?" + v.GetType().Name;
    }
    static string Attr(object a)
    {
        var values = (IList?)P(a, "Values");
        var vs = new StringBuilder();
        if (values != null)
            foreach (var v in values)
            {
                if (vs.Length > 0)
                    vs.Append(',');
                vs.Append(E((string)v));
            }
        return N(P(a, "Name")) + ":" + P(a, "TokenizedType") + ":" + (int)P(a, "Presence")! + ":" +
               P(a, "IsDeclaredInExternal") + ":" + P(a, "IsXmlAttribute") + ":" + P(a, "LineNumber") + ":" +
               P(a, "LinePosition") + ":" + P(a, "ValueLineNumber") + ":" + P(a, "ValueLinePosition") + ":" +
               E((string?)P(a, "DefaultValueExpanded")) + ":" + T(P(a, "DefaultValueTyped")) + ":" + vs;
    }
    static string Info(object info)
    {
        var b = new StringBuilder();
        b.Append(N(P(info, "DocTypeName")))
            .Append('|')
            .Append(E((string?)P(info, "InternalDtdSubset")))
            .Append('|')
            .Append(P(info, "HasDefaultAttributes"))
            .Append(':')
            .Append(P(info, "HasNonCDataAttributes"));
        foreach (var name in new[] { "ElementDecls", "UndeclaredElementDecls" })
        {
            b.Append('|');
            foreach (DictionaryEntry e in (IDictionary)P(info, name)!)
            {
                var element = e.Value!;
                b.Append('[')
                    .Append(N(P(element, "Name")))
                    .Append(':')
                    .Append(P(element, "HasNonCDataAttribute"));
                foreach (DictionaryEntry a in (IDictionary)P(element, "AttDefs")!)
                    b.Append(';').Append(Attr(a.Value!));
                b.Append(']');
            }
        }
        foreach (var name in new[] { "GeneralEntities", "ParameterEntities" })
        {
            b.Append('|');
            foreach (DictionaryEntry entry in (IDictionary)P(info, name)!)
            {
                var e = entry.Value!;
                b.Append('[')
                    .Append(N(P(e, "Name")))
                    .Append(':')
                    .Append(N(P(e, "NData")))
                    .Append(':')
                    .Append(P(e, "IsExternal"))
                    .Append(':')
                    .Append(P(e, "DeclaredInExternal"))
                    .Append(':')
                    .Append(P(e, "IsParameterEntity"))
                    .Append(':')
                    .Append(P(e, "ParsingInProgress"))
                    .Append(':')
                    .Append(P(e, "Line"))
                    .Append(':')
                    .Append(P(e, "Pos"))
                    .Append(':')
                    .Append(E((string?)P(e, "BaseURI")))
                    .Append(':')
                    .Append(E((string?)P(e, "DeclaredURI")))
                    .Append(':')
                    .Append(E((string?)P(e, "Url")))
                    .Append(':')
                    .Append(E((string?)P(e, "Pubid")))
                    .Append(':')
                    .Append(E((string?)P(e, "Text")))
                    .Append(']');
            }
        }
        b.Append('|');
        foreach (DictionaryEntry entry in (IDictionary)P(info, "Notations")!)
        {
            var n = entry.Value!;
            b.Append('[')
                .Append(N(P(n, "Name")))
                .Append(':')
                .Append(E((string?)P(n, "SystemLiteral")))
                .Append(':')
                .Append(E((string?)P(n, "Pubid")))
                .Append(']');
        }
        return b.ToString();
    }
    static string Snapshot(object raw, XmlReader reader)
    {
        var chars = (char[])P(raw, "ParsingBuffer")!;
        int used = (int)P(raw, "ParsingBufferLength")!;
        return P(raw, "CurrentPosition") + ":" + P(raw, "LineNo") + ":" + P(raw, "LineStartPosition") + ":" +
               P(raw, "IsEof") + ":" + P(raw, "EntityStackLength") + ":" + (int)reader.ReadState + ":" +
               E(new string(chars, 0, used));
    }
    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--metadata")
        {
            var file = typeof(XmlDocument).Assembly.Location;
            Console.WriteLine(System.Text.Json.JsonSerializer.Serialize(
                new { runtime = Environment.Version.ToString(), assembly = file,
                      sha256 = Convert.ToHexString(
                          System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(file))),
                      platform = System.Runtime.InteropServices.RuntimeInformation.OSDescription }));
            return;
        }
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split('|');
            var reader = XmlReader.Create(new StringReader(a[0] == "G" ? "" : D(a[1])),
                                          new XmlReaderSettings { DtdProcessing = DtdProcessing.Parse });
            var t = reader.GetType();
            object parser = Activator.CreateInstance(t.Assembly.GetType("System.Xml.DtdParser")!, true)!;
            object adapter =
                Activator.CreateInstance(t.GetNestedType("DtdParserProxy", BindingFlags.NonPublic)!, F, null,
                                         new object[] { reader }, null)!;
            object raw = adapter;
            AdapterProxy? proxy = null;
            if (a.Length >= 4)
            {
                var it = t.Assembly.GetType("System.Xml.IDtdParserAdapterV1")!;
                adapter = DispatchProxy.Create(it, typeof(AdapterProxy));
                proxy = (AdapterProxy)adapter;
                proxy.Target =
                    Activator.CreateInstance(t.GetNestedType("DtdParserProxy", BindingFlags.NonPublic)!, F,
                                             null, new object[] { reader }, null)!;
                proxy.Flags = a[3];
                proxy.Fail = a.Length > 4 ? int.Parse(a[4]) : 0;
                proxy.ExternalText = a.Length > 5 && a[5] != "~" ? D(a[5]) : null;
                var ht = t.Assembly.GetType("System.Xml.IValidationEventHandling")!;
                proxy.Handler = DispatchProxy.Create(ht, typeof(HandlerProxy));
                ((HandlerProxy)proxy.Handler).Owner = proxy;
            }
            object? info = null;
            string result = "";
            try
            {
                var iface = parser.GetType().GetInterface("System.Xml.IDtdParser")!;
                if (a[0].StartsWith("I"))
                    info =
                        iface.GetMethod("ParseInternalDtd")!.Invoke(parser, new object[] { adapter, true });
                else if (a[0] == "G")
                {
                    string ? ND(string v) => v == "~" ? null : D(v);
                    info = iface.GetMethod("ParseFreeFloatingDtd")!.Invoke(
                        parser, new object?[] { ND(a[6]), ND(a[2]), ND(a[7]), ND(a[8]), ND(a[1]), adapter });
                }
                else
                    info = iface.GetMethod("ParseFreeFloatingDtd")!.Invoke(
                        parser, new object?[] { "", D(a[2]), null, null, D(a[1]), adapter });
                result = "OK|" + Info(info!);
                if (a[0] == "G")
                    result +=
                        "|IDS:" + E((string?)parser.GetType().GetField("_publicId", F)!.GetValue(parser)) +
                        ":" + E((string?)parser.GetType().GetField("_systemId", F)!.GetValue(parser));
            }
            catch (Exception e)
            {
                while (e is TargetInvocationException && e.InnerException != null)
                    e = e.InnerException;
                result =
                    "E|" + e.GetType().FullName + "|" + unchecked((uint)e.HResult) + "|" + E(e.Message) +
                    "|" +
                    (e is XmlException x ? x.LineNumber + ":" + x.LinePosition + ":" + E(x.SourceUri) : "-");
            }
            Console.WriteLine(result + (proxy == null ? "" : "|TRACE:" + E(string.Join(";", proxy.Trace))) +
                              (proxy == null ? "" : "|STATE:" + Snapshot(raw, reader)));
        }
    }
}
