using System;
using System.IO;
using System.Text;
using System.Collections.Generic;
using System.Reflection;
using Nini.Ini;
class Program
{
    static string Dec(string s)
    {
        if (s == "-")
            return null;
        if (s == ".")
            return "";
        var a = new char[s.Length / 4];
        for (int i = 0; i < a.Length; i++)
            a[i] = (char)(Convert.ToInt32(s.Substring(i * 4, 2), 16) |
                          (Convert.ToInt32(s.Substring(i * 4 + 2, 2), 16) << 8));
        return new string(a);
    }
    static string Hex(string s)
    {
        if (s == null)
            return "-";
        if (s.Length == 0)
            return ".";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((byte)c).ToString("x2")).Append(((byte)(c >> 8)).ToString("x2"));
        return b.ToString();
    }
    static string Error(Exception e) => e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" +
                                        Hex(e.Message) + ":" + Hex((e as ArgumentException)?.ParamName);
    class Sink
    {
        public P p;
        public bool closed;
        public string returned = "writer", nl = Environment.NewLine;
        public StringBuilder buffer = new();
        List<string> seen = new();
        public Sink(P p)
        {
            this.p = p;
        }
        public void Write(string s)
        {
            int i = seen.FindIndex(x => Object.ReferenceEquals(x, s));
            if (i < 0)
            {
                seen.Add(s);
                i = seen.Count - 1;
            }
            p.Event(1, "W" + (i + 1) + ":" + Hex(s));
            if (closed)
                throw new ObjectDisposedException(null, "Cannot write to a closed TextWriter.");
            buffer.Append(s);
        }
        public void Flush() => p.Event(2, "F");
        public void Close()
        {
            p.Event(3, "C");
            closed = true;
        }
        public string Text()
        {
            p.Event(4, "T");
            return returned;
        }
        public string Line
        {
            get {
                p.Event(7, "NG");
                return nl;
            }
            set {
                p.Event(8, "NS:" + Hex(value));
                nl = value ?? Environment.NewLine;
            }
        }
    }
    class Track : MemoryStream
    {
        P p;
        public bool closed;
        public Track(P p)
        {
            this.p = p;
        }
        public override void Close()
        {
            p.Event(5, "S");
            closed = true;
            base.Close();
        }
    }
    class Writer : TextWriter
    {
        public Sink s;
        public Writer(P p)
        {
            s = new Sink(p);
        }
        public override Encoding Encoding => Encoding.Unicode;
        public override void Write(string t) => s.Write(t);
        public override void Flush() => s.Flush();
        public override void Close() => s.Close();
        public override string ToString() => s.Text();
        public override string NewLine
        {
            get => s.Line;
            set => s.Line = value;
        }
    }
    class SW : StreamWriter
    {
        public Sink s;
        public SW(P p, Track t) : base(t)
        {
            s = new Sink(p);
        }
        public override void Write(string t) => s.Write(t);
        public override void Flush() => s.Flush();
        public override void Close() => s.Close();
        public override string ToString() => s.Text();
        public override string NewLine
        {
            get => s.Line;
            set => s.Line = value;
        }
    }
    class Exposed : IniWriter
    {
        P p;
        public Exposed(P p, TextWriter t) : base(t)
        {
            this.p = p;
        }
        protected override void Dispose(bool d)
        {
            p.Event(6, d ? "H1" : "H0");
            if (p.mode != 1)
                base.Dispose(d);
            if (p.mode == 2)
                throw new Exception("after");
        }
        public void FinalizeNow() => Dispose(false);
    }
    class P
    {
        public int point, nth, hit, mode;
        public List<string> trace = new();
        public Track stream;
        TextWriter backend;
        StringWriter real;
        StringBuilder external;
        Sink sink;
        Exposed w;
        string saved = "-";
        public void Event(int x, string s)
        {
            trace.Add(s);
            if (point == x && ++hit == nth)
                throw new Exception("fault");
        }
        string Snapshot()
        {
            bool disposed = (bool)typeof(IniWriter)
                                .GetField("disposed", BindingFlags.NonPublic | BindingFlags.Instance)
                                .GetValue(w);
            return (int)w.WriteState + "," + w.Indentation + "," + (w.UseValueQuotes ? "1" : "0") + "," +
                   (int)w.CommentDelimiter + "," + (int)w.AssignDelimiter + "," + (disposed ? "1" : "0") +
                   "," + (stream != null && Object.ReferenceEquals(w.BaseStream, stream) ? "1" : "0") +
                   "," +
                   (sink == null  ? "-"
                    : sink.closed ? "1"
                                  : "0") +
                   "," +
                   (stream == null  ? "-"
                    : stream.closed ? "1"
                                    : "0") +
                   "," +
                   Hex(real != null ? real.GetStringBuilder().ToString() : sink?.buffer.ToString() ?? "") +
                   "," +
                   (real == null                                                ? "-"
                    : Object.ReferenceEquals(real.GetStringBuilder(), external) ? "1"
                                                                                : "0") +
                   "," + Hex(sink != null ? sink.nl : real?.NewLine);
        }
        public string Run(string line)
        {
            var args = line.Split(' ');
            int kind = int.Parse(args[0]);
            point = int.Parse(args[1]);
            nth = int.Parse(args[2]);
            mode = int.Parse(args[3]);
            if (kind == 0)
            {
                var b = new Writer(this);
                sink = b.s;
                backend = b;
            }
            else if (kind == 1)
            {
                stream = new Track(this);
                var b = new SW(this, stream);
                sink = b.s;
                backend = b;
            }
            else if (kind == 2)
            {
                external = new StringBuilder("seed");
                real = new StringWriter(external);
                backend = real;
            }
            else if (kind == 4)
            {
                real = new StringWriter();
                external = real.GetStringBuilder();
                backend = real;
            }
            w = new Exposed(this, backend);
            GC.SuppressFinalize(w);
            var output = new List<string>();
            foreach (var token in args[4].Split(','))
            {
                var a = token.Split(':');
                string op = a[0], r = op;
                try
                {
                    switch (op)
                    {
                    case "s":
                        w.WriteSection(Dec(a[1]));
                        break;
                    case "sc":
                        w.WriteSection(Dec(a[1]), Dec(a[2]));
                        break;
                    case "k":
                        w.WriteKey(Dec(a[1]), Dec(a[2]));
                        break;
                    case "kc":
                        w.WriteKey(Dec(a[1]), Dec(a[2]), Dec(a[3]));
                        break;
                    case "e":
                        w.WriteEmpty();
                        break;
                    case "ec":
                        w.WriteEmpty(Dec(a[1]));
                        break;
                    case "i":
                        w.Indentation = int.Parse(a[1]);
                        break;
                    case "v":
                        w.UseValueQuotes = a[1] == "1";
                        break;
                    case "a":
                        w.AssignDelimiter = (char)int.Parse(a[1]);
                        break;
                    case "m":
                        w.CommentDelimiter = (char)int.Parse(a[1]);
                        break;
                    case "c":
                        w.Close();
                        break;
                    case "f":
                        w.Flush();
                        break;
                    case "d":
                        w.Dispose();
                        break;
                    case "z":
                        w.FinalizeNow();
                        break;
                    case "h":
                        mode = int.Parse(a[1]);
                        break;
                    case "t":
                        var t = w.ToString();
                        r = "T:" + Hex(t) + "@" +
                            (sink != null && Object.ReferenceEquals(t, sink.returned) ? "R" : "N");
                        break;
                    case "ret":
                        if (sink != null)
                            sink.returned = Dec(a[1]);
                        break;
                    case "nl":
                        if (backend != null)
                            backend.NewLine = Dec(a[1]);
                        break;
                    case "ng":
                        r = "N:" + Hex(backend?.NewLine);
                        break;
                    case "b":
                        external?.Append(Dec(a[1]));
                        break;
                    case "bx":
                        external?.Clear();
                        break;
                    case "bs":
                        external = new StringBuilder(Dec(a[1]));
                        break;
                    case "bc":
                        backend?.Close();
                        break;
                    case "bw":
                        backend?.Write(Dec(a[1]));
                        break;
                    case "sf":
                        stream?.Close();
                        break;
                    case "q":
                        r = saved;
                        break;
                    default:
                        throw new Exception("operation");
                    }
                }
                catch (Exception e)
                {
                    saved = Error(e);
                    r = "EX:" + saved;
                }
                output.Add(r + "/" + Snapshot() + "/" + saved);
            }
            return string.Join("|", output) + "/IO:" + string.Join(";", trace);
        }
    }
    static void Main()
    {
        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
            Console.WriteLine(new P().Run(line));
    }
}
