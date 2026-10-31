using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using OpenMetaverse;
using OpenMetaverse.Packets;
static class Program {
    static readonly UUID FixtureId=new UUID("12345678-1234-5678-90ab-123456789abc");
    static object Fixture(Type type,string protocolType,int size,uint index,int seed){
        bool zero=seed==0;
        if(type==typeof(byte))return (byte)(zero?0:index%251);
        if(type==typeof(sbyte))return (sbyte)(zero?0:-(int)(index%100+1));
        if(type==typeof(ushort))return (ushort)(zero?0:(index*19)%65536);
        if(type==typeof(short))return (short)(zero?0:-(int)(index%30000+1));
        if(type==typeof(uint))return zero?0u:0xab000000u|index;
        if(type==typeof(int))return zero?0:-(int)((index%100000+1)*1000);
        if(type==typeof(ulong))return zero?0ul:0x0123456789abcdeful^((ulong)index<<32);
        if(type==typeof(long))return zero?0L:-0x123456789abcdeL+index;
        if(type==typeof(bool))return !zero&&index%2==0;
        if(type==typeof(float))return zero?0f:(float)(index%1000)/8f-60f;
        if(type==typeof(double))return zero?0.0:(double)(index%1000)/16+17;
        if(type==typeof(UUID))return zero?UUID.Zero:FixtureId;
        if(type==typeof(Vector3))return zero?new Vector3():new Vector3(1.25f,-2.5f,128.75f);
        if(type==typeof(Vector3d))return zero?new Vector3d():new Vector3d(1.25,-2.5,128000000.75);
        if(type==typeof(Vector4))return zero?new Vector4():new Vector4(1.25f,-2.5f,128.75f,-.125f);
        if(type==typeof(Quaternion))return zero?new Quaternion():seed==1?new Quaternion(0,0,0,1):seed==2?new Quaternion(.5f,.5f,.5f,.5f):new Quaternion(.5f,.5f,.5f,-.5f);
        if(type==typeof(byte[])){
            int count=protocolType=="Fixed"?size:zero?0:(int)(index%11+1);
            var bytes=new byte[count];for(int i=0;i<count;++i)bytes[i]=zero?(byte)0:(byte)(index+i*17);return bytes;
        }
        throw new ArgumentException(type.Name);
    }
    static void Main(string[] args){
        if(args[0]=="--folder-types"){Console.WriteLine(JsonSerializer.Serialize(Enum.GetValues<FolderType>().ToDictionary(value=>value.ToString(),value=>(int)value)));return;}
        var schema=JsonDocument.Parse(System.IO.File.ReadAllText(args[0])).RootElement;
        string line;
        while((line=Console.ReadLine())!=null){
            try{
                var words=line.Split(' ');string name=words[0];int seed=int.Parse(words[1]);
                var packet=Packet.BuildPacket(Enum.Parse<PacketType>(name));
                packet.Header.Sequence=0xff000000u+(uint)seed;packet.Header.Resent=seed==2;
                packet.Header.AppendedAcks=seed%4!=0;
                packet.Header.AckList=Enumerable.Range(0,seed%4).Select(i=>i==0?0u:0xabcdef00u+(uint)i).ToArray();
                var definition=schema.GetProperty(name);
                foreach(var block in definition.GetProperty("blocks").EnumerateArray()){
                    var field=packet.GetType().GetField(block.GetProperty("name").GetString());
                    var type=field.FieldType.IsArray?field.FieldType.GetElementType():field.FieldType;
                    int count=block.GetProperty("mode").GetString()=="Variable"?(seed<4?seed%4:seed==4?0:seed==5?17:seed==6?100:300):block.GetProperty("count").GetInt32();
                    Array rows=field.FieldType.IsArray?Array.CreateInstance(type,count):null;
                    for(int b=0;b<count;++b){
                        var row=Activator.CreateInstance(type);int f=0;
                        foreach(var item in block.GetProperty("fields").EnumerateArray()){
                            var member=type.GetField(item.GetProperty("name").GetString());
                            uint index=(uint)(f+b*17+(uint)packet.Type*13+seed*7);
                            member.SetValue(row,Fixture(member.FieldType,item.GetProperty("type").GetString(),item.GetProperty("size").GetInt32(),index,seed));++f;
                        }
                        if(rows!=null)rows.SetValue(row,b);else field.SetValue(packet,row);
                    }
                    if(rows!=null)field.SetValue(packet,rows);
                }
                if(seed>=4){var fragments=packet.ToBytesMultiple();Console.WriteLine(string.Join(",",fragments.Select(bytes=>Convert.ToHexString(bytes).ToLowerInvariant()))+"|"+(packet.Header.AppendedAcks?1:0));continue;}
                byte[] raw=packet.ToBytes(),wire=raw;
                if(packet.Header.Zerocoded){var buffer=new byte[raw.Length*2+1024];int size=Helpers.ZeroEncode(raw,raw.Length,buffer);wire=buffer[..size];}
                int end=wire.Length;
                string decoded;
                try{var value=Packet.BuildPacket(wire,ref end,new byte[65536]);decoded=value==null?"DROPPED":Convert.ToHexString(value.ToBytes()).ToLowerInvariant();}
                catch(NullReferenceException){decoded="BASELINE_FIXED_BLOCK_ERROR";}
                Console.WriteLine(Convert.ToHexString(raw).ToLowerInvariant()+"|"+Convert.ToHexString(wire).ToLowerInvariant()+"|"+
                    decoded+"|"+(packet.ValidIDs(FixtureId,FixtureId)?1:0)+"|"+(packet.ValidIDs(UUID.Zero,FixtureId)?1:0));
            }catch(Exception error){Console.WriteLine("ERROR:"+error.GetType().Name+":"+error.Message);}
        }
    }
}
