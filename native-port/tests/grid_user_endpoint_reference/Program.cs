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
using System.Globalization;
using OpenSim.Framework.Servers.HttpServer;
using OpenSim.Framework.ServiceAuth;
using OpenSim.Server.Handlers.GridUser;
using OpenSim.Services.UserAccountService;
static class Program
{
    public static double Now = 0;
    public static int Unix = 1700000000;
    public static Database Database;
    static string Hex(string value) =>
        Convert.ToHexString(Encoding.UTF8.GetBytes(value ?? "")).ToLowerInvariant();
    static void Main(string[] args)
    {
        CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
        Console.InputEncoding = new UTF8Encoding(false);
        Console.OutputEncoding = new UTF8Encoding(false);
        var config = new Nini.Config.IniConfigSource(args[0]);
        config.Configs["GridUserService"].Set("StorageProvider", "fixture");
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            Database = new();
            var fields = line.Split('|');
            var body = Convert.FromHexString(fields[0]);
            var authorization = Encoding.UTF8.GetString(Convert.FromHexString(fields[1]));
            var service = new GridUserService(config);
            var cache = typeof(GridUserService)
                            .GetField("cache", BindingFlags.Static | BindingFlags.NonPublic)
                            .GetValue(null);
            cache.GetType().GetMethod("Clear").Invoke(cache, null);
            service.LoggedIn("00000000-0000-0000-0000-000000000001");
            service.SetHome("00000000-0000-0000-0000-000000000001",
                            new OpenMetaverse.UUID("00000000-0000-0000-0000-000000000003"),
                            new OpenMetaverse.Vector3(128, 64, 70), OpenMetaverse.Vector3.Zero);
            Database.Store(new OpenSim.Data.GridUserData { UserID = "00000000-0000-0000-0000-000000000002",
                                                           Data = new() { ["Login"] = "-2147483648",
                                                                          ["Logout"] = "2147483647" } });
            var handler =
                new GridUserServerPostHandler(service, ServiceAuth.Create(config, "GridUserService"));
            var request = new IOSHttpRequest();
            var response = new IOSHttpResponse();
            if (authorization.Length > 0)
                request.Headers["Authorization"] = authorization;
            if (fields[2] == "1")
                request.Headers["X-SecondLife-Shard"] = "";
            try
            {
                var result = handler.Handle("/griduser", new MemoryStream(body),
                                            request, response);
                var snapshot =
                    handler.Handle("/griduser",
                                   new MemoryStream(Encoding.UTF8.GetBytes(
                                       "METHOD=getgriduserinfo&UserID=00000000-0000-0000-0000-000000000001")),
                                   request, new IOSHttpResponse());
                Console.WriteLine(response.StatusCode + "|" + Convert.ToHexString(result).ToLowerInvariant() +
                                  "|" + Hex(response.ContentType) + "|" +
                                  Hex(response.Headers["WWW-Authenticate"]) + "|" +
                                  Convert.ToHexString(snapshot).ToLowerInvariant());
            }
            catch (FormatException)
            {
                Console.WriteLine("BASE64_FORMAT_ERROR");
            }
        }
    }
}
