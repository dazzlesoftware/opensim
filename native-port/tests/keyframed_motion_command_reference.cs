using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Text.Json;
using OpenMetaverse;
using OMV_Vector3 = OpenMetaverse.Vector3;
using OMV_Quaternion = OpenMetaverse.Quaternion;
using LSL_List = LSL_Types.list;

namespace LSL_Types {
    public struct LSLInteger { public int value; }
    public struct LSLFloat { public double value; }
    public struct LSLString { public string m_string; }
    public struct Vector3 {
        public double x,y,z;
        public Vector3(OMV_Vector3 value) { x=value.X;y=value.Y;z=value.Z; }
    }
    public struct Quaternion {
        public double x,y,z,s;
        public Quaternion(OMV_Quaternion value) { x=value.X;y=value.Y;z=value.Z;s=value.W; }
        /*NORMALIZE*/
    }
    public class list {
        public object[] Data;
        public list(object[] items) { Data=items; }
        /*INTEGER*/
        /*FLOAT*/
        /*VECTOR*/
        /*ROTATION*/
    }
}

class ScriptBaseClass {
    public const int KFM_MODE=1,KFM_DATA=2,KFM_COMMAND=0;
    public const int KFM_FORWARD=0,KFM_REVERSE=3,KFM_LOOP=1,KFM_PING_PONG=2;
    public const int KFM_CMD_PLAY=0,KFM_CMD_STOP=1,KFM_CMD_PAUSE=2;
}

class Reference {
    static List<string> events;
    static string throws;
    static void Event(string name) {
        events.Add(name);
        if(throws==name) throw new InvalidOperationException("controlled "+name);
    }
    class PhysicsActor { public bool IsPhysical; }
    class Part {
        public PhysicsActor PhysActor;
        public KeyframeMotion motion;
        public KeyframeMotion KeyframeMotion {
            get=>motion;
            set { if(value==null) Event("clear"); motion=value; }
        }
    }
    class SceneObjectGroup { public Part RootPart=new Part(); public bool IsAttachment; }
    class Host { public SceneObjectGroup ParentGroup=new SceneObjectGroup(); }
    Host m_host=new Host();
    class KeyframeMotion {
        public enum PlayMode { Forward,Reverse,Loop,PingPong }
        public enum DataFormat { Rotation=1,Translation=2 }
        public struct Keyframe { public OMV_Vector3? Position;public OMV_Quaternion? Rotation;public int TimeMS; }
        public PlayMode mode=(PlayMode)(-1);
        public DataFormat data=(DataFormat)(-1);
        public Keyframe[] frames;
        public KeyframeMotion() {}
        public KeyframeMotion(SceneObjectGroup group,PlayMode mode,DataFormat data) { Event("create");this.mode=mode;this.data=data; }
        public void Delete()=>Event("delete");
        public void SetKeyframes(Keyframe[] frames) { Event("set");this.frames=frames; }
        public void Start()=>Event("start");
        public void Stop()=>Event("stop");
        public void Pause()=>Event("pause");
    }
    /*API*/
    static LSL_List List(JsonElement input) {
        var items=new List<object>();
        foreach(var item in input.EnumerateArray()) {
            var value=item[1];
            switch(item[0].GetString()) {
                case "i":items.Add(new LSL_Types.LSLInteger{value=value.GetInt32()});break;
                case "f":items.Add(new LSL_Types.LSLFloat{value=value.GetDouble()});break;
                case "s":items.Add(new LSL_Types.LSLString{m_string=value.GetString()});break;
                case "v":items.Add(new LSL_Types.Vector3{x=value[0].GetDouble(),y=value[1].GetDouble(),z=value[2].GetDouble()});break;
                case "q":items.Add(new LSL_Types.Quaternion{x=value[0].GetDouble(),y=value[1].GetDouble(),z=value[2].GetDouble(),s=value[3].GetDouble()});break;
                default:items.Add(new LSL_List(Array.Empty<object>()));break;
            }
        }
        return new LSL_List(items.ToArray());
    }
    static int[] Bits(params float[] values)=>values.Select(BitConverter.SingleToInt32Bits).ToArray();
    static void Main() {
        CultureInfo.CurrentCulture=CultureInfo.InvariantCulture;
        string line;
        while((line=Console.ReadLine())!=null) {
            var f=JsonDocument.Parse(line).RootElement;
            events=new List<string>();throws=f.GetProperty("throws").GetString();
            var host=new Reference();var group=host.m_host.ParentGroup;
            if(f.GetProperty("physical").GetBoolean()) group.RootPart.PhysActor=new PhysicsActor{IsPhysical=true};
            group.IsAttachment=f.GetProperty("attachment").GetBoolean();
            if(f.GetProperty("existing").GetBoolean()) group.RootPart.motion=new KeyframeMotion();
            string error="",message="";
            try { host.llSetKeyframedMotion(List(f.GetProperty("frames")),List(f.GetProperty("options"))); }
            catch(Exception ex) { error=ex.GetType().Name;message=ex.Message; }
            var motion=group.RootPart.motion;
            Console.WriteLine(JsonSerializer.Serialize(new {
                present=motion!=null, mode=motion==null?-1:(int)motion.mode, data=motion==null?-1:(int)motion.data,
                framesSet=motion?.frames!=null,
                frames=(motion?.frames??Array.Empty<KeyframeMotion.Keyframe>()).Select(k=>new {
                    p=k.Position.HasValue?Bits(k.Position.Value.X,k.Position.Value.Y,k.Position.Value.Z):Bits(),
                    r=k.Rotation.HasValue?Bits(k.Rotation.Value.X,k.Rotation.Value.Y,k.Rotation.Value.Z,k.Rotation.Value.W):Bits(),ms=k.TimeMS}),
                events,error,message
            }));
        }
    }
}
