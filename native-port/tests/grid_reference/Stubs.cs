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
using System.Collections.Generic;
using OpenMetaverse;
using OpenSim.Data;
namespace log4net {
 public interface ILog {void DebugFormat(string value,params object[] args);void WarnFormat(string value,params object[] args);void Warn(string value);}
 class Log:ILog {public void DebugFormat(string value,params object[] args){}public void WarnFormat(string value,params object[] args){}public void Warn(string value){}}
 public static class LogManager {public static ILog GetLogger(Type type)=>new Log();}
}
namespace OpenSim.Framework {
 public static class Constants {public const uint RegionSize=256,MaximumRegionSize=4096;}
 public static class Util {
  public static uint WorldToRegionLoc(uint value)=>value>>8;public static uint RegionToWorldLoc(uint value)=>value<<8;public static ulong UIntsToLong(uint x,uint y)=>((ulong)x<<32)|y;
  public static uint ConvertAccessLevelToMaturity(byte value)=>value<=13?0u:value<=21?1u:2u;
  public static IPEndPoint getEndPoint(string host,int port)=>new(Dns.GetHostAddresses(host)[0],port);
  public static IPAddress GetHostFromDNS(string host)=>host=="bad.invalid"?null:IPAddress.Parse("203.0.113.7");public static int UnixTimeSinceEpoch()=>1700000000;public static string EscapeForLike(string value)=>value.Replace("_","\\_").Replace("%","\\%");
 }
 public class RegionInfo {public string RegionName,ExternalHostName,ServerURI,regionSecret;public uint WorldLocX,WorldLocY,RegionSizeX,RegionSizeY,HttpPort;public IPEndPoint InternalEndPoint;public UUID RegionID;public byte AccessLevel;public RegionSettings RegionSettings=new();public EstateSettings EstateSettings=new();}
 public class RegionSettings {public UUID TerrainImageID,ParcelImageID;public int Maturity;}
 public class EstateSettings {public UUID EstateOwner;}
 public class GridInfo {public int IsLocalGrid(string uri)=>uri.Contains("local.example")?1:0;}
}
namespace OpenSim.Framework.Console {public class MainConsole {public static MainConsole Instance=new();public void Output(string value){}}}
namespace OpenSim.Services.Interfaces {public interface IAuthenticationService {bool Verify(UUID id,string token,int lifetime);}}

public class FaultRegionStore : IRegionData {
 readonly OpenSim.Data.Null.NullRegionData inner=new("","");public bool StoreFalse,StoreThrows,EraseFalse,EraseThrows;
 public RegionData Get(UUID id,UUID scope)=>inner.Get(id,scope);public RegionData Get(int x,int y,UUID scope)=>inner.Get(x,y,scope);
 public List<RegionData> Get(string name,UUID scope)=>inner.Get(name,scope);public RegionData GetSpecific(string name,UUID scope)=>inner.GetSpecific(name,scope);
 public List<RegionData> Get(int x1,int y1,int x2,int y2,UUID scope)=>inner.Get(x1,y1,x2,y2,scope);
 public bool Store(RegionData row){if(StoreThrows)throw new Exception("fixture write");return !StoreFalse&&inner.Store(row);}
 public bool SetDataItem(UUID id,string item,string value)=>inner.SetDataItem(id,item,value);
 public bool Delete(UUID id){if(EraseThrows)throw new Exception("fixture delete");return !EraseFalse&&inner.Delete(id);}
 public List<RegionData> GetDefaultRegions(UUID scope)=>inner.GetDefaultRegions(scope);public List<RegionData> GetDefaultHypergridRegions(UUID scope)=>inner.GetDefaultHypergridRegions(scope);
 public List<RegionData> GetFallbackRegions(UUID scope)=>inner.GetFallbackRegions(scope);public List<RegionData> GetHyperlinks(UUID scope)=>inner.GetHyperlinks(scope);public List<RegionData> GetOnlineRegions(UUID scope)=>inner.GetOnlineRegions(scope);
}

namespace OpenSim.Services.GridService {public class HypergridLinker {public OpenSim.Services.Interfaces.GridRegion LinkRegion(UUID scope,OpenSim.Framework.RegionURI uri){if(uri.Host!="remote.example")return null;return new(){RegionID=new UUID("00000000-0000-0000-0000-000000000090"),RegionName=uri.RegionName,InternalEndPoint=new IPEndPoint(IPAddress.Any,9000),ExternalHostName="203.0.113.7"};}}}
