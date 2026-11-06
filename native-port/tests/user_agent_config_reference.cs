using System;
using System.IO;
using System.Net;
using System.Globalization;
using System.Collections.Generic;
using System.Text;
using Nini.Config;
using OpenSim.Framework;
namespace log4net
{
    public interface ILog
    {
        void Error(string s);
        void ErrorFormat(string f, params object[] args);
        void DebugFormat(string f, params object[] args);
    }
    class Logger : ILog
    {
        public void Error(string s)
        {
            ConfigHost.Hit("error:" + s);
            if ((ConfigHost.Flags & 2) != 0)
                throw new InvalidOperationException("error");
        }
        public void ErrorFormat(string f, params object[] args) => Error(string.Format(f, args));
        public void DebugFormat(string f, params object[] args)
        {
            ConfigHost.Hit("debug:" + string.Format(f, args));
            if ((ConfigHost.Flags & 4) != 0)
                throw new InvalidOperationException("debug");
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type t) => new Logger();
    }
}
namespace OpenSim.Framework
{
    public static partial class Util
    {
        static log4net.ILog m_log = log4net.LogManager.GetLogger(typeof(Util));
        public static IPAddress GetHostFromDNS(string host)
        {
            ConfigHost.Hit("dns:" + host);
            if ((ConfigHost.Flags & 8) != 0)
                throw new Exception("dns");
            return host == "bad.invalid" ? null : IPAddress.Parse("203.0.113.7");
        }
    }
}
static class ConfigHost
{
    public static int Flags;
    static List<string> trace = new();
    public static void Hit(string t) => trace.Add(t);
    static string H(string t)
    {
        if (t == null)
            return "-";
        if (t.Length == 0)
            return ".";
        var outp = new StringBuilder();
        foreach (char c in t)
            outp.Append(((int)c & 255).ToString("x2")).Append(((int)c >> 8).ToString("x2"));
        return outp.ToString();
    }
    static string D(string s)
    {
        if (s == "-")
            return null;
        if (s == ".")
            return "";
        var bytes = Convert.FromHexString(s);
        var chars = new char[bytes.Length / 2];
        for (int i = 0; i < chars.Length; i++)
            chars[i] = (char)(bytes[i * 2] | bytes[i * 2 + 1] << 8);
        return new string(chars);
    }
    public static void Main()
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var input = line.Split(' ');
            Flags = int.Parse(input[0]);
            trace.Clear();
            string url = null, status = "OK";
            try
            {
                IConfigSource source = null;
                var data = D(input[1]);
                if (data != null)
                {
                    var ini = new IniConfigSource();
                    ini.CaseSensitive = (Flags & 1) != 0;
                    ini.Load(new StringReader(data));
                    source = ini;
                }
                var connector = new ConfigConnector(source);
                url = connector.URL;
            }
            catch (Exception e)
            {
                status = "E:" + e.GetType().FullName + ":" + H(e.Message);
            }
            Console.WriteLine(status + "|" + H(url) + "|" + H(string.Join("\n", trace)));
        }
    }
}
