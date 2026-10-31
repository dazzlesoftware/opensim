using System;using OpenMetaverse;
class AppearanceStub {public float AvatarHeight;}
class RegionInfoStub {public uint RegionSizeX,RegionSizeY;}
class SceneStub {
 public RegionInfoStub RegionInfo=new();public int Mode,Calls;public float X,Y;
 public float GetGroundHeight(float x,float y){Calls++;X=x;Y=y;return Mode==1?(21f+x*.03125f)+y*.015625f:Mode==2?-90f:Mode==3?4000f:21f;}
}
class LogStub {public void WarnFormat(string message,params object[] args){}}
partial class RootPositionReference {
 SceneStub m_scene=new();AppearanceStub Appearance;LogStub m_log=new();string Name="avatar";UUID UUID=UUID.Zero;float PhysSearchHeight=4096;
 static float F(string s)=>BitConverter.UInt32BitsToSingle(Convert.ToUInt32(s,16));static string H(float f)=>BitConverter.SingleToUInt32Bits(f).ToString("x");
 static void Main(){string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');var host=new RootPositionReference();host.m_scene.RegionInfo.RegionSizeX=uint.Parse(p[0]);host.m_scene.RegionInfo.RegionSizeY=uint.Parse(p[1]);host.m_scene.Mode=int.Parse(p[6]);if(p[7]=="1")host.Appearance=new AppearanceStub{AvatarHeight=F(p[2])};Vector3 pos=host.Fragment(new Vector3(F(p[3]),F(p[4]),F(p[5])));Console.WriteLine(H(pos.X)+","+H(pos.Y)+","+H(pos.Z)+"|"+host.m_scene.Calls+"|"+H(host.m_scene.X)+","+H(host.m_scene.Y));}}
}
