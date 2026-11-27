using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using System.Threading;
using System.IO;
using System.Runtime.Serialization.Formatters.Binary;
using OpenMetaverse;

class Util { public static double GetTimeStampMS()=>Capture.Now; }
class KeyframeTimer {
    public static void Add(KeyframeMotion motion)=>Capture.Event((motion.Scene?.Prefix??"")+"timer_add");
    public static void Remove(KeyframeMotion motion)=>Capture.Event((motion.Scene?.Prefix??"")+"timer_remove");
}
public class Events {
    readonly string prefix;
    public Events(string prefix="") { this.prefix=prefix; }
    public void TriggerMovingStartEvent(uint id)=>Capture.Event(prefix+"moving_start");
    public void TriggerMovingEndEvent(uint id)=>Capture.Event(prefix+"moving_end");
}
public class Scene {
    public string Prefix;
    public Events EventManager;
    public Scene(string prefix="") { Prefix=prefix;EventManager=new Events(prefix); }
}
public class SceneObjectPart {
    public uint LocalId=1;
    public string Prefix="";
    public Vector3 RawVelocity,RawAngular;
    public Quaternion RawRotation=Quaternion.Identity;
    public Vector3 Velocity { get=>RawVelocity;set { Capture.Event(Prefix+Capture.Trace("velocity:",value));RawVelocity=value; } }
    public Vector3 AngularVelocity { get=>RawAngular;set { Capture.Event(Prefix+Capture.Trace("angular:",value));RawAngular=value; } }
    public Quaternion RotationOffset { get=>RawRotation;set { Capture.Event(Prefix+Capture.Trace("rotation:",value));RawRotation=value; } }
    public void ScheduleTerseUpdate()=>Capture.Event(Prefix+"terse");
}
public class SceneObjectGroup {
    public bool IsSelected;
    public Scene Scene;
    public SceneObjectPart RootPart;
    public SceneObjectGroup(string prefix="") { Scene=new Scene(prefix);RootPart=new SceneObjectPart{Prefix=prefix}; }
    public Vector3 RawPosition;
    public KeyframeMotion Motion;
    public Vector3 AbsolutePosition {
        get { if(Capture.PositionError)throw new InvalidOperationException();return RawPosition; }
        set { Capture.Event(Scene.Prefix+Capture.Trace("position:",value));if(Capture.Boundary&&value.X>=256)Motion.StartCrossingCheck();else RawPosition=value; }
    }
    public Quaternion GroupRotation=>RootPart.RawRotation;
}

/*SOURCE_CLASS*/

