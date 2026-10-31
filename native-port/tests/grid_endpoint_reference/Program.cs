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
using System;using System.IO;using System.Text;using System.Linq;using System.Net;using OpenMetaverse;using OpenSim.Data;using OpenSim.Services.Interfaces;using OpenSim.Services.GridService;using OpenSim.Framework.ServiceAuth;using OpenSim.Framework.Servers.HttpServer;using OpenSim.Server.Handlers.Grid;using GridRegion=OpenSim.Services.Interfaces.GridRegion;
static class EndpointProgram {
 static string Hex(string value)=>Convert.ToHexString(Encoding.UTF8.GetBytes(value??"")).ToLowerInvariant();
 static void Main(string[] args){var config=new Nini.Config.IniConfigSource(args[0]);if(args.Length>1){System.Console.WriteLine(Hex(OpenSim.Server.Base.ServerUtils.BuildXmlResponse(new ExtraFeatures(config).Values)));return;}var store=new FaultRegionStore();string line;while((line=System.Console.ReadLine())!=null){var previous=store.Get("%",UUID.Zero);if(previous!=null)foreach(var row in previous)store.Delete(row.RegionID);var grid=new GridService(store,config,null);
 for(int i=1;i<=2;i++){var region=new GridRegion{RegionID=new UUID("00000000-0000-0000-0000-00000000000"+i),RegionName=i==1?"Alpha":"Beta",RegionLocX=25600+(i-1)*256,RegionLocY=25600,InternalEndPoint=new IPEndPoint(IPAddress.Any,9000),ExternalHostName="127.0.0.1"};var data=grid.RegionInfo2RegionData(region);data.Data["flags"]=i==1?"1031":"516";store.Store(data);}
 var fields=line.Split('|');var body=Encoding.UTF8.GetString(Convert.FromHexString(fields[0]));var authorization=Encoding.UTF8.GetString(Convert.FromHexString(fields[1]));var request=new IOSHttpRequest();if(authorization.Length>0)request.Headers["Authorization"]=authorization;if(fields[2]=="1")request.Headers["X-SecondLife-Shard"]="";var response=new IOSHttpResponse();var handler=new GridServerPostHandler(grid,ServiceAuth.Create(config,"GridService"));try{var result=handler.Handle("/grid",new MemoryStream(Encoding.UTF8.GetBytes(body)),request,response);System.Console.WriteLine(response.StatusCode+"|"+Convert.ToHexString(result).ToLowerInvariant()+"|"+Hex(response.ContentType)+"|"+Hex(response.Headers["WWW-Authenticate"]));}catch(FormatException){System.Console.WriteLine("BASE64_FORMAT_ERROR");}
 }}
}
