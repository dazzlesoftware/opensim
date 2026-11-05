// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
using System;using System.IO;using System.Linq;using System.Collections.Generic;using System.Text;using OpenSim.Framework;using OpenMetaverse;
sealed class Capture:log4net.Appender.AppenderSkeleton {public List<string> Messages=new();protected override void Append(log4net.Core.LoggingEvent e){Messages.Add(e.RenderedMessage);}}
sealed class SnapshotCulture:System.Globalization.CultureInfo {readonly Action call;public SnapshotCulture(Action action):base(""){call=action;}public override System.Globalization.NumberFormatInfo NumberFormat {get {call();return base.NumberFormat;}set {base.NumberFormat=value;}}}
sealed class OuterReader:System.Xml.XmlReader {
 readonly bool empty;readonly int fault;readonly string inner;readonly Action<string> trace;
 public OuterReader(bool e,int f,string i,Action<string> t){empty=e;fault=f;inner=i;trace=t;}
 public override bool IsEmptyElement {get{trace("empty");if(fault==1)throw new InvalidOperationException("empty fault");return empty;}}
 public override string ReadInnerXml(){trace("inner");if(fault==2)throw new InvalidOperationException("inner fault");return inner;}
 public override int AttributeCount=>0;public override string BaseURI=>"";public override int Depth=>0;public override bool EOF=>false;public override bool HasValue=>false;public override string LocalName=>"outer";public override string NamespaceURI=>"";public override System.Xml.XmlNameTable NameTable=>new System.Xml.NameTable();public override System.Xml.XmlNodeType NodeType=>System.Xml.XmlNodeType.Element;public override string Prefix=>"";public override System.Xml.ReadState ReadState=>System.Xml.ReadState.Interactive;public override string Value=>"";
 public override string GetAttribute(string n)=>null;public override string GetAttribute(string n,string ns)=>null;public override string GetAttribute(int i)=>null;public override string LookupNamespace(string p)=>null;public override bool MoveToAttribute(string n)=>false;public override bool MoveToAttribute(string n,string ns)=>false;public override void MoveToAttribute(int i){}public override bool MoveToElement()=>true;public override bool MoveToFirstAttribute()=>false;public override bool MoveToNextAttribute()=>false;public override bool Read()=>false;public override bool ReadAttributeValue()=>false;public override void ResolveEntity(){}
}
sealed class CaptureWriter:System.Xml.XmlTextWriter {
 readonly Action<string> write;public CaptureWriter(Action<string> action):base(new StringWriter()){write=action;}public override void WriteRaw(string data){write(data);}
}
static class Program {
 static readonly UUID Id=new UUID("00112233-4455-6677-8899-aabbccddeeff");
 static byte[] Bytes(string s)=>s=="null"?null:s=="-"?Array.Empty<byte>():Convert.FromHexString(s);
 static string Hex(byte[] b)=>Convert.ToHexString(b).ToLowerInvariant();
 static string Raw16(string s)=>s==null?"null":string.Concat(s.Select(c=>((int)c).ToString("x4")));
 static string Text16(string s)=>s=="null"?null:s=="-"?"":new string(Enumerable.Range(0,s.Length/4).Select(i=>(char)Convert.ToUInt16(s.Substring(i*4,4),16)).ToArray());
 static float Number(string s)=>BitConverter.Int32BitsToSingle(unchecked((int)Convert.ToUInt32(s,16)));
static string MediaSnapshot(PrimitiveBaseShape.MediaList list){if(list==null)return "null";var o=new StringBuilder();o.Append(list.Count).Append(':');foreach(var e in list){if(e==null){o.Append("null;");continue;}foreach(var v in new[]{e.EnableAlterntiveImage,e.AutoLoop,e.AutoPlay,e.AutoScale,e.AutoZoom,e.InteractOnFirstClick,e.EnableWhiteList})o.Append(v?1:0);o.Append(',').Append((int)e.Controls).Append(',').Append(e.Height).Append(',').Append(e.Width).Append(',').Append((byte)e.ControlPermissions).Append(',').Append((byte)e.InteractPermissions).Append(',').Append(Raw16(e.HomeURL)).Append(',').Append(Raw16(e.CurrentURL)).Append(',');if(e.WhiteList==null)o.Append("null");else foreach(var v in e.WhiteList)o.Append(Raw16(v)).Append(':');o.Append(';');}return o.ToString();}
static string OmvSnapshot(Primitive s){var o=new StringBuilder();var p=s.PrimData;o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathEnd))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathRadiusOffset))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathSkew))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathScaleX))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathScaleY))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathShearX))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathShearY))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathTaperX))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathTaperY))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathBegin))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathTwist))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathTwistBegin))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.PathRevolutions))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.ProfileBegin))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.ProfileEnd))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(p.ProfileHollow))).Append(',');o.Append(p.profileCurve).Append(',').Append((byte)p.PathCurve).Append(',').Append(p.State).Append(',').Append((byte)p.PCode).Append(',').Append((byte)p.Material).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Scale.X))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Scale.Y))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Scale.Z))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Position.X))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Position.Y))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Position.Z))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Rotation.X))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Rotation.Y))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Rotation.Z))).Append(',');o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Rotation.W))).Append(',');o.Append(s.Textures==null?"null":Hex(s.Textures.GetBytes())).Append(',');if(s.Sculpt!=null)o.Append(s.Sculpt.SculptTexture).Append(':').Append(s.Sculpt.GetBytes()[16]).Append(':').Append((byte)s.Sculpt.Type);else o.Append("null");o.Append(',');
if(s.Flexible!=null){var f=s.Flexible;o.Append(f.Softness).Append(':');foreach(var v in new[]{f.Tension,f.Drag,f.Gravity,f.Wind,f.Force.X,f.Force.Y,f.Force.Z})o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(v))).Append(':');}else o.Append("null");o.Append(',');
if(s.Light!=null){var l=s.Light;foreach(var v in new[]{l.Color.R,l.Color.G,l.Color.B,l.Color.A,l.Intensity,l.Radius,l.Cutoff,l.Falloff})o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(v))).Append(':');}else o.Append("null");o.Append(',');
if(s.Properties!=null){var v=s.Properties;o.Append(Raw16(v.Name)).Append(':').Append(Raw16(v.Description)).Append(':').Append(Raw16(v.TouchName)).Append(':').Append(Raw16(v.SitName)).Append(':').Append(v.ObjectID).Append(':').Append(v.CreatorID).Append(':').Append(v.OwnerID).Append(':').Append(v.GroupID).Append(':').Append(v.ItemID).Append(':').Append(v.FolderID).Append(':').Append(v.FromTaskID).Append(':').Append(v.LastOwnerID).Append(':').Append(v.CreationDate.Ticks).Append(':');foreach(var mask in new[]{v.Permissions.BaseMask,v.Permissions.EveryoneMask,v.Permissions.GroupMask,v.Permissions.NextOwnerMask,v.Permissions.OwnerMask})o.Append((uint)mask).Append(':');o.Append(v.OwnershipCost).Append(':').Append(v.SalePrice).Append(':').Append((int)v.Category).Append(':').Append((byte)v.SaleType).Append(':').Append(v.AggregatePerms).Append(':').Append(v.AggregatePermTextures).Append(':').Append(v.AggregatePermTexturesOwner).Append(':').Append(v.InventorySerial).Append(':').Append(v.TextureIDs==null?"null":"array");}else o.Append("null");return o.ToString();}
 static void Seed(PrimitiveBaseShape s){s.FlexiEntry=s.LightEntry=s.SculptEntry=s.ProjectionEntry=true;s.SculptType=5;s.SculptTexture=Id;s.ProjectionTextureUUID=Id;s.FlexiSoftness=3;
s.FlexiTension=0.25f;
s.FlexiDrag=0.5f;
s.FlexiGravity=0.75f;
s.FlexiWind=1.0f;
s.FlexiForceX=1.25f;
s.FlexiForceY=1.5f;
s.FlexiForceZ=1.75f;
s.LightColorR=2.0f;
s.LightColorG=2.25f;
s.LightColorB=2.5f;
s.LightColorA=2.75f;
s.LightRadius=3.0f;
s.LightCutoff=3.25f;
s.LightFalloff=3.5f;
s.LightIntensity=3.75f;
s.ProjectionFOV=4.0f;
s.ProjectionFocus=4.25f;
s.ProjectionAmbiance=4.5f;s.ReadMeshFlagsData(Bytes("ffffffff"),0);s.ReflectionProbe=new Primitive.ReflectionProbe {Ambiance=.5f,ClipDistance=2f,Flags=9};s.RenderMaterials=new Primitive.RenderMaterials{entries=new[]{new Primitive.RenderMaterials.RenderMaterialEntry{te_index=7,id=Id}}};}
 static string Snapshot(PrimitiveBaseShape s){var o=new StringBuilder();o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Scale.X))).Append(',').Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Scale.Y))).Append(',').Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.Scale.Z))).Append(',');
