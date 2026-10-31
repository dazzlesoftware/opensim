using System;using OpenMetaverse;using OpenMetaverse.Packets;using OpenSim.Framework;
enum ThrottleOutPacketType {Task}
class UDPPacketBuffer {public byte[] Data=new byte[65536];public int DataLength;}
static class OpenSimUDPBase {public static UDPPacketBuffer GetNewUDPBuffer(object endpoint)=>new UDPPacketBuffer();}
class PacketPool {public static PacketPool Instance=new PacketPool();public Packet GetPacket(PacketType type)=>new AgentWearablesUpdatePacket();}
class Client {public object RemoteEndPoint;}
class Server {public void SendUDPPacket(Client client,UDPPacketBuffer buf,ThrottleOutPacketType type,object callback,bool zero){Emit(buf.Data,buf.DataLength);}public static void Emit(byte[] data,int n){byte[] wire=new byte[131072];int len=Helpers.ZeroEncode(data,n,wire);Console.WriteLine(Convert.ToHexString(wire,0,len).ToLowerInvariant());}}
partial class AvatarPacketsReference {
 UUID m_agentId=Id(1),m_sessionId=Id(2);Client m_udpClient=new Client();Server m_udpServer=new Server();
 void OutPacket(Packet p,ThrottleOutPacketType type){byte[] bytes=p.ToBytes();Server.Emit(bytes,bytes.Length);}
 static UUID Id(int n)=>new UUID("00000000-0000-0000-0000-"+n.ToString("x12"));
 static void Main(){string line;var host=new AvatarPacketsReference();while((line=Console.ReadLine())!=null){var words=line.Split(' ');int seed=int.Parse(words[0]),mode=int.Parse(words[1]);if(mode==0){var ws=new AvatarWearable[17];for(int i=0;i<17;++i){ws[i]=new AvatarWearable();for(int j=0;j<(seed+i)%4;++j)ws[i].Add(Id(100+i*10+j),Id(200+i*10+j));}host.SendWearables(ws,seed-100);}else{byte[] visual=new byte[(seed*11)%256],texture=new byte[(seed*17)%512];for(int i=0;i<visual.Length;++i)visual[i]=(byte)(i*7+seed);for(int i=0;i<texture.Length;++i)texture[i]=(byte)(i*13+seed);host.SendAppearance(Id(1),visual,texture,seed*.125f);}}}
}
