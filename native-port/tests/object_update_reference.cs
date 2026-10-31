using System;using OpenMetaverse;using OpenSim.Region.ClientStack.LindenUDP;
namespace OpenSim.Framework {public static class Util {
 public unsafe static int osUTF8Getbytes(string s,byte* b,int n,bool terminate){throw new NotSupportedException("unused short-string fixture path");}
 public static byte[] StringToBytes256(string s)=>throw new NotSupportedException("attachment model excluded");
}}
enum ProfileShape {Circle=0}
class Shape {
 public byte State,PCode=9,Material,SculptType,ProfileCurve=1,PathCurve=16,PathScaleX=100,PathScaleY=100,PathShearX,PathShearY,PathRevolutions;
 public sbyte PathTwist,PathTwistBegin,PathRadiusOffset,PathTaperX,PathTaperY,PathSkew;
 public ushort PathBegin,PathEnd,ProfileBegin,ProfileEnd,ProfileHollow;
 public Vector3 Scale=new Vector3(.5f,.5f,.5f);
 public byte[] TextureEntry=new Primitive.TextureEntry(new UUID("89556747-24cb-43ed-920b-47caed15465f")).GetBytes(),ExtraParams;
}
class Group {public bool IsAttachment;public UUID FromItemID;public int AttachmentPoint;}
class SceneObjectPart {
 public uint LocalId,ParentID,GeneratedFlags;public UUID UUID,OwnerID,Sound;public bool CreateSelected,IsRoot=true;
 public int PseudoCRC;public byte Material=3,ClickAction,SoundFlags;
 public Shape Shape=new();public Group ParentGroup=new();
 public Vector3 RelativePosition,Velocity,Acceleration,AngularVelocity;public Quaternion RotationOffset;
 public byte[] TextureAnimation,ParticleSystem;public osUTF8 osUTF8Text,osUTFMediaUrl;public double SoundGain,SoundRadius;
 public int TextColorArgb()=>0;
}
class ScenePresence {public UUID UUID;}
class Permissions {public uint GenerateClientFlags(SceneObjectPart p,ScenePresence s)=>p.GeneratedFlags;}
class Scene {public Permissions Permissions=new();}
partial class ObjectUpdateReference {
 Scene m_scene=new();
 static SceneObjectPart Object(int seed,int mode){var o=new SceneObjectPart();o.OwnerID=new UUID("11223344-5566-7788-99aa-bbccddeeff00");o.UUID=new UUID("00112233-4455-6677-8899-aabbccddeeff");o.LocalId=(uint)(720001+seed);o.RelativePosition=new Vector3(seed*.25f,-seed*.125f,23);o.Velocity=new Vector3(seed*.03125f,0,1);o.Acceleration=new Vector3(1,-2,0);o.AngularVelocity=new Vector3(.5f,0,-.25f);o.RotationOffset=new Quaternion(seed*.01f,-.2f,.3f,(mode&4)!=0?-.4f:.4f);if((mode&8)!=0)o.RotationOffset=new Quaternion(0,0,0,0);if((mode&16)!=0)o.RotationOffset=new Quaternion(.01f,.02f,.03f,.99995f);o.GeneratedFlags=(mode&2)!=0?0xfff0fff3u:0x12347u;o.PseudoCRC=unchecked((int)(0xaabb0000u+seed));o.CreateSelected=(mode&4)!=0;
 if((mode&32)!=0){var s=o.Shape;s.State=(byte)(seed&255);s.PathCurve=32;s.ProfileCurve=5;s.PathBegin=123;s.PathEnd=456;s.PathScaleX=115;s.PathScaleY=133;s.PathShearX=17;s.PathShearY=255;s.PathTwist=-123;s.PathTwistBegin=127;s.PathRadiusOffset=-17;s.PathTaperX=-31;s.PathTaperY=29;s.PathRevolutions=33;s.PathSkew=-44;s.ProfileBegin=1234;s.ProfileEnd=2345;s.ProfileHollow=3333;o.Material=7;o.ClickAction=2;}
 if((mode&64)!=0)o.Shape.TextureEntry=new byte[]{1,0,2,255,17};if((mode&128)!=0)o.Shape.TextureEntry=Array.Empty<byte>();if((mode&256)!=0){o.Shape.Scale=new Vector3(-0f,0,-0f);o.RelativePosition=new Vector3(-0f,0,0);o.Velocity=new Vector3(0,-0f,0);o.Acceleration=new Vector3(0,0,-0f);o.AngularVelocity=new Vector3(0,-0f,0);}return o;}
 static void Main(){string line;var host=new ObjectUpdateReference();while((line=Console.ReadLine())!=null){var args=line.Split(' ');int seed=int.Parse(args[0]),mode=int.Parse(args[1]);var part=Object(seed,mode);var sp=new ScenePresence{UUID=(mode&1)!=0?part.OwnerID:UUID.Zero};byte[] data=new byte[65536];data[0]=0xc0;data[6]=12;var zc=new LLUDPZeroEncoder(data);zc.Position=7;zc.AddUInt64(0x123456789abcdef0ul);zc.AddUInt16(Utils.FloatZeroOneToushort(seed*.03125f));zc.AddByte(1);host.CreatePrimUpdateBlock(part,sp,zc);int n=zc.Finish();Console.WriteLine(Convert.ToHexString(data,0,n).ToLowerInvariant());}}
}
