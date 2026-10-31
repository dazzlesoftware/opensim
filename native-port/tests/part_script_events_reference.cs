using System;using System.Collections.Generic;using OpenMetaverse;
interface IEmailModule {void AddPartMailBox(UUID part);void RemovePartMailBox(UUID part);}
class Email:IEmailModule {public void AddPartMailBox(UUID id){Reference.Step(4,"ADDMAIL");if(Reference.Bit(256))Reference.Group.RootPart=null;if(Reference.Bit(512))Reference.Part.ParentGroup=null;}public void RemovePartMailBox(UUID id){Reference.Step(3,"REMOVEMAIL");if(Reference.Bit(128))Reference.Part.ParentGroup=null;}}
class Scene {public T RequestModuleInterface<T>()where T:class{Reference.Step(1,"MAIL");return Reference.Bit(32)?null:new Email()as T;}}
class Group {public Scene Scene=Reference.Bit(4)?null:new();public Part RootPart;public void RemoveScriptTargets(UUID id){Reference.Step(2,"TARGETS:"+id);if(Reference.Bit(64))Reference.Part.ParentGroup=null;}public void aggregateScriptEvents()=>Reference.Step(5,"GROUP");}
partial class Part {
 public UUID UUID=Reference.Id(9);public Group ParentGroup;public scriptEvents AggregatedScriptEvents;PrimFlags m_localFlags;Dictionary<UUID,scriptEvents> m_scriptEvents=new();public bool AllowedDrop=>Reference.Bit(16);
 public void UpdatePhysicsSubscribedEvents()=>Reference.Step(6,"PHYSICS");public void ScheduleFullUpdate()=>Reference.Step(7,"FULL");
 public string Dump(){string Get(int n)=>m_scriptEvents.TryGetValue(Reference.Id(n),out var v)?((ulong)v).ToString():"-";return ((ulong)AggregatedScriptEvents)+":"+((uint)m_localFlags)+":"+Get(1)+":"+Get(2);}
 public void Seed(){AggregatedScriptEvents=(scriptEvents)0xfedcba9876543210ul;m_localFlags=(PrimFlags)0x12345678;}
}
class Reference {
 public static int Mode,Failure;public static Part Part;public static Group Group;static string Trace;
 public static bool Bit(int value)=>(Mode&value)!=0;public static UUID Id(int n)=>new("11223344-5566-7788-99aa-"+n.ToString().PadLeft(12,'0'));public static void Step(int n,string value){Trace+="|"+value;if(Failure==n)throw new Exception();}
 static string Probe(int mode,ulong first,ulong second,int action,int failure){Mode=mode;Failure=failure;Trace="";Part=new();Group=new(){RootPart=Bit(2)?null:Bit(8)?new Part():Part};Part.ParentGroup=Bit(1)?null:Group;Part.Seed();string result="OK";try{if(action==0){Part.SetScriptEvents(Id(1),first);Part.SetScriptEvents(Id(1),first);Part.SetScriptEvents(Id(2),second);Part.RemoveScriptEvents(Id(3));Part.RemoveScriptEvents(Id(1));Part.aggregateScriptEvents();Part.RemoveScriptEvents(Id(2));}else if(action==1){Part.SetScriptEvents(Id(1),first);Part.RemoveScriptEvents(Id(1));}else if(action==2)Part.RemoveScriptEvents(Id(1));else Part.aggregateScriptEvents();}catch{result="THROW";}return result+":"+Part.Dump()+Trace;}
 static void Main(){string line;while((line=Console.ReadLine())!=null){var a=line.Split(' ');Console.WriteLine(Probe(int.Parse(a[0]),ulong.Parse(a[1]),ulong.Parse(a[2]),int.Parse(a[3]),int.Parse(a[4])));}}
}