class Capture {
    public static double Now;
    public static bool Boundary;
    public static bool PositionError;
    static List<string> events;
    public static void Event(string value)=>events.Add(value);
    static int[] Bits(params float[] values)=>values.Select(BitConverter.SingleToInt32Bits).ToArray();
    static int[] Bits(Vector3 v)=>Bits(v.X,v.Y,v.Z);
    static int[] Bits(Quaternion q)=>Bits(q.X,q.Y,q.Z,q.W);
    public static string Trace(string name,Vector3 v)=>name+string.Join(",",Bits(v));
    public static string Trace(string name,Quaternion q)=>name+string.Join(",",Bits(q));
    static object Field(object value,string name)=>value.GetType().GetField(name,BindingFlags.Instance|BindingFlags.NonPublic).GetValue(value);
    static Vector3 Vector(JsonElement v)=>new Vector3(v[0].GetSingle(),v[1].GetSingle(),v[2].GetSingle());
    static Quaternion Rotation(JsonElement v)=>new Quaternion(v[0].GetSingle(),v[1].GetSingle(),v[2].GetSingle(),v[3].GetSingle());
    static KeyframeMotion.Keyframe[] Frames(JsonElement input) {
        var result=new List<KeyframeMotion.Keyframe>();
        foreach(var item in input.EnumerateArray()) {
            var frame=new KeyframeMotion.Keyframe();
            if(item.TryGetProperty("p",out var p)&&p.GetArrayLength()>0)frame.Position=Vector(p);
            if(item.TryGetProperty("r",out var r)&&r.GetArrayLength()>0)frame.Rotation=Rotation(r);
            frame.TimeMS=item.GetProperty("ms").GetInt32();result.Add(frame);
        }
        return result.Count==0?new KeyframeMotion.Keyframe[0]:result.ToArray();
    }
    static object Frame(KeyframeMotion.Keyframe frame)=>new {
        p=frame.Position.HasValue?Bits(frame.Position.Value):Bits(),
        r=frame.Rotation.HasValue?Bits(frame.Rotation.Value):Bits(),
        ms=frame.TimeMS,total=frame.TimeTotal,startP=Bits(frame.StartPosition),
        startR=Bits(frame.StartRotation),angular=Bits(frame.AngularVelocity)
    };
    static object State(KeyframeMotion motion,SceneObjectGroup group) {
        var keys=(KeyframeMotion.Keyframe[])Field(motion,"m_keyframes");
        var last=BitConverter.DoubleToInt64Bits((double)Field(motion,"m_lasttickMS"));
        return new {
            p=Bits(group.RawPosition),r=Bits(group.RootPart.RawRotation),v=Bits(group.RootPart.RawVelocity),a=Bits(group.RootPart.RawAngular),
            baseP=Bits((Vector3)Field(motion,"m_basePosition")),baseR=Bits((Quaternion)Field(motion,"m_baseRotation")),
            serialized=Bits((Vector3)Field(motion,"m_serializedPosition")),next=Bits((Vector3)Field(motion,"m_nextPosition")),
            currentV=Bits((Vector3)Field(motion,"m_currentVel")),current=Frame((KeyframeMotion.Keyframe)Field(motion,"m_currentFrame")),
            frames=((List<KeyframeMotion.Keyframe>)Field(motion,"m_frames")).Select(Frame).ToArray(),
            backing=((KeyframeMotion.Keyframe[])Field(Field(motion,"m_frames"),"_items")).Select(Frame).ToArray(),
            shared=ReferenceEquals(keys,Field(Field(motion,"m_frames"),"_items")),
            version=(int)Field(Field(motion,"m_frames"),"_version"),
            keysNull=keys==null,keys=(keys??Array.Empty<KeyframeMotion.Keyframe>()).Select(Frame).ToArray(),
            stopped=(bool)Field(motion,"m_timerStopped"),running=(bool)Field(motion,"m_running"),
            crossing=(bool)Field(motion,"m_isCrossing"),waiting=(bool)Field(motion,"m_waitingCrossing"),
            selected=(bool)Field(motion,"m_selected"),iterations=(int)Field(motion,"m_iterations"),skip=(int)Field(motion,"m_skipLoops"),
            last=new[]{unchecked((int)last),unchecked((int)(last>>32))},
            mode=(int)(KeyframeMotion.PlayMode)Field(motion,"m_mode"),data=(int)(KeyframeMotion.DataFormat)Field(motion,"m_data")
        };
    }
    static object Snapshot(KeyframeMotion motion,SceneObjectGroup group,string error,KeyframeMotion copied=null,SceneObjectGroup copiedGroup=null)=>new {
        original=State(motion,group),copied=copied==null?null:State(copied,copiedGroup),events=events.ToArray(),error
    };
    static void Main() {
        string line;
        while((line=Console.ReadLine())!=null) {
            var input=JsonDocument.Parse(line).RootElement;
            Now=0;PositionError=false;Boundary=input.GetProperty("boundary").GetBoolean();events=new List<string>();
            var group=new SceneObjectGroup {RawPosition=Vector(input.GetProperty("position"))};
            group.RootPart.RawRotation=Rotation(input.GetProperty("rotation"));
            var motion=new KeyframeMotion(group,(KeyframeMotion.PlayMode)input.GetProperty("mode").GetInt32(),
                (KeyframeMotion.DataFormat)(input.TryGetProperty("data",out var data)?data.GetInt32():3));
            group.Motion=motion;
            if(!input.GetProperty("nullFrames").GetBoolean())motion.SetKeyframes(Frames(input.GetProperty("frames")));
            var output=new List<object>{Snapshot(motion,group,"")};
            KeyframeMotion copied=null;SceneObjectGroup copiedGroup=null;
            foreach(var action in input.GetProperty("actions").EnumerateArray()) {
                events.Clear();string error="";
                try {
                    bool useCopy=action.TryGetProperty("target",out var name)&&name.GetString()=="copy";
                    var target=useCopy?copied:motion;
                    var targetGroup=useCopy?copiedGroup:group;
                    switch(action.GetProperty("op").GetString()) {
                        case "copy":
                            copiedGroup=new SceneObjectGroup("copy."){RawPosition=Vector(action.GetProperty("position"))};
                            copiedGroup.RootPart.RawRotation=action.TryGetProperty("rotation",out var copyRotation)?Rotation(copyRotation):group.RootPart.RawRotation;
                            copiedGroup.RootPart.RawVelocity=group.RootPart.RawVelocity;
                            copiedGroup.RootPart.RawAngular=group.RootPart.RawAngular;
                            copied=motion.Copy(copiedGroup);copiedGroup.Motion=copied;break;
                        case "serialize":target.Serialize();break;
                        case "positionError":PositionError=action.GetProperty("value").GetBoolean();break;
                        case "restore":
                            copiedGroup=new SceneObjectGroup("copy."){RawPosition=Vector(action.GetProperty("position")),
                                IsSelected=action.GetProperty("selected").GetBoolean()};
                            copiedGroup.RootPart.RawRotation=group.RootPart.RawRotation;
                            copiedGroup.RootPart.RawVelocity=group.RootPart.RawVelocity;
                            copiedGroup.RootPart.RawAngular=group.RootPart.RawAngular;
                            var originalKeys=Field(motion,"m_keyframes");
                            if(action.TryGetProperty("alias",out var alias)&&alias.GetBoolean())
                                typeof(KeyframeMotion).GetField("m_keyframes",BindingFlags.Instance|BindingFlags.NonPublic)
                                    .SetValue(motion,Field(Field(motion,"m_frames"),"_items"));
                            var saved=motion.Serialize();
                            typeof(KeyframeMotion).GetField("m_keyframes",BindingFlags.Instance|BindingFlags.NonPublic).SetValue(motion,originalKeys);
                            copied=KeyframeMotion.FromData(copiedGroup,saved);
                            copiedGroup.Motion=copied;break;
                        case "start":target.Start();break;
                        case "stop":target.Stop();break;
                        case "pause":target.Pause();break;
                        case "suspend":target.Suspend();break;
                        case "resume":target.Resume();break;
                        case "erase":target.Delete();break;
                        case "selected":target.Selected=action.GetProperty("value").GetBoolean();break;
                        case "update":target.UpdateSceneObject(action.TryGetProperty("null",out var n)&&n.GetBoolean()?null:targetGroup);break;
                        case "cross":target.StartCrossingCheck();break;
                        case "fail":target.CrossingFailure();break;
                        case "boundary":Boundary=action.GetProperty("value").GetBoolean();break;
                        case "external":
                            if(action.TryGetProperty("position",out var p))targetGroup.RawPosition=Vector(p);
                            if(action.TryGetProperty("rotation",out var r))targetGroup.RootPart.RawRotation=Rotation(r);
                            if(action.TryGetProperty("velocity",out var v))targetGroup.RootPart.RawVelocity=Vector(v);
                            break;
                        case "set":target.SetKeyframes(action.TryGetProperty("null",out var nil)&&nil.GetBoolean()?null:Frames(action.GetProperty("frames")));break;
                        case "tick":
                            int repeat=action.TryGetProperty("repeat",out var count)?count.GetInt32():1;
                            double dt=action.TryGetProperty("dt",out var delta)?delta.GetDouble():50;
                            double duration=action.TryGetProperty("duration",out var interval)?interval.GetDouble():50;
                            for(int k=0;k<repeat;++k){Now+=dt;target.OnTimer(duration);}
                            break;
                    }
                }catch(Exception ex){error=ex.GetType().Name;}
                output.Add(Snapshot(motion,group,error,copied,copiedGroup));
            }
            Console.WriteLine(JsonSerializer.Serialize(output));
        }
    }
}
