using System;using System.Net;using OpenMetaverse;
namespace log4net {}
namespace OpenSim.Framework {
 public class RegionURI{}
 public static class Constants {public const uint RegionSize=256,MaximumRegionSize=4096;}
 public static partial class Util {public static uint WorldToRegionLoc(uint v)=>v>>8;public static uint RegionToWorldLoc(uint v)=>v<<8;public static ulong UIntsToLong(uint x,uint y)=>((ulong)x<<32)|y;public static uint ConvertAccessLevelToMaturity(byte v)=>v<=13?0u:v<=21?1u:2u;public static IPEndPoint getEndPoint(string h,int p)=>throw new NotSupportedException("DNS outside home-region fixture");}
 public class RegionInfo {public string RegionName,ExternalHostName,ServerURI,regionSecret;public uint WorldLocX,WorldLocY,RegionSizeX,RegionSizeY,HttpPort;public IPEndPoint InternalEndPoint;public UUID RegionID;public byte AccessLevel;public RegionSettings RegionSettings=new();public EstateSettings EstateSettings=new();}
 public class RegionSettings {public UUID TerrainImageID,ParcelImageID;public int Maturity;}
 public class EstateSettings {public UUID EstateOwner;}
}
