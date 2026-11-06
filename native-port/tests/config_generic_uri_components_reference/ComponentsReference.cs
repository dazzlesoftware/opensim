using System;
using System.Text;
using System.Collections.Generic;
class Program
{
    static string D(string s)
    {
        if (s == "-")
            return "";
        var b = new StringBuilder();
        for (int i = 0; i < s.Length; i += 4)
            b.Append((char)Convert.ToInt32(s.Substring(i, 4), 16));
        return b.ToString();
    }
    static string H(string s)
    {
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string E(Exception e) => "E:" + e.GetType().FullName + ":" + e.HResult + ":" + H(e.Message) + ":" +
                                    (e is ArgumentException a && a.ParamName != null ? H(a.ParamName) : "~") +
                                    ":" +
                                    (e is ArgumentOutOfRangeException r && r.ActualValue != null
                                         ? r.ActualValue.ToString()
                                         : "~");
    static void Main()
    {
        string? row;
        while ((row = Console.ReadLine()) != null)
        {
            var ids = new Dictionary<object, int>(ReferenceEqualityComparer.Instance);
            string R(string s)
            {
                if (!ids.TryGetValue(s, out int id))
                {
                    id = ids.Count + 1;
                    ids.Add(s, id);
                }
                return id + "," + H(s);
            }
            var input = D(row);
            R(input);
            try
            {
                var u = new Uri(input, UriKind.Absolute);
                Console.Write("A");
                foreach (char c in "phiduaqotalfsphiduaq")
                {
                    Console.Write("|" + c + ":");
                    try
                    {
                        if (c == 'p')
                        {
                            Console.Write(u.Port);
                            continue;
                        }
                        Console.Write(R(c switch { 'h' => u.Host, 'i' => u.IdnHost, 'd' => u.DnsSafeHost,
                                                   'u' => u.UserInfo, 'a' => u.AbsolutePath, 'q' => u.Query,
                                                   'o' => u.OriginalString, 't' => u.ToString(),
                                                   'l' => u.LocalPath, 'f' => u.Fragment,
                                                   _ => u.Scheme }));
                    }
                    catch (Exception e)
                    {
                        Console.Write(E(e));
                    }
                }
            }
            catch (Exception e)
            {
                Console.Write(E(e));
            }
            Console.WriteLine();
        }
    }
}