using System;
using System.Collections;
using System.Collections.Generic;
using System.Reflection;
using System.Xml.Linq;
using OpenSim.Framework;
namespace Fixture
{
    public static class Host
    {
        public static List<string> Events = new();
        public static string Fail;
        public static int Level, Number, Elapsed, Kind, Mutate;
        public static int Compact;
        public static string Hit(string stage)
        {
            Events.Add(stage);
            if (Fail == stage)
                throw new InvalidOperationException(stage);
            return stage;
        }
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
        public static void Main()
        {
            string line;
            while ((line = Console.ReadLine()) != null)
            {
                var a = line.Split(' ');
                Level = int.Parse(a[0]);
                Number = int.Parse(a[1]);
                Elapsed = int.Parse(a[2]);
                Kind = int.Parse(a[3]);
                Fail =
                    a[4] == "-" ? "-" : System.Text.Encoding.Unicode.GetString(Convert.FromHexString(a[4]));
                Mutate = int.Parse(a[5]);
                Compact = 0;
                Events.Clear();
                string result;
                try
                {
                    var r = XMLRPCRequester.SendRequest(new Hashtable(), "method", "url");
                    result = r == null ? "N" : "M";
                }
                catch (Exception e)
                {
                    result = e.GetType().FullName + ":" + H(e.Message);
                }
                Console.WriteLine(result + "|" + Number + "|" + H(string.Join("\n", Events)));
            }
        }
    }
    public class Client : IDisposable
    {
        public void Dispose()
        {
            Host.Hit("close");
        }
    }
    public class Request
    {
        public Request(string name, ArrayList args)
        {
        }
        public override string ToString() => Host.Hit("request-text");
        public Response Send(string url, Client client)
        {
            Host.Hit("send");
            return Host.Kind == 4 ? null : new Response();
        }
    }
    public class Response
    {
        Nwc.XmlRpc.XmlRpcResponse Inner;
        public Response()
        {
            Inner = new();
            if (Host.Kind == 2)
                Inner.SetFault(9, "fault");
            else if (Host.Kind == 3)
            {
                Inner.SetFault(9, "fault");
                Inner.Value = new Hashtable();
            }
            else
                Inner.Value = Host.Kind == 1 ? (object) "text" : Host.Kind == 5 ? null : new Hashtable();
        }
        public override string ToString() => Host.Hit("response-text");
        public bool IsFault => Host.Kind == 3 || Inner.IsFault;
        public int FaultCode => Host.Kind == 3 ? throw new NullReferenceException() : Inner.FaultCode;
        public string FaultString => Inner.FaultString;
        public object Value => Inner.Value;
    }
    public class Element
    {
        string text;
        Element(string s)
        {
            text = s;
        }
        public static Element Parse(string s)
        {
            Host.Hit("compact" + (++Host.Compact));
            return new Element("compact:" + s);
        }
        public string ToString(SaveOptions o) => text;
    }
}
namespace log4net
{
    public interface ILog
    {
        void Debug(object o);
        void Error(object o);
        void InfoFormat(string f, params object[] args);
    }
    public class Log : ILog
    {
        public void Debug(object o)
        {
            Fixture.Host.Hit("debug:" + o);
        }
        public void Error(object o)
        {
            Fixture.Host.Hit("error:" + o);
        }
        public void InfoFormat(string f, params object[] args)
        {
            Fixture.Host.Hit("info:" + string.Format(f, args));
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type t) => new Log();
    }
}
namespace OpenSim.Framework
{
    public static class WebUtil
    {
        public static int RequestNumber
        {
            get {
                Fixture.Host.Hit("number-get");
                return Fixture.Host.Number;
            }
            set {
                Fixture.Host.Hit("number-set");
                Fixture.Host.Number = value;
            }
        }
        public static int DebugLevel
        {
            get {
                Fixture.Host.Hit("level");
                return Fixture.Host.Level;
            }
        }
        public const int LongCallTime = 3000, MaxRequestDiagLength = 200;
        public static Fixture.Client GetNewGlobalHttpClient(int n)
        {
            Fixture.Host.Hit("client:" + n);
            return new();
        }
        public static void LogOutgoingDetail(string kind, int n, string text)
        {
            Fixture.Host.Hit("out:" + n + ":" + text);
            if (Fixture.Host.Mutate != 0)
                Fixture.Host.Level = Fixture.Host.Mutate;
        }
        public static void LogResponseDetail(int n, string text)
        {
            Fixture.Host.Hit("response:" + n + ":" + text);
        }
    }
    public static class Util
    {
        public static int EnvironmentTickCount()
        {
            Fixture.Host.Hit("tick");
            return 17;
        }
        public static int EnvironmentTickCountSubtract(int n)
        {
            Fixture.Host.Hit("elapsed:" + n);
            return Fixture.Host.Elapsed;
        }
    }
}
