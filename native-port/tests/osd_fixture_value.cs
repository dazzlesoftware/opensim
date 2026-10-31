// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;using System.Text;using System.Globalization;using OpenMetaverse;using OpenMetaverse.StructuredData;
static class FixtureValue {
 public static OSD Build(string line){
  int space=line.IndexOf(' ');string kind=space<0?line:line.Substring(0,space),text=space<0?"":line.Substring(space+1);
  switch(kind){
   case "real":return OSD.FromReal(BitConverter.Int64BitsToDouble(unchecked((long)ulong.Parse(text,NumberStyles.HexNumber))));
   case "integer":return OSD.FromInteger(int.Parse(text));
   case "long":return OSD.FromLong(long.Parse(text));
   case "unsigned":return OSD.FromUInteger(uint.Parse(text));
   case "bool":return OSD.FromBoolean(text=="1");
   case "string":return OSD.FromString(Encoding.UTF8.GetString(Convert.FromHexString(text)));
   case "binary":return OSD.FromBinary(Convert.FromHexString(text));
   case "uuid":return OSD.FromUUID(UUID.Zero);
   case "uri":return OSD.FromUri(new Uri("http://example.test/"));
   case "date":return OSD.FromDate(DateTime.Parse(text,CultureInfo.InvariantCulture,DateTimeStyles.AdjustToUniversal|DateTimeStyles.AssumeUniversal));
   case "array":var array=new OSDArray();if(text!="")foreach(var part in text.Split(','))array.Add(OSD.FromInteger(int.Parse(part)));return array;
   case "map":return new OSDMap();
   default:return new OSD();
  }
 }
}
