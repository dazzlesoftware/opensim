using System;
using OpenMetaverse;
class Program {
    static void Main(){
        var e=new Primitive.TextureEntry(Primitive.TextureEntry.WHITE_TEXTURE);
        string line;
        while((line=Console.ReadLine())!=null){
            var p=line.Split(' ');int i=int.Parse(p[0]),op=int.Parse(p[1]);uint bits=Convert.ToUInt32(p[2],16);float v=BitConverter.UInt32BitsToSingle(bits);
            var x=i<0?e.DefaultTexture:e.GetFace((uint)i);
            switch(op){
                case 0:x.RepeatU=v;break;case 1:x.RepeatV=v;break;case 2:x.OffsetU=v;break;case 3:x.OffsetV=v;break;case 4:x.Rotation=v;break;case 5:x.Glow=v;break;
                case 6:x.Bump=(Bumpiness)(byte)bits;break;case 7:x.Shiny=(Shininess)(byte)bits;break;case 8:x.Fullbright=bits!=0;break;case 9:x.MediaFlags=bits!=0;break;case 10:x.TexMapType=(MappingType)(byte)bits;break;
                case 11:x.RGBA=new Color4{R=v,G=v,B=v,A=v};break;case 12:e.SetDirtyFlags(i,(TextureAttributes)bits);break;case 13:e=new Primitive.TextureEntry(x);break;
                case 14:x.TextureID=bits!=0?Primitive.TextureEntry.WHITE_TEXTURE:UUID.Zero;break;case 15:x.MaterialID=bits!=0?Primitive.TextureEntry.WHITE_TEXTURE:UUID.Zero;break;
                case 16:e.DefaultTexture=null;break;case 17:e.DefaultTexture=new Primitive.TextureEntryFace(null);break;
            }
            foreach(int length in new[]{-1,0,21,22,45,100})Console.Write(Convert.ToHexString(e.GetBakesBytes(length)).ToLowerInvariant()+";");
            foreach(int length in new[]{-1,0,21,22,45,100})Console.Write(Convert.ToHexString(e.GetBytes(length)).ToLowerInvariant()+";");
            foreach(bool bakes in new[]{false,true}){
                var bytes=bakes?e.GetBakesBytes():e.GetBytes();var decoded=new Primitive.TextureEntry(bytes,0,bytes.Length);
                Console.Write(Convert.ToHexString(decoded.GetBytes()).ToLowerInvariant()+";"+Convert.ToHexString(decoded.GetBakesBytes()).ToLowerInvariant()+";"+((uint)decoded.GetDirtyFlags(45,false)).ToString("x")+";");
            }
            Console.Write(((uint)e.GetDirtyFlags(i,(bits&1)!=0)).ToString("x")+";");
            foreach(var face in e.FaceTextures)Console.Write(face==null?'0':'1');
            Console.WriteLine(";"+((uint)e.GetDirtyFlags(45,false)).ToString("x"));
        }
    }
}