o.Append((int)s.PathBegin).Append(",");
o.Append((int)s.PathCurve).Append(",");
o.Append((int)s.PathEnd).Append(",");
o.Append((int)s.PathRadiusOffset).Append(",");
o.Append((int)s.PathRevolutions).Append(",");
o.Append((int)s.PathScaleX).Append(",");
o.Append((int)s.PathScaleY).Append(",");
o.Append((int)s.PathShearX).Append(",");
o.Append((int)s.PathShearY).Append(",");
o.Append((int)s.PathSkew).Append(",");
o.Append((int)s.PathTaperX).Append(",");
o.Append((int)s.PathTaperY).Append(",");
o.Append((int)s.PathTwist).Append(",");
o.Append((int)s.PathTwistBegin).Append(",");
o.Append((int)s.PCode).Append(",");
o.Append((int)s.ProfileBegin).Append(",");
o.Append((int)s.ProfileEnd).Append(",");
o.Append((int)s.ProfileHollow).Append(",");
o.Append((int)s.State).Append(",");
o.Append((int)s.LastAttachPoint).Append(",");
o.Append((int)s.ProfileShape).Append(",");
o.Append((int)s.HollowShape).Append(",");
o.Append((int)s.SculptType).Append(",");
o.Append((int)s.FlexiSoftness).Append(",");o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiTension))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiDrag))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiGravity))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiWind))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiForceX))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiForceY))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.FlexiForceZ))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightColorR))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightColorG))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightColorB))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightColorA))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightRadius))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightCutoff))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightFalloff))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.LightIntensity))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.ProjectionFOV))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.ProjectionFocus))).Append(",");
o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.ProjectionAmbiance))).Append(",");o.Append(s.FlexiEntry?1:0).Append(",");
o.Append(s.LightEntry?1:0).Append(",");
o.Append(s.SculptEntry?1:0).Append(",");
o.Append(s.ProjectionEntry?1:0).Append(",");o.Append(s.MeshFlagEntry?1:0).Append(',').Append((uint)typeof(PrimitiveBaseShape).GetField("_meshFlags",System.Reflection.BindingFlags.NonPublic|System.Reflection.BindingFlags.Instance).GetValue(s)).Append(',').Append(s.AnimeshEnabled?1:0).Append(',').Append(s.SculptTexture).Append(',').Append(s.ProjectionTextureUUID).Append(',').Append(Hex(s.TextureEntry)).Append(',');
 if(s.ReflectionProbe!=null)o.Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.ReflectionProbe.Ambiance))).Append(':').Append(unchecked((uint)BitConverter.SingleToInt32Bits(s.ReflectionProbe.ClipDistance))).Append(':').Append(s.ReflectionProbe.Flags);else o.Append("null");o.Append(',');
 if(s.RenderMaterials==null)o.Append("null");else if(s.RenderMaterials.entries==null)o.Append("none");else foreach(var e in s.RenderMaterials.entries)o.Append(e.te_index).Append(':').Append(e.id).Append(';');
 o.Append(',').Append(Hex(s.ExtraParamsToBytes())).Append(',');if(s.RenderMaterials==null)o.Append("nomat");else if(s.RenderMaterials.overrides==null)o.Append("null");else foreach(var e in s.RenderMaterials.overrides)o.Append(e.te_index).Append(':').Append(Raw16(e.data)).Append(';');var raw=s.RenderMaterialsOvrToRawBin();o.Append(',').Append(raw==null?"null":Hex(raw)).Append(',').Append(s.SculptData==null?"null":Hex(s.SculptData)).Append(',').Append(MediaSnapshot(s.Media));return o.ToString();}
 static void Main(){System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;using var output=new StreamWriter(Console.OpenStandardOutput(),new UTF8Encoding(false),65536);Console.SetOut(output);var capture=new Capture();log4net.Config.BasicConfigurator.Configure(capture);
 string line;while((line=Console.ReadLine())!=null){System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;capture.Messages.Clear();var s=new PrimitiveBaseShape();string error="ok",result="";foreach(var step in line.Split('|')){try{var a=step.Split(' ');switch(a[0]){
 case "seed":Seed(s);break;
 case "factory":int k=int.Parse(a[1]),n=int.Parse(a[2]);s=k switch {0=>new PrimitiveBaseShape(),1=>PrimitiveBaseShape.CreateBox(),2=>PrimitiveBaseShape.CreateSphere(),3=>PrimitiveBaseShape.CreateCylinder(),4=>PrimitiveBaseShape.Default,5=>PrimitiveBaseShape.CreateMesh(n,Id),6=>PrimitiveBaseShape.CreateCylinder(1.25f,2.5f),7=>PrimitiveBaseShape.Create(),_=>throw new Exception()};break;
 case "profile":s.ProfileCurve=byte.Parse(a[1]);break;
 case "rawprofile":s.HollowShape=(HollowShape)byte.Parse(a[1]);s.ProfileShape=(ProfileShape)byte.Parse(a[2]);break;
 case "animesh":s.AnimeshEnabled=a[1]=="1";break;
 case "sculpt":s.SculptType=byte.Parse(a[1]);s.SculptEntry=a[2]=="1";break;
 case "read":k=int.Parse(a[1]);int p=int.Parse(a[2]),size=int.Parse(a[3]);var b=Bytes(a[4]);switch(k){case 0:s.ReadInExtraParamsBytes(b);break;case 1:s.ReadSculptData(b,p);break;case 2:s.ReadFlexiData(b,p);break;case 3:s.ReadLightData(b,p);break;case 4:s.ReadProjectionData(b,p);break;case 5:s.ReadMeshFlagsData(b,p);break;case 6:s.ReadReflectionProbe(b,p);break;case 7:result=s.ReadRenderMaterials(b,p,size).ToString();break;}break;
 case "update":s.ReadInUpdateExtraParam(ushort.Parse(a[1]),a[2]=="1",Bytes(a[3]));break;
 case "color":float v=Number(a[2]);switch(a[1]){case "0":s.LightColorR=v;break;case "1":s.LightColorG=v;break;case "2":s.LightColorB=v;break;case "3":s.LightColorA=v;break;}break;
 case "texture":s.TextureEntry=Bytes(a[1]);break;
 case "medianew":s.Media=new PrimitiveBaseShape.MediaList(int.Parse(a[1]));break;
 case "mediaadd":s.Media.Add(a[1]=="1"?new MediaEntry():null);break;
 case "mediafield":if(a[1]=="0")s.Media[^1].CurrentURL=Text16(a[2]);else s.Media[^1].HomeURL=Text16(a[2]);break;
 case "mediawhite":s.Media[^1].WhiteList[int.Parse(a[1])]=Text16(a[2]);break;
 case "mediawhites":int whites=int.Parse(a[1]);s.Media[^1].WhiteList=whites<0?null:new string[whites];break;
 case "mediavalue":int indexValue=int.Parse(a[1]),mediaValue=int.Parse(a[2]);var media=s.Media[^1];switch(indexValue){case 0:media.EnableAlterntiveImage=mediaValue!=0;break;case 1:media.AutoLoop=mediaValue!=0;break;case 2:media.AutoPlay=mediaValue!=0;break;case 3:media.AutoScale=mediaValue!=0;break;case 4:media.AutoZoom=mediaValue!=0;break;case 5:media.InteractOnFirstClick=mediaValue!=0;break;case 6:media.EnableWhiteList=mediaValue!=0;break;case 7:media.Controls=(MediaControls)mediaValue;break;case 8:media.Height=mediaValue;break;case 9:media.Width=mediaValue;break;case 10:media.ControlPermissions=(MediaPermission)mediaValue;break;case 11:media.InteractPermissions=(MediaPermission)mediaValue;break;}break;
 case "mediacollect":if(a[1]=="1"){var original=s.Media;s.Media=new PrimitiveBaseShape.MediaList(original);result=(!ReferenceEquals(s.Media,original)?"1":"0")+(original.Count==0||ReferenceEquals(original[0],s.Media[0])?"1":"0");}else s.Media=new PrimitiveBaseShape.MediaList((IEnumerable<MediaEntry>)null);break;
 case "mediawrite":var captured=new CaptureWriter(data=>{result="raw:"+Raw16(data)+";";if(a[1]=="1")throw new InvalidOperationException("writer fault");});s.Media.WriteXml(captured);break;
 case "mediawritenull":s.Media.WriteXml((System.Xml.XmlWriter)null);break;
 case "mediareadnull":s.Media.ReadXml((System.Xml.XmlReader)null);break;
 case "mediaouterfault":var fake=new OuterReader(a[1]=="1",int.Parse(a[2]),Text16(a[3]),e=>result+=e+";");s.Media.ReadXml(fake);break;
 case "mediaculture":var culture=(System.Globalization.CultureInfo)System.Globalization.CultureInfo.InvariantCulture.Clone();culture.NumberFormat.NegativeSign=Text16(a[1]);System.Globalization.CultureInfo.CurrentCulture=culture;break;
 case "mediamutate":bool firstFormat=true;System.Globalization.CultureInfo.CurrentCulture=new SnapshotCulture(()=>{if(firstFormat){firstFormat=false;var e=s.Media[0];e.CurrentURL="changed";e.Height=-99;e.WhiteList[0]="changed";s.Media.Add(null);}});try{result=Raw16(s.Media.ToXml());}finally{System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;}break;
 case "mediaxml":result=Raw16(s.Media.ToXml());break;
 case "mediaread":s.Media.ReadXml(Text16(a[1]));break;
 case "mediafrom":s.Media=PrimitiveBaseShape.MediaList.FromXml(Text16(a[1]));break;
 case "mediaouter":using(var outer=new System.Xml.XmlTextReader(new StringReader(Text16(a[1])))){outer.MoveToContent();s.Media.ReadXml(outer);}break;
 case "fromomv":var prim=new Primitive();prim.PrimData.profileCurve=byte.Parse(a[1]);prim.PrimData.PathCurve=(PathCurve)byte.Parse(a[2]);prim.PrimData.State=byte.Parse(a[3]);prim.PrimData.PCode=(PCode)byte.Parse(a[4]);int sculpt=int.Parse(a[5]);prim.PrimData.PathEnd=Number(a[7]);prim.PrimData.PathRadiusOffset=Number(a[8]);prim.PrimData.PathSkew=Number(a[9]);prim.PrimData.PathScaleX=Number(a[10]);prim.PrimData.PathScaleY=Number(a[11]);prim.PrimData.PathShearX=Number(a[12]);prim.PrimData.PathShearY=Number(a[13]);prim.PrimData.PathTaperX=Number(a[14]);prim.PrimData.PathTaperY=Number(a[15]);prim.PrimData.PathBegin=Number(a[16]);prim.PrimData.PathTwist=Number(a[17]);prim.PrimData.PathTwistBegin=Number(a[18]);prim.PrimData.PathRevolutions=Number(a[19]);prim.PrimData.ProfileBegin=Number(a[20]);prim.PrimData.ProfileEnd=Number(a[21]);prim.PrimData.ProfileHollow=Number(a[22]);prim.Scale=new Vector3(Number(a[23]),Number(a[24]),Number(a[25]));if(a[6]=="1")prim.Textures=new Primitive.TextureEntry(Id);if(sculpt>=0)prim.Sculpt=new Primitive.SculptData{SculptTexture=Id,Type=(SculptType)sculpt};s=new PrimitiveBaseShape(prim);break;
 case "convert":result=OmvSnapshot(a[1]=="1"?s.ToOmvPrimitive(new Vector3(1,2,3),new Quaternion(4,5,6,7)):s.ToOmvPrimitive());break;
 case "nullomv":s=new PrimitiveBaseShape((Primitive)null);break;
 case "dimension":float dim=Number(a[2]);if(a[1]=="0")s.SetScale(dim);else if(a[1]=="1")s.SetHeigth(dim);else s.SetRadius(dim);break;
 case "sculptprops":s.SetSculptProperties(byte.Parse(a[1]),new UUID(a[2]));break;
 case "textures":s.Textures=new Primitive.TextureEntry(new UUID(a[1]));break;
 case "getmesh":s.GetMesh();break;
 case "range":int kind=int.Parse(a[1]);float x=Number(a[2]),y=Number(a[3]);if(kind==0)s.SetPathRange(x,y);else if(kind==1)s.SetProfileRange(x,y);else if(kind==2)s.SetPathRange(new Vector3(x,y,9));else s.SetProfileRange(new Vector3(x,y,9));break;
 case "ovrnew":int count=int.Parse(a[1]);s.RenderMaterials=count<0?null:new Primitive.RenderMaterials();if(count>0)s.RenderMaterials.overrides=new Primitive.RenderMaterials.RenderMaterialOverrideEntry[count];break;
 case "ovrset":s.RenderMaterials.overrides[int.Parse(a[1])]=new Primitive.RenderMaterials.RenderMaterialOverrideEntry{te_index=byte.Parse(a[2]),data=Text16(a[3])};break;
 case "ovrread":s.RenderMaterialsOvrFromRawBin(Bytes(a[1]));break;
 case "float":int floatIndex=int.Parse(a[1]);float rawFloat=Number(a[2]);switch(floatIndex){case 0:s.FlexiTension=rawFloat;break;case 1:s.FlexiDrag=rawFloat;break;case 2:s.FlexiGravity=rawFloat;break;case 3:s.FlexiWind=rawFloat;break;case 4:s.FlexiForceX=rawFloat;break;case 5:s.FlexiForceY=rawFloat;break;case 6:s.FlexiForceZ=rawFloat;break;case 7:s.LightColorR=rawFloat;break;case 8:s.LightColorG=rawFloat;break;case 9:s.LightColorB=rawFloat;break;case 10:s.LightColorA=rawFloat;break;case 11:s.LightRadius=rawFloat;break;case 12:s.LightCutoff=rawFloat;break;case 13:s.LightFalloff=rawFloat;break;case 14:s.LightIntensity=rawFloat;break;case 15:s.ProjectionFOV=rawFloat;break;case 16:s.ProjectionFocus=rawFloat;break;case 17:s.ProjectionAmbiance=rawFloat;break;}break;
 case "set":int index=int.Parse(a[1]),value=int.Parse(a[2]);switch(index){case 0:s.PathBegin=(ushort)value;break;case 1:s.PathCurve=(byte)value;break;case 2:s.PathEnd=(ushort)value;break;case 3:s.PathRadiusOffset=(sbyte)value;break;case 4:s.PathRevolutions=(byte)value;break;case 5:s.PathScaleX=(byte)value;break;case 6:s.PathScaleY=(byte)value;break;case 7:s.PathShearX=(byte)value;break;case 8:s.PathShearY=(byte)value;break;case 9:s.PathSkew=(sbyte)value;break;case 10:s.PathTaperX=(sbyte)value;break;case 11:s.PathTaperY=(sbyte)value;break;case 12:s.PathTwist=(sbyte)value;break;case 13:s.PathTwistBegin=(sbyte)value;break;case 14:s.PCode=(byte)value;break;case 15:s.ProfileBegin=(ushort)value;break;case 16:s.ProfileEnd=(ushort)value;break;case 17:s.ProfileHollow=(ushort)value;break;case 18:s.State=(byte)value;break;case 19:s.LastAttachPoint=(byte)value;break;case 20:s.ProfileShape=(ProfileShape)value;break;case 21:s.HollowShape=(HollowShape)value;break;case 22:s.SculptType=(byte)value;break;case 23:s.FlexiSoftness=(int)value;break;}break;
 case "decode":result=Hex(s.Textures.GetBytes());break;
 case "nulltextures":s.Textures=null;break;
 case "defaults":var defaults=new PrimitiveBaseShape();result=(ReferenceEquals(s.TextureEntry,defaults.TextureEntry)?"1":"0")+(ReferenceEquals(s.SculptData,defaults.SculptData)?"1":"0");break;
 case "aliasreplace":Seed(s);var replacement=s.Copy();replacement.ReadRenderMaterials(Bytes("012100112233445566778899aabbccddeeff"),0,18);replacement.ReadReflectionProbe(Bytes("0000803f0000004007"),0);result=(ReferenceEquals(s.RenderMaterials,replacement.RenderMaterials)?"1":"0")+(ReferenceEquals(s.ReflectionProbe,replacement.ReflectionProbe)?"1":"0");break;
 case "alias":Seed(s);s.TextureEntry=Bytes("1234");s.SculptData=Bytes("5678");s.Media=new PrimitiveBaseShape.MediaList{new MediaEntry()};var other=s.Copy();result=(ReferenceEquals(s.TextureEntry,other.TextureEntry)?"1":"0")+(ReferenceEquals(s.SculptData,other.SculptData)?"1":"0")+(ReferenceEquals(s.ReflectionProbe,other.ReflectionProbe)?"1":"0")+(ReferenceEquals(s.RenderMaterials,other.RenderMaterials)?"1":"0")+(ReferenceEquals(s.Media,other.Media)?"1":"0");other.Media.Add(null);other.ProfileBegin=123;other.TextureEntry[0]=77;other.SculptData[0]=88;other.ReflectionProbe.Ambiance=9;other.RenderMaterials.entries[0].te_index=19;result+=":"+s.ProfileBegin+":"+Hex(s.SculptData);break;
 case "meshkey":result=s.GetMeshKey(new Vector3(Number(a[1]),Number(a[2]),Number(a[3])),Number(a[4]),a[5]=="1").ToString();break;
 default:throw new Exception("Unknown operation");}
 }catch(Exception e){error=e.GetType().FullName+":"+Hex(Encoding.UTF8.GetBytes(e.Message))+":"+unchecked((uint)e.HResult)+":"+(e is ArgumentException arg&&arg.ParamName!=null?Hex(Encoding.UTF8.GetBytes(arg.ParamName)):"null");break;}}
 System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;Console.Write(error+"/"+result+"/"+Snapshot(s)+"/");foreach(var m in capture.Messages)Console.Write(Hex(Encoding.UTF8.GetBytes(m))+";");Console.WriteLine();}
 }
}
