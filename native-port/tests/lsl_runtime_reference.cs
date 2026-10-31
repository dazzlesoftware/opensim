using System;using System.Text;using OpenMetaverse;
enum ChatTypeEnum {Say,Owner}
class Util {public static Encoding UTF8=Encoding.UTF8;}
interface IWorldComm {void DeliverMessage(ChatTypeEnum type,int channel,string name,UUID id,string text,Vector3 position);}
class Prim {public Vector3 AbsolutePosition;public string Name="Object";public UUID UUID,OwnerID,GroupID;}
class WorldStub:IWorldComm {
 public Api Host;
 public void SimChat(byte[] text,ChatTypeEnum type,int channel,Vector3 position,string name,UUID id,bool broadcast){Host.Output+="S:"+channel+":"+Api.Hex(text)+";";}
 public void SimChatBroadcast(string text,ChatTypeEnum type,int channel,Vector3 position,string name,UUID id,bool broadcast){Host.Output+="O:"+channel+":"+Api.Hex(Encoding.UTF8.GetBytes(text))+";";}
 public void DeliverMessage(ChatTypeEnum type,int channel,string name,UUID id,string text,Vector3 position){Host.Output+="L:"+channel+":"+Api.Hex(Encoding.UTF8.GetBytes(text))+";";}
 public T RequestModuleInterface<T>() where T:class=>this as T;
}
class EngineStub {public WorldStub World;}
class TestConsole {public static Api Host;public static void WriteLine(string text){Host.Output+="C:"+Api.Hex(Encoding.UTF8.GetBytes(text))+";";}}
partial class Api {
 public string Output="";public DateTime Clock=new DateTime(638000000000000000);public bool Slept;
 protected DateTime m_lastSayShoutCheck;protected int m_SayShoutCount,m_scriptConsoleChannel;protected bool m_scriptConsoleChannelEnabled;protected float m_ScriptDelayFactor=1;
 protected Prim m_host=new();protected WorldStub World;protected EngineStub m_ScriptEngine;
 public Api(int mode,int channel){m_lastSayShoutCheck=Clock;m_host.OwnerID=new UUID("11223344-5566-7788-99aa-bbccddeeff00");if((mode&1)!=0)m_host.GroupID=m_host.OwnerID;m_scriptConsoleChannelEnabled=(mode&2)!=0;m_scriptConsoleChannel=channel;if((mode&4)!=0)m_ScriptDelayFactor=.001f;World=new WorldStub{Host=this};m_ScriptEngine=new EngineStub{World=World};TestConsole.Host=this;}
 protected virtual void Sleep(int delay){Slept=true;Output+="D:"+delay+";";}
 public static string Hex(byte[] data)=>Convert.ToHexString(data).ToLowerInvariant();
 static void Main(){string line;while((line=Console.ReadLine())!=null){var p=line.Split(' ');int mode=int.Parse(p[0]),count=int.Parse(p[1]),channel=int.Parse(p[2]);string text=p[3]=="-"?"":Encoding.UTF8.GetString(Convert.FromHexString(p[3]));var api=new Api(mode,channel);for(int i=0;i<count;i++){api.Slept=false;api.llSay(channel,text);if(api.Slept)api.Clock=api.Clock.AddSeconds(2);else if((mode&8)!=0)api.Clock=api.Clock.AddMilliseconds(500);api.llOwnerSay(text);if((mode&8)!=0)api.Clock=api.Clock.AddMilliseconds(500);}Console.WriteLine(api.Output);}}
}
