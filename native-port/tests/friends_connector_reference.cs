using System;
using System.Text;
using System.Xml;
using System.IO;
using System.Collections.Generic;
using OpenMetaverse;
using Nini.Config;
using OpenSim.Services.Connectors.Friends;
namespace log4net
{
    public interface ILog
    {
        void DebugFormat(string f, params object[] a);
        void Error(object a);
        void Debug(object a);
    }
    public class Logger : ILog
    {
        public void DebugFormat(string f, params object[] a)
        {
        }
        public void Error(object a)
        {
        }
        public void Debug(object a)
        {
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type t) => new Logger();
    }
}
namespace OpenSim.Framework.ServiceAuth
{
    public interface IServiceAuth
    {
    }
}
namespace OpenSim.Framework
{
    public static class Util
    {
        public static Encoding UTF8NoBomEncoding = new UTF8Encoding(false);
    }
    public static class SynchronousRestFormsRequester
    {
        public static string MakeRequest(string method, string uri, string body,
                                         OpenSim.Framework.ServiceAuth.IServiceAuth auth) =>
            Program.Send(uri, body);
    }
}
namespace OpenSim.Services.Connectors
{
    public class BaseServiceConnector
    {
        protected OpenSim.Framework.ServiceAuth.IServiceAuth m_Auth;
        protected void Initialise(IConfigSource source, string section)
        {
        }
    }
}
class Program
{
    static string Trace, Reply;
    public static string H(string s) =>
        s == null       ? "-"
        : s.Length == 0 ? "."
                        : Convert.ToHexString(Encoding.UTF8.GetBytes(s)).ToLowerInvariant();
    static string Decode(string s) => s == "-"   ? null
                                      : s == "." ? ""
                                                 : Encoding.UTF8.GetString(Convert.FromHexString(s));
    public static string Send(string uri, string body)
    {
        Trace += H(uri) + ":" + H(body) + ";";
        if (Reply == "#throw")
            throw new Exception();
        return Reply ?? "";
    }
    static void Main(string[] args)
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var p = line.Split(' ');
            int mode = int.Parse(p[0]);
            string principal = Decode(p[1]), friend = Decode(p[2]);
            Reply = Decode(p[3]);
            Trace = "";
            string output;
            try
            {
                var connector = args.Length > 0
                                    ? new FriendsServicesConnector(new IniConfigSource(args[0]))
                                    : new FriendsServicesConnector("http://friends.invalid/base///");
                UUID.TryParse(principal, out UUID id);
                if (mode < 2)
                {
                    var rows = mode == 1 ? connector.GetFriends(principal) : connector.GetFriends(id);
                    output = rows.Length.ToString();
                    foreach (var row in rows)
                        output += "|" + row.PrincipalID + ":" + H(row.Friend) + ":" + row.MyFlags + ":" +
                                  row.TheirFlags;
                }
                else
                    output = (mode == 2   ? connector.StoreFriend(principal, friend, -2147483647)
                              : mode == 3 ? connector.Delete(id, friend)
                                          : connector.Delete(principal, friend))
                                 ? "T"
                                 : "F";
            }
            catch
            {
                output = "E";
            }
            Console.WriteLine(Trace + "|" + output);
        }
    }
}
