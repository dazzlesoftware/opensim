using System;
using System.Text;
using System.Globalization;
class Program
{
    static string D(string s)
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
    static string H(string s)
    {
        if (s == null)
            return "~";
        var b = new StringBuilder();
        foreach (char c in s)
            b.Append(((int)c).ToString("x4"));
        return b.Length == 0 ? "-" : b.ToString();
    }
    static string Error(Exception e) => e.GetType().FullName + ":" +
                                        unchecked((uint)e.HResult).ToString("x8") + ":" + H(e.Message) + ":" +
                                        (e is ArgumentException a ? H(a.ParamName) : "~");
    static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string row;
        while ((row = Console.ReadLine()) != null)
        {
            var a = row.Split(' ');
            var b = new StringBuilder();
            try
            {
                Uri uri = (a[0] == "R" || a[0] == "RS") ? new Uri(new Uri(D(a[1]), UriKind.Absolute), D(a[2]))
                                                        : new Uri(D(a[1]), UriKind.Absolute);
                b.Append("ok");
                if (a[0] == "RS")
                {
                    b.Append('/' + H(uri.OriginalString) + '/' + H(uri.Scheme));
                    Console.WriteLine(b.ToString());
                    continue;
                }
                foreach (Func<string> f in new Func<string>[] {
                             () => uri.OriginalString, () => uri.Scheme, () => uri.Host, () => uri.IdnHost,
                             () => uri.DnsSafeHost, () => uri.Port.ToString(CultureInfo.InvariantCulture),
                             () => uri.UserInfo, () => uri.AbsolutePath, () => uri.Query, () => uri.Fragment,
                             () => uri.AbsoluteUri
                         })
                {
                    try
                    {
                        b.Append('/' + H(f()));
                    }
                    catch (Exception e)
                    {
                        b.Append('/' + Error(e));
                    }
                }
            }
            catch (Exception e)
            {
                b.Append(Error(e));
            }
            Console.WriteLine(b.ToString());
        }
    }
}
