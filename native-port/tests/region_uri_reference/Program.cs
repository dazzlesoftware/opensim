/*
 * Copyright (c) Contributors, http://opensimulator.org/
 * See CONTRIBUTORS.TXT for a full list of copyright holders.
 *
 * - Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 * - Neither the name of the openmetaverse.org nor the names
 *   of its contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

using System;
using System.Net;
using System.Text;
using OpenSim.Framework;
namespace OpenSim.Framework
{
    public class GridInfo
    {
        public int IsLocalGrid(string uri) => uri.Contains("local.example") ? 1 : 0;
    }
    public static class Util
    {
        public static IPAddress GetHostFromDNS(string host) => host == "bad.invalid"
                                                                   ? null
                                                                   : IPAddress.Parse("203.0.113.7");
    }
}
static class Program
{
    static string Hex(string value) => Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();
    static string Text(string value) => value == "N" ? null
                                                     : Encoding.UTF8.GetString(Convert.FromHexString(value));
    static string Info(RegionURI uri) => string.Join("|", (int)uri.Flags, uri.Schema, uri.Host, uri.Port,
                                                     uri.RegionName, uri.Username, uri.UserPass, uri.X, uri.Y,
                                                     uri.Z, uri.originalURI ?? "NULL",
                                                     uri.IP?.ToString() ?? "NULL", uri.IsValid? 1: 0,
                                                     uri.HostUrl, uri.HostUrlEndSlash, uri.RegionUrlAndName,
                                                     uri.RegionHostPortSpaceName, uri.IsLocalGrid? 1: 0);
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            try
            {
                var fields = line.Split('|');
                int mode = int.Parse(fields[0]);
                var original = Text(fields[1]);
                var uri = mode == 1 ? new RegionURI(original, new GridInfo()) : new RegionURI(original);
                if (mode == 2)
                    uri.Parse(Text(fields[2]));
                if (mode == 3)
                    uri.IsLocalGrid = false;
                if (mode == 4)
                    uri.IsLocalGrid = true;
                if (mode == 5)
                    uri.ResolveDNS();
                Console.WriteLine(Hex(Info(uri)));
            }
            catch
            {
                Console.WriteLine(Hex("EX"));
            }
        }
    }
}
