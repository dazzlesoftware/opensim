using System;
using System.IO;
using System.Xml;
using System.Text;
using System.Globalization;
class TracingStream : MemoryStream
{
    public bool Seekable = true;
    public int Chunk = 3;
    public StringBuilder Trace = new StringBuilder();
    public TracingStream(byte[] value) : base(value)
    {
    }
    public override bool CanSeek => Seekable;
    public override int Read(byte[] buffer, int offset, int count)
    {
        Trace.Append("R" + count + ",");
        return base.Read(buffer, offset, Math.Min(count, Chunk));
    }
    protected override void Dispose(bool disposing)
    {
        Trace.Append("C,");
        base.Dispose(disposing);
    }
}
class Program
{
    static string D(string s)
    {
        if (s == "-")
            return "";
        char[] v = new char[s.Length / 4];
        for (int i = 0; i < v.Length; i++)
            v[i] = (char)Convert.ToUInt16(s.Substring(i * 4, 4), 16);
        return new string(v);
    }
    static string E(string s)
    {
        if (s.Length == 0)
            return "-";
        var v = new StringBuilder();
        foreach (char c in s)
            v.Append(((int)c).ToString("x4"));
        return v.ToString();
    }
    static string Shape(XmlNode node)
    {
        var b = new StringBuilder(((int)node.NodeType) + ":" + E(node.Name) + ":" + E(node.Prefix) + ":" +
                                  E(node.LocalName) + ":" + E(node.NamespaceURI) + ":" +
                                  (node.Value == null ? "~" : E(node.Value)) + ":" +
                                  (node is XmlElement element && element.IsEmpty ? "1" : "0") + "[");
        foreach (XmlNode child in node.ChildNodes)
            b.Append(Shape(child) + ",");
        b.Append("]{");
        if (node.Attributes != null)
            foreach (XmlNode a in node.Attributes)
                b.Append(Shape(a) + ",");
        return b + "}";
    }
    static string Summary(XmlDocument document)
    {
        var pending = new System.Collections.Generic.Stack<(XmlNode, int)>();
        pending.Push((document, 0));
        ulong nodes = 0, attrs = 0, maximum = 0, units = 0, hash = 14695981039346656037UL;
        void Number(ulong value)
        {
            hash = unchecked((hash ^ value) * 1099511628211UL);
        }void Text(string? value)
        {
            Number(value == null ? 0UL : 1UL);
            if (value != null)
            {
                Number((ulong)value.Length);
                foreach (char c in value)
                    Number(c);
            }
        }
        while (pending.Count > 0)
        {
            var (node, depth) = pending.Pop();
            ++nodes;
            maximum = Math.Max(maximum, (ulong)depth);
            Number((ulong)node.NodeType);
            Number((ulong)depth);
            Number(node is XmlElement element && element.IsEmpty ? 1UL : 0UL);
            Text(node.Name);
            Text(node.Prefix);
            Text(node.LocalName);
            Text(node.NamespaceURI);
            Text(node.Value);
            if (node.Value != null)
                units += (ulong)node.Value.Length;
            var children = new System.Collections.Generic.List<XmlNode>();
            foreach (XmlNode child in node.ChildNodes)
                children.Add(child);
            var attributes = new System.Collections.Generic.List<XmlNode>();
            if (node.Attributes != null)
                foreach (XmlNode attribute in node.Attributes)
                    attributes.Add(attribute);
            attrs += (ulong)attributes.Count;
            Number((ulong)children.Count);
            Number((ulong)attributes.Count);
            for (int i = children.Count - 1; i >= 0; --i)
                pending.Push((children[i], depth + 1));
            for (int i = attributes.Count - 1; i >= 0; --i)
                pending.Push((attributes[i], depth + 1));
        }
        return nodes + "," + attrs + "," + maximum + "," + units + "," + hash;
    }
    static void Navigation(StringBuilder output, XmlReader r, bool resolve = false)
    {
        var info = (IXmlLineInfo)r;
        void Snapshot()
        {
            output.Append(((int)r.NodeType) + ":" + r.Depth + ":" + info.LineNumber + ":" +
                          info.LinePosition + ":" + E(r.Name) + ":" + E(r.Value) + ",");
        }
        output.Append("NAV[");
        if (r.MoveToFirstAttribute())
        {
            do
            {
                Snapshot();
                for (int j = 0; j < (resolve ? 1024 : 2); j++)
                {
                    bool moved = r.ReadAttributeValue();
                    output.Append((moved ? 1 : 0) + ":");
                    Snapshot();
                    if (resolve && !moved)
                        break;
                    if (resolve && r.NodeType == XmlNodeType.EntityReference)
                        r.ResolveEntity();
                }
            } while (r.MoveToNextAttribute());
            output.Append((r.MoveToElement() ? 1 : 0) + ":");
            Snapshot();
        }
        output.Append("]");
    }
    static void Main(string[] args)
    {
        long docLimit = 0, entityLimit = -1;
        bool normalize = Array.IndexOf(args, "--normalize") >= 0;
        foreach (var arg in args)
        {
            if (arg.StartsWith("--doc-limit="))
                docLimit = long.Parse(arg.Substring(12));
            if (arg.StartsWith("--entity-limit="))
                entityLimit = long.Parse(arg.Substring(15));
        }
        int chunk = 3;
        foreach (var arg in args)
            if (arg.StartsWith("--chunk="))
                chunk = int.Parse(arg.Substring(8));
        bool resolve = Array.IndexOf(args, "--resolve") >= 0,
             summarize = Array.IndexOf(args, "--summary") >= 0,
             captureState = Array.IndexOf(args, "--state") >= 0,
             startAttribute = Array.IndexOf(args, "--start-attribute") >= 0,
             streamMode = Array.IndexOf(args, "--stream") >= 0,
             nonseek = Array.IndexOf(args, "--nonseek") >= 0,
             closeStream = Array.IndexOf(args, "--close-stream") >= 0,
             prohibit = Array.IndexOf(args, "--prohibit") >= 0,
             nav = Array.IndexOf(args, "--navigation") >= 0, load = Array.IndexOf(args, "--load") >= 0,
             bytes = Array.IndexOf(args, "--bytes") >= 0, legacy = Array.IndexOf(args, "--legacy") >= 0;
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        CultureInfo.CurrentUICulture = CultureInfo.InvariantCulture;
        string? line;
        while ((line = Console.ReadLine()) != null)
        {
            var output = new StringBuilder();
            var document = new XmlDocument();
            XmlReader? r = null;
            string state = "";
            TracingStream? stream = null;
            try
            {
                var settings = new XmlReaderSettings {
                    MaxCharactersInDocument = docLimit,
                    MaxCharactersFromEntities = entityLimit < 0 ? 10000000 : entityLimit,
                    DtdProcessing = prohibit ? DtdProcessing.Prohibit : DtdProcessing.Parse,
                    CloseInput = closeStream
                };
                if (streamMode)
                    stream = new TracingStream(
                        line == "-" ? Array.Empty<byte>()
                                    : Convert.FromHexString(line)) { Seekable = !nonseek, Chunk = chunk };
                if (legacy)
                {
                    var old =
                        bytes ? new XmlTextReader(stream ??
                                                  new MemoryStream(line == "-" ? Array.Empty<byte>()
                                                                               : Convert.FromHexString(line)))
                              : new XmlTextReader(new StringReader(D(line)));
                    old.EntityHandling = EntityHandling.ExpandCharEntities;
                    typeof(XmlTextReader)
                        .GetProperty("XmlValidatingReaderCompatibilityMode",
                                     System.Reflection.BindingFlags.Instance |
                                         System.Reflection.BindingFlags.NonPublic)!
                        .SetValue(old, Array.IndexOf(args, "--standalone") < 0);
                    r = old;
                }
                else
                    r = bytes ? XmlReader.Create(
                                    stream ?? new MemoryStream(line == "-" ? Array.Empty<byte>()
                                                                           : Convert.FromHexString(line)),
                                    settings)
                              : XmlReader.Create(new StringReader(D(line)), settings);
                if (legacy && normalize)
                    ((XmlTextReader)r).Normalization = true;
                var info = (IXmlLineInfo)r;
                if (startAttribute)
                {
                    r.Read();
                    r.MoveToFirstAttribute();
                }
                if (load)
                {
                    document.Load(r);
                    output.Append("OK");
                }
                else
                {
                    while (r.Read())
                    {
                        output.Append(((int)r.NodeType) + ":" + r.Depth + ":" + info.LineNumber + ":" +
                                      info.LinePosition + ":" + (r.IsEmptyElement ? 1 : 0) + ":" + E(r.Name) +
                                      ":" + E(r.Prefix) + ":" + E(r.LocalName) + ":" + E(r.NamespaceURI) +
                                      ":" + E(r.Value) + "[");
                        if (r.MoveToFirstAttribute())
                        {
                            do
                            {
                                output.Append(E(r.Name) + ":" + E(r.Prefix) + ":" + E(r.LocalName) + ":" +
                                              E(r.NamespaceURI) + ":" + E(r.Value) + ",");
                            } while (r.MoveToNextAttribute());
                            r.MoveToElement();
                        }
                        output.Append("]|");
                        if (nav)
                            Navigation(output, r, resolve);
                        if (resolve && r.NodeType == XmlNodeType.EntityReference)
                            r.ResolveEntity();
                    }
                    output.Append("END");
                }
            }
            catch (Exception e)
            {
                output.Append("E:" + e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" +
                              E(e.Message) + ":" + (e is XmlException x ? x.LineNumber.ToString() : "~") +
                              ":" + (e is XmlException y ? y.LinePosition.ToString() : "~"));
            }
            finally
            {
                if (captureState && r != null)
                    state = "/STATE:" + (int)r.ReadState + "," + (r.EOF ? 1 : 0) + "," + (int)r.NodeType +
                            "," + r.Depth;
                r?.Dispose();
            }
            output.Append(state);
            if (streamMode && stream != null)
                output.Append("/IO:" + stream.Trace);
            if (load)
                output.Append("/" + (summarize ? Summary(document) : Shape(document)));
            Console.WriteLine(output);
        }
    }
}
