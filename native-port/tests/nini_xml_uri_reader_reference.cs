using System.Xml;
using System.Net;
using System.Text;

class ProbeStream : Stream
{
    public string Body = "<a/>";
    public int PositionValue, Fault, Reads;
    public StringBuilder Trace;
    public override bool CanRead => true;
    public override bool CanSeek => true;
    public override bool CanWrite => false;

    public override long Length
    {
        get
        {
            Trace.Append("L,");
            if (Fault == 2)
                throw new IOException("length");
            return Body.Length;
        }
    }

    public override long Position { get => PositionValue; set => throw new NotSupportedException(); }

    public override int Read(byte[] b, int o, int n)
    {
        Trace.Append("R" + n + ",");
        if (Fault == 3 || (Fault == 7 && ++Reads == 2))
            throw new IOException("read");
        int count = Math.Min(n, Body.Length - PositionValue);
        if (Fault == 7)
            count = Math.Min(count, 2);
        Encoding.ASCII.GetBytes(Body.AsSpan(PositionValue, count), b.AsSpan(o, count));
        PositionValue += count;
        return count;
    }

    protected override void Dispose(bool d)
    {
        Trace.Append("C,");
    }

    public override void Flush()
    {
    }

    public override long Seek(long x, SeekOrigin o) => throw new NotSupportedException();
    public override void SetLength(long x) => throw new NotSupportedException();
    public override void Write(byte[] b, int o, int n) => throw new NotSupportedException();
}

class Resolver : XmlResolver
{
    public StringBuilder Trace = new();
    public int Mode, Entities;
    public Action OnEntity;
    public string Body = "<a/>";
    public override ICredentials Credentials
    {
        set
        {
        }
    }

    public override object GetEntity(Uri u, string role, Type t)
    {
        Trace.Append("G,");
        OnEntity?.Invoke();
        ++Entities;
        if (Mode == 4)
            return null;
        if (Mode == 1)
            throw new IOException("entity");
        return new ProbeStream
        {
            Trace = Trace,
            Body = Body,
            Fault = (Mode == 5 ? (Entities == 1 ? 2 : 0) : Mode == 6 ? (Entities == 1 ? 3 : 0) : Mode)
        };
    }
}

class Program
{
    static string Err(Exception e) => "E[" + e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" + string.Concat(e.Message.Select(c => ((int)c).ToString("x4"))) + "]";
    static void Main()
    {
        for (int mode = 0; mode < 8; mode++)
            for (int action = 0; action < 5; action++)
            {
                var resolver = new Resolver
                {
                    Mode = mode
                };
                var reader = new XmlTextReader("file:///probe.xml");
                reader.XmlResolver = resolver;
                string states = "";
                for (int j = 0; j < action; j++)
                {
                    try
                    {
                        states += reader.Read() ? "T" : "F";
                    }
                    catch (Exception e)
                    {
                        states += Err(e);
                    }

                    states += (int)reader.ReadState + ",";
                }

                reader.Close();
                Console.WriteLine($"{mode}:{action}:{states}:U,S,{resolver.Trace}");
            }

        for (int mode = 0; mode < 8; mode++)
            for (int body = 0; body < 7; body++)
            {
                var resolver = new Resolver
                {
                    Mode = mode,
                    Body = body == 0 ? "<a/>" : body == 1 ? "<a/><b/>" : body == 2 ? "<a><b/></a><" : body == 3 ? "<a><b/>" : body == 4 ? "<!DOCTYPE a [<!ENTITY e 'value'><!ATTLIST a x CDATA 'default'>]><a>&e;</a>" : body == 5 ? "<!DOCTYPE a [<!ENTITY % p '<!ELEMENT a ANY>'>%p;<!ATTLIST a x NMTOKENS ' a  b '>]><a/>" : "<!DOCTYPE a [<!ELEMENT a (b|)>]><a/>"
                };
                var doc = new XmlDocument();
                doc.LoadXml("<old/>");
                doc.XmlResolver = resolver;
                resolver.OnEntity = () => resolver.Trace.Append(doc.DocumentElement == null ? "EMPTY," : "OLD,");
                string status = "OK";
                try
                {
                    doc.Load("file:///probe.xml");
                }
                catch (Exception e)
                {
                    status = Err(e);
                }

                string meta = "[";
                if (doc.DocumentElement != null)
                    foreach (XmlAttribute a in doc.DocumentElement.Attributes)
                        meta += a.Name + "=" + string.Concat(a.Value.Select(c => ((int)c).ToString("x4"))) + "/" + a.Specified + ";";
                meta += "]";
                Console.WriteLine($"D{mode}:{body}:{status}:{doc.DocumentElement?.Name ?? "-"}{meta}:U,S,{resolver.Trace}");
            }
    }
}
