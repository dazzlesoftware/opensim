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
using System;using System.Net;using System.Text;using OpenSim.Framework;
namespace OpenSim.Framework {public static class Util {public static IPAddress GetHostFromDNS(string host)=>host=="bad.invalid"?null:IPAddress.Parse("203.0.113.7");}}
static class Program {
 static string Hex(string value)=>Convert.ToHexString(Encoding.UTF8.GetBytes(value)).ToLowerInvariant();static string Text(string value)=>value=="N"?null:Encoding.UTF8.GetString(Convert.FromHexString(value));
 static void Main(){string line;while((line=Console.ReadLine())!=null){try{var fields=line.Split('|');int mode=int.Parse(fields[0]);var uri=new OSHTTPURI(Text(fields[1]),mode==1);if(mode==2)uri.ResolveDNS();uri.ValidAndResolved(out string error);var text=string.Join("|",(int)uri.Flags,uri.Port,uri.Host,uri.URL,uri.Path,uri.URI,uri.IP?.ToString()??"NULL",uri.URIwEndSlash,uri.HostAndPort,uri.IsValidHost?1:0,uri.IsResolvedHost?1:0,error);if(mode==3){var other=new OSHTTPURI(Text(fields[2]));text+="|"+(uri.Equals(other)?1:0)+"|"+Math.Sign(uri.CompareTo(other));}Console.WriteLine(Hex(text));}catch{Console.WriteLine(Hex("EX"));}}}
}
