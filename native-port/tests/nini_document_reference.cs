using System;
using System.IO;
using System.Text;
using System.Collections;
using System.Collections.Generic;
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
            a[i] = (char)(Convert.ToInt32(s.Substring(i * 4, 2), 16) | (Convert.ToInt32(s.Substring(i * 4 + 2, 2), 16) << 8));
        return new string (a);
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

    static string Error(Exception e) => e.GetType().FullName + ":" + unchecked((uint)e.HResult) + ":" + Hex(e.Message) + ":" + Hex((e as ArgumentException)?.ParamName);
    class Reader : TextReader
    {
        P p;
        string text;
        int pos;
        bool closed;
        public Reader(P p, string t)
        {
            this.p = p;
            text = t;
        }

        public override int Peek()
        {
            p.Event(3, "P");
            if (closed)
                throw new ObjectDisposedException(null, "Cannot read from a closed TextReader.");
            return pos < text.Length ? text[pos] : -1;
        }

        public override int Read()
        {
            p.Event(2, "R");
            if (closed)
                throw new ObjectDisposedException(null, "Cannot read from a closed TextReader.");
            return pos < text.Length ? text[pos++] : -1;
        }

        public override void Close()
        {
            p.Event(4, "RC");
            closed = true;
        }
    }

    class Writer : TextWriter
    {
        P p;
        int mutation;
        bool closed;
        public Writer(P p, int m)
        {
            this.p = p;
            mutation = m;
        }

        public override Encoding Encoding => Encoding.Unicode;

        public override void Write(string t)
        {
            p.Event(1, "W:" + Hex(t));
            int m = mutation;
            mutation = 0;
            p.Mutate(m);
            if (closed)
                throw new ObjectDisposedException(null, "Cannot write to a closed TextWriter.");
        }

        public override void Flush() => p.Event(6, "F");
        public override void Close()
        {
            p.Event(5, "WC");
            closed = true;
        }
    }

    class Item : IniItem
    {
        public Item(string n, string v, IniType t, string c) : base(n, v, t, c)
        {
        }
    }

    class P
    {
        public IniDocument doc = new();
        IniSectionCollection collection;
        IniSection section;
        IniItem item;
        List<IniSection> sections = new();
        IEnumerator enumerator;
        int point, nth, hit;
        public bool recording = true;
        List<string> trace = new();
        public P()
        {
            collection = doc.Sections;
        }

        public void Event(int x, string s)
        {
            if (!recording)
                return;
            trace.Add(s);
            if (point == x && ++hit == nth)
                throw new Exception("fault");
        }

        void Load(string text, bool ir = false)
        {
            if (text == null)
            {
                if (ir)
                    doc.Load((IniReader)null);
                else
                    doc.Load((TextReader)null);
                return;
            }

            var r = new Reader(this, text);
            if (ir)
            {
                var ini = new IniReader(r);
                GC.SuppressFinalize(ini);
                ini.IgnoreComments = true;
                ini.AcceptNoAssignmentOperator = true;
                doc.Load(ini);
            }
            else
                doc.Load(r);
        }

        public void Mutate(int m)
        {
            if (m == 1)
                collection.Add(new IniSection("later"));
            else if (m == 2)
            {
                if (collection.Count > 0)
                    collection[0].Set("added", "value");
            }
            else if (m == 3)
            {
                if (collection.Count > 0)
                    collection.Remove(collection[0].Name);
            }
            else if (m == 4)
                Load(";added\n");
            else if (m == 5)
            {
                if (collection.Count > 0 && collection[0].ItemCount > 0)
                    collection[0].GetItem(0).Type = IniType.Empty;
            }
            else if (m == 6)
                Load("[replacement]\nx=y\n");
            else if (m == 7)
                doc.FileType = IniFileType.PythonStyle;
            else if (m == 8)
            {
                if (collection.Count > 0 && collection[0].ItemCount > 0)
                    collection[0].Remove(collection[0].GetItem(0).Name);
            }
            else if (m == 9)
                doc.Save(new Writer(this, 0));
        }

        string Snapshot()
        {
            var o = new StringBuilder(((int)doc.FileType) + "{");
            for (int i = 0; i < collection.Count; i++)
            {
                var s = collection[i];
                o.Append(Hex(s.Name)).Append(':').Append(Hex(s.Comment)).Append('[');
                for (int j = 0; j < s.ItemCount; j++)
                {
                    var t = s.GetItem(j);
                    o.Append((int)t.Type).Append(':').Append(Hex(t.Name)).Append(':').Append(Hex(t.Value)).Append(':').Append(Hex(t.Comment)).Append(';');
                }

                o.Append(']');
            }

            o.Append("}/S:");
            if (section != null)
            {
                o.Append(Hex(section.Name)).Append(':').Append(Hex(section.Comment)).Append('[');
                for (int j = 0; j < section.ItemCount; j++)
                {
                    var t = section.GetItem(j);
                    o.Append((int)t.Type).Append(':').Append(Hex(t.Name)).Append(':').Append(Hex(t.Value)).Append(':').Append(Hex(t.Comment)).Append(';');
                }

                o.Append(']');
            }
            else
                o.Append('-');
            o.Append("/I:");
            if (item != null)
                o.Append((int)item.Type).Append(':').Append(Hex(item.Name)).Append(':').Append(Hex(item.Value)).Append(':').Append(Hex(item.Comment));
            else
                o.Append('-');
            return o.ToString();
        }

        public string Run(string row)
        {
            var args = row.Split(' ');
            doc.FileType = (IniFileType)int.Parse(args[0]);
            point = int.Parse(args[1]);
            nth = int.Parse(args[2]);
            var output = new List<string>();
            foreach (var token in args[3].Split(','))
            {
                var a = token.Split(':');
                string op = a[0], r = op;
                try
                {
                    switch (op)
                    {
                        case "load":
                        case "ir":
                            Load(Dec(a[1]), op == "ir");
                            break;
                        case "ft":
                            doc.FileType = (IniFileType)int.Parse(a[1]);
                            break;
                        case "new":
                            section = new IniSection(Dec(a[1]), Dec(a[2]));
                            sections.Add(section);
                            break;
                        case "add":
                            collection.Add(a[1] == "-" ? null : sections[int.Parse(a[1])]);
                            break;
                        case "sel":
                            section = collection[int.Parse(a[1])];
                            break;
                        case "find":
                            r = collection[Dec(a[1])] != null ? "found" : "missing";
                            break;
                        case "remove":
                            collection.Remove(Dec(a[1]));
                            break;
                        case "set":
                            section.Set(Dec(a[1]), Dec(a[2]), Dec(a[3]));
                            break;
                        case "empty":
                            section.Set(Dec(a[1]));
                            break;
                        case "rm":
                            section.Remove(Dec(a[1]));
                            break;
                        case "get":
                            r = "V:" + Hex(section.GetValue(Dec(a[1])));
                            break;
                        case "contains":
                            r = section.Contains(Dec(a[1])) ? "true" : "false";
                            break;
                        case "keys":
                            r = "K";
                            foreach (var k in section.GetKeys())
                                r += ":" + Hex(k);
                            break;
                        case "item":
                            item = section.GetItem(int.Parse(a[1]));
                            break;
                        case "makeitem":
                            item = new Item(Dec(a[1]), Dec(a[2]), (IniType)int.Parse(a[3]), Dec(a[4]));
                            break;
                        case "identity":
                            bool same = true;
                            var keys = section.GetKeys();
                            int keyIndex = 0;
                            for (int j = 0; j < section.ItemCount; j++)
                            {
                                var t = section.GetItem(j);
                                if (t.Type == IniType.Key)
                                    same = same && Object.ReferenceEquals(keys[keyIndex++], t.Name);
                                same = same && Object.ReferenceEquals(section.GetValue(t.Name), t.Value);
                            }

                            r = same ? "same" : "bad";
                            break;
                        case "iv":
                            item.Value = Dec(a[1]);
                            break;
                        case "ic":
                            item.Comment = Dec(a[1]);
                            break;
                        case "it":
                            item.Type = (IniType)int.Parse(a[1]);
                            break;
                        case "save":
                            doc.Save(new Writer(this, int.Parse(a[1])));
                            break;
                        case "save-null":
                            doc.Save((TextWriter)null);
                            break;
                        case "enum":
                            enumerator = collection.GetEnumerator();
                            break;
                        case "next":
                            r = enumerator.MoveNext() ? "true" : "false";
                            break;
                        case "reset":
                            enumerator.Reset();
                            break;
                        case "current":
                            var e = (DictionaryEntry)enumerator.Current;
                            r = "E:" + Hex((string)e.Key) + ":" + Hex(((IniSection)e.Value).Name);
                            break;
                        case "copy":
                            Array ar = null;
                            if (a[1] != "-")
                            {
                                int type = int.Parse(a[1]), rank = int.Parse(a[2]), lower = int.Parse(a[3]), len = int.Parse(a[4]);
                                Type t = type == 0 ? typeof(DictionaryEntry) : type == 1 ? typeof(object) : type == 2 ? typeof(IniSection) : typeof(string);
                                if (rank == 1)
                                    ar = Array.CreateInstance(t, new[] { len }, new[] { lower });
                                else
                                    ar = Array.CreateInstance(t, new[] { len, 1 }, new[] { lower, 0 });
                            }

                            if (ar is IniSection[] typed)
                                collection.CopyTo(typed, int.Parse(a[5]));
                            else
                                collection.CopyTo(ar, int.Parse(a[5]));
                            var entries = new List<string>();
                            foreach (var v in ar)
                                if (v is DictionaryEntry de && de.Value != null)
                                    entries.Add(Hex((string)de.Key) + ":" + Hex(((IniSection)de.Value).Name));
                            entries.Sort(StringComparer.Ordinal);
                            r = "A";
                            foreach (var entry in entries)
                                r += ":" + entry;
                            break;
                        case "sync":
                            r = Object.ReferenceEquals(collection.SyncRoot, doc.Sections.SyncRoot) && !collection.IsSynchronized ? "same" : "bad";
                            break;
                        default:
                            throw new Exception("operation");
                    }
                }
                catch (Exception e)
                {
                    r = "EX:" + Error(e);
                }

                output.Add(r + "/" + Snapshot());
            }

            return string.Join("|", output) + "/IO:" + string.Join(";", trace);
        }
    }

    class FinalWriter : TextWriter
    {
        public int closes;
        public bool fail;
        public override Encoding Encoding => Encoding.Unicode;

        public override void Write(string text)
        {
            if (fail)
                throw new Exception("write");
        }

        public override void Close()
        {
            closes++;
        }
    }

    [System.Runtime.CompilerServices.MethodImpl(System.Runtime.CompilerServices.MethodImplOptions.NoInlining)]
    static void RunSave(IniDocument d, FinalWriter w)
    {
        try
        {
            d.Save(w);
        }
        catch (Exception)
        {
        }
    }

    static void FinalizerProof()
    {
        var d = new IniDocument();
        d.Sections.Add(new IniSection("a"));
        var ok = new FinalWriter();
        RunSave(d, ok);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        var bad = new FinalWriter
        {
            fail = true
        };
        RunSave(d, bad);
        GC.Collect();
        GC.WaitForPendingFinalizers();
        GC.Collect();
        Console.WriteLine(ok.closes + ":" + bad.closes);
        GC.KeepAlive(d);
    }

    static void Main(string[] args)
    {
        if (args.Length > 0 && args[0] == "--finalizer")
        {
            FinalizerProof();
            return;
        }

        System.Globalization.CultureInfo.CurrentCulture = System.Globalization.CultureInfo.InvariantCulture;
        System.Globalization.CultureInfo.CurrentUICulture = System.Globalization.CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            var p = new P();
            bool nogc = GC.TryStartNoGCRegion(4 * 1024 * 1024);
            try
            {
                Console.WriteLine(p.Run(row));
            }
            finally
            {
                p.recording = false;
                if (nogc)
                    GC.EndNoGCRegion();
            }
        }
    }
}
