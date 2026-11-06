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
using System.IO;
using System.Text;
using System.Reflection;
using System.Collections.Generic;
using System.Linq;
using OpenMetaverse;
using OpenSim.Data.Null;
using OpenSim.Framework.Servers.HttpServer;
using OpenSim.Framework.ServiceAuth;
using OpenSim.Server.Handlers.Presence;
using OpenSim.Services.PresenceService;
static class Program
{
    public static double Now = 0;
    public static NullPresenceData Database;
    static string Hex(string value) =>
        Convert.ToHexString(Encoding.UTF8.GetBytes(value ?? "")).ToLowerInvariant();
    static void Main(string[] args)
    {
        Console.InputEncoding = new UTF8Encoding(false);
        Console.OutputEncoding = new UTF8Encoding(false);
        var config = new Nini.Config.IniConfigSource(args[0]);
        Database = new("", "");
        config.Configs["PresenceService"].Set("StorageProvider", "fixture");
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            if (args.Length > 1 && args[1] == "--forms")
            {
                var form = OpenSim.Server.Base.ServerUtils.ParseQueryString(
                    Encoding.UTF8.GetString(Convert.FromHexString(line)));
                var rows = new List<string>();
                foreach (var pair in form)
                {
                    var row = Hex(pair.Key) + "=";
                    if (pair.Value is string scalar)
                        row += "S" + Hex(scalar);
                    else
                    {
                        row += "L";
                        foreach (var item in (List<string>)pair.Value)
                            row += Hex(item) + ",";
                    }
                    rows.Add(row);
                }
                rows.Sort(StringComparer.Ordinal);
                Console.WriteLine(string.Join("|", rows));
                continue;
            }
            var fields = line.Split('|');
            byte[] body = Convert.FromHexString(fields[0]);
            string authorization = Encoding.UTF8.GetString(Convert.FromHexString(fields[1]));
            var service = new PresenceService(config);
            foreach (var name in new[] { "BySessionCache", "ByUserCache" })
            {
                var cache = typeof(PresenceService)
                                .GetField(name, BindingFlags.Static | BindingFlags.NonPublic)
                                .GetValue(null);
                cache.GetType().GetMethod("Clear").Invoke(cache, null);
            }
            Database.Delete("UserID", "alpha");
            Database.Delete("UserID", "beta <&> \r\n試験");
            var first = new UUID("00000000-0000-0000-0000-000000000001");
            var second = new UUID("00000000-0000-0000-0000-000000000002");
            service.LoginAgent("alpha", first, UUID.Zero);
            service.ReportAgent(first, new UUID("00000000-0000-0000-0000-000000000003"));
            service.LoginAgent("beta <&> \r\n試験", second, UUID.Zero);
            var handler =
                new PresenceServerPostHandler(service, ServiceAuth.Create(config, "PresenceService"));
            var request = new IOSHttpRequest();
            var response = new IOSHttpResponse();
            if (authorization.Length > 0)
                request.Headers["Authorization"] = authorization;
            if (fields[2] == "1")
                request.Headers["X-SecondLife-Shard"] = "";
            try
            {
                byte[] result = handler.Handle("/presence", new MemoryStream(body),
                                               request, response);
                Console.WriteLine(response.StatusCode + "|" + Convert.ToHexString(result).ToLowerInvariant() +
                                  "|" + Hex(response.ContentType) + "|" +
                                  Hex(response.Headers["WWW-Authenticate"]));
            }
            catch (FormatException)
            {
                Console.WriteLine("BASE64_FORMAT_ERROR");
            }
        }
    }
}
