/*
Copyright (c) Contributors, http://opensimulator.org/
See CONTRIBUTORS.TXT for a full list of copyright holders.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the OpenSimulator Project nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE DEVELOPERS ``AS IS'' AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
using System;
using System.Net;
using System.Text;
using System.Collections.Generic;
using Nini.Config;
using OpenSim.Framework;
namespace log4net
{
    public interface ILog
    {
        void Error(string value);
        void ErrorFormat(string value, params object[] args);
    }
    class Log : ILog
    {
        public void Error(string value)
        {
        }
        public void ErrorFormat(string value, params object[] args)
        {
        }
    }
    public static class LogManager
    {
        public static ILog GetLogger(Type type) => new Log();
    }
}
namespace OpenSim.Framework
{
    public static partial class Util
    {
        static readonly log4net.ILog m_log = log4net.LogManager.GetLogger(typeof(Util));
        public static IPAddress GetHostFromDNS(string host) => host == "bad.invalid"
                                                                   ? null
                                                                   : IPAddress.Parse("203.0.113.7");
    }
}
static class Program
{
    static string Text(string value) => value == "N" ? null
                                                     : Encoding.UTF8.GetString(Convert.FromHexString(value));
    static string Array(string[] value) => value == null ? "NULL" : string.Join(",", value);
    static string Value(string value) => value ?? "NULL";
    static void Main(string[] args)
    {
        try
        {
            var info = new GridInfo(new IniConfigSource(args[0]), Text(args[1]));
            int action = int.Parse(args[2]);
            try
            {
                if (action == 1)
                {
                    info.GridName = "";
                    info.GridName = "New Name";
                    info.GridNick = "";
                    info.GridNick = "NN";
                    info.GridUrl = "https://new.example:8002";
                    info.SearchURL = "http://search.example/new?q=2";
                    info.DestinationGuideURL = "http://bad.invalid";
                    info.EconomyURL = "https://economy.example/path";
                    info.GridUrlAlias = new[] { "added.example", "http://added.example:80", "bad/path" };
                    info.StunServers = new[] { "stun.example:3478", "bad/path", "stun.example:3478" };
                }
                if (action == 2)
                {
                    info.GridUrlAlias = System.Array.Empty<string>();
                    info.StunServers = System.Array.Empty<string>();
                    info.GridUrl = "bad/path";
                    info.SearchURL = "http://bad.invalid";
                    info.GridName = null;
                    info.GridNick = "";
                }
                if (action == 3)
                {
                    info.GridUrlAlias = null;
                }
            }
            catch
            {
                Console.WriteLine("SETTER_ERROR");
                return;
            }
            var rows = new List<string> { info.HasHGConfig ? "1" : "0",
                                          Value(info.GateKeeperURLNoEndSlash),
                                          info.GateKeeperURL,
                                          Value(info.HGGateKeeperURLNoEndSlash),
                                          info.HGGateKeeperURL,
                                          Value(info.HomeURLNoEndSlash),
                                          info.HomeURL,
                                          Value(info.HGHomeURLNoEndSlash),
                                          info.HGHomeURL,
                                          Value(info.GridUrl),
                                          Array(info.GridUrlAlias),
                                          info.GridName,
                                          info.GridNick,
                                          info.SearchURL,
                                          info.DestinationGuideURL,
                                          Value(info.EconomyURL),
                                          Array(info.StunServers) };
            foreach (var value in new[] { null, "", "bad/path", "http://gate.example",
                                          "https://gate.example:80", "alias.example", "home.example",
                                          "http://bad.invalid", "http://unrelated.example", "added.example" })
            {
                rows.Add(info.IsLocalGrid(value).ToString());
                rows.Add(info.IsLocalGrid(value, true).ToString());
                rows.Add(info.IsLocalHome(value).ToString());
                rows.Add(info.IsLocalHome(value, true).ToString());
            }
            Console.WriteLine(
                Convert.ToHexString(Encoding.UTF8.GetBytes(string.Join("|", rows))).ToLowerInvariant());
        }
        catch (Exception error)
        {
            Console.WriteLine("ERROR|" + error.Message);
        }
    }
}
