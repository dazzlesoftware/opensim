using System;
using System.Collections;
using System.Collections.Generic;
using System.Text;
using System.Xml.Linq;
using System.Runtime.CompilerServices;
using Nwc.XmlRpc;
static class Host
{
    public static int Level;
    public static string Logs;
    public static string H(string s)
    {
        if (s == null)
            return "-";
        if (s.Length == 0)
            return ".";
        string o = "";
        foreach (char c in s)
            o += ((int)c & 255).ToString("x2") + ((int)c >> 8).ToString("x2");
        return o;
    }
    static string D(string h)
    {
        if (h == "-")
            return null;
        if (h == ".")
            return "";
        var bytes = Convert.FromHexString(h);
        var chars = new char[bytes.Length / 2];
        for (int i = 0; i < chars.Length; i++)
            chars[i] = (char)(bytes[2 * i] | bytes[2 * i + 1] << 8);
        return new string(chars);
    }
    public static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var a = line.Split(' ');
            int mode = int.Parse(a[0]), id = int.Parse(a[2]);
            Level = int.Parse(a[1]);
            var text = D(a[3]);
            Logs = "";
            string result;
            try
            {
                if (mode == 2)
                {
                    if (id == 0)
                        WebUtil.LogOutgoingDetail("SEND", id, text);
                    else
                        WebUtil.LogResponseDetail(id, text);
                    result = "L:" + H(Logs);
                }
                else
                {
                    if (mode == 1)
                        text = new XmlRpcRequest("method", new ArrayList { text }).ToString();
                    else if (mode == 3 || mode == 4)
                    {
                        var r = new XmlRpcResponse();
                        r.Value = mode == 3
                                      ? (object)text
                                      : new ArrayList { text,
                                                        new ArrayList { true, 47, 3.5,
                                                                        new DateTime(638448068960000000L),
                                                                        new byte[] { 0, 255 } },
                                                        new Hashtable { { "name", text } } };
                        text = r.ToString();
                    }
                    else if (mode == 5)
                        text = new XmlRpcRequest(text, new ArrayList { 42 }).ToString();
                    result = "X:" + H(XElement.Parse(text).ToString(SaveOptions.DisableFormatting));
                }
            }
            catch (Exception e)
            {
                result = "E:" + e.GetType().FullName + ":" + H(e.Message);
            }
            Console.WriteLine(result);
        }
    }
}
class Logger
{
    public void Debug(string s)
    {
        Host.Logs += s;
    }
}
static partial class WebUtil
{
    static Logger m_log = new();
    public static int DebugLevel => Host.Level;
    public const int MaxRequestDiagLength = 200;
}
static partial class Util
{
}
