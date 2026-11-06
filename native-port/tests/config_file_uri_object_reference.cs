using System;
using System.Linq;
using System.Collections.Generic;
class Program
{
    static string Encode(string s) => string.Concat(s.Select(c => ((int)c).ToString("x4")));
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int attempt = int.Parse(p[0]), kind = int.Parse(p[1]);
            string input = null;
            if (p[2] != "~")
            {
                input = "";
                for (int i = 0; i < p[2].Length; i += 4)
                    input += (char)Convert.ToUInt16(p[2].Substring(i, 4), 16);
            }
            var ids = new Dictionary<object, int>(ReferenceEqualityComparer.Instance);
            int next = 1;
            string Render(string t)
            {
                if (!ids.TryGetValue(t, out int id))
                {
                    id = next++;
                    ids[t] = id;
                }
                return id + "," + Encode(t);
            }
            if (input != null)
                Render(input);
            try
            {
                Uri uri;
                if (attempt != 0)
                {
                    Uri.TryCreate(input, (UriKind)kind, out uri);
                }
                else
                    uri = new Uri(input, (UriKind)kind);
                if (uri == null)
                {
                    Console.WriteLine("N");
                    continue;
                }
                Console.Write(uri.IsAbsoluteUri ? "A" : "R");
                foreach (char c in "otalfssflatolato")
                {
                    try
                    {
                        string text = c switch { 'o' => uri.OriginalString,
                                                 't' => uri.ToString(),
                                                 'a' => uri.AbsoluteUri,
                                                 'l' => uri.LocalPath,
                                                 'f' => uri.Fragment,
                                                 _ => uri.Scheme };
                        Console.Write("|" + c + ":" + Render(text));
                    }
                    catch (Exception e)
                    {
                        Console.Write(
                            $"|{c}:E{e.GetType().FullName},{unchecked((uint)e.HResult)},{Encode(e.Message)},{(e is ArgumentException z&&z.ParamName!=null?Encode(z.ParamName):"-")},{(e is ArgumentOutOfRangeException q&&q.ActualValue!=null?q.ActualValue.ToString():"-")}");
                    }
                }
                Console.WriteLine();
            }
            catch (Exception e)
            {
                Console.WriteLine(
                    $"E{e.GetType().FullName},{unchecked((uint)e.HResult)},{Encode(e.Message)},{(e is ArgumentException x&&x.ParamName!=null?Encode(x.ParamName):"-")}");
            }
        }
    }
}
