using System;using System.Collections.Generic;using OpenMetaverse;using OpenMetaverse.StructuredData;using OpenMetaverse.Packets;
interface IEventQueue {osUTF8 StartEvent(string name);byte[] EndEventToBytes(osUTF8 b);bool Enqueue(byte[] b,UUID a);}
static class OSUTF8Cached {public static osUTF8 Acquire()=>new osUTF8(1024);public static osUTF8 Acquire(int n)=>new osUTF8(n);public static byte[] GetArrayAndRelease(osUTF8 b)=>b.ToArray();}
partial class Eq:IEventQueue {public bool Enqueue(byte[] bytes,UUID agent){NotificationReference.Trace+="|E:"+agent+":"+Convert.ToHexString(bytes).ToLowerInvariant();if((NotificationReference.Mode&32)!=0)throw new Exception();return (NotificationReference.Mode&16)==0;}}
class Scene {public T RequestModuleInterface<T>()where T:class=>(NotificationReference.Mode&1)!=0?null:new Eq() as T;}
class Logger {public void DebugFormat(string s){NotificationReference.Trace+="|D";if((NotificationReference.Mode&64)!=0)throw new Exception();}}
enum ThrottleOutPacketType {Task}
class PacketPool {public static PacketPool Instance=new();public Packet GetPacket(PacketType t)=>Packet.BuildPacket(t);}
static class Util {public static byte[] StringToBytes256(ReadOnlySpan<char> s)=>Utils.StringToBytes(s,255);}
partial class Client {
 public Scene Scene=new();Logger m_log=new();UUID m_agentId;public UUID SessionId;
 public Client(){m_agentId=(NotificationReference.Mode&4)!=0?UUID.Zero:NotificationReference.Id(1);SessionId=(NotificationReference.Mode&8)!=0?UUID.Zero:NotificationReference.Id(2);}
 void OutPacket(Packet p,ThrottleOutPacketType t){var raw=p.ToBytes();var wire=new byte[raw.Length*2+32];int n;if(p.Header.Zerocoded)n=Helpers.ZeroEncode(raw,raw.Length,wire);else{n=raw.Length;Array.Copy(raw,wire,n);}NotificationReference.Trace+="|"+Convert.ToHexString(raw).ToLowerInvariant()+"|"+Convert.ToHexString(wire,0,n).ToLowerInvariant();}
}
class NotificationReference {
 public static int Mode;public static string Trace;
 public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));
 static string[] Messages()=>new[]{null,"","message","ALERT: message","NOTIFY: message","alert: message","Home position set.","You died and have been teleported to your home location",new string('a',300),string.Concat(System.Linq.Enumerable.Repeat("é中😃",100)),"abc\0suffix","ALERT:","NOTIFY:"," ALERT: message","\u00adALERT: message","A\u00adLERT: message","\0ALERT: message","\r\nmessage"};
 static string Probe(int kind,int mode,int count){Mode=mode;Trace="";string result="OK";try{var c=new Client();if(kind==2)c.SendAgentAlertMessage(Messages()[count],(mode&1)!=0);else{var list=new List<UUID>();for(int n=0;n<count;++n)list.Add(n%3==0?UUID.Zero:Id(n%2+3));UUID[] ids=(mode&2)!=0?null:list.ToArray();if(kind==0)c.SendRemoveInventoryItems(ids);else c.SendRemoveInventoryFolders(ids);}}catch{result="THROW";}return result+Trace;}
 static void Main(){System.Globalization.CultureInfo.CurrentCulture=System.Globalization.CultureInfo.InvariantCulture;string line;while((line=Console.ReadLine())!=null){var a=Array.ConvertAll(line.Split(' '),int.Parse);Console.WriteLine(Probe(a[0],a[1],a[2]));}}
}

