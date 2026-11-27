// Serialization oracle uses the actual original compiled simulator class.
using System;
using System.Collections.Generic;
using System.Linq;
using System.Reflection;
using System.Text.Json;
using OpenMetaverse;
using Motion = OpenSim.Region.Framework.Scenes.KeyframeMotion;

class Reference {
    static void Set(Motion motion, string name, object value) =>
        typeof(Motion).GetField(name, BindingFlags.Instance | BindingFlags.NonPublic).SetValue(motion, value);

    static void Main(string[] args) {
        if (args.Length > 0 && args[0] == "--decode") {
            string line;
            while ((line = Console.ReadLine()) != null) {
                var input = JsonDocument.Parse(line).RootElement;
                var decoded = Motion.FromData(null, Convert.FromHexString(input.GetProperty("nrbf").GetString()));
                Console.WriteLine(JsonSerializer.Serialize(decoded == null ? null : State(decoded)));
            }
            return;
        }
        for (int i = 0; i < 256; i++) {
            var motion =
                new Motion(null, Motion.PlayMode.PingPong, Motion.DataFormat.Translation | Motion.DataFormat.Rotation);
            float scalar = BitConverter.Int32BitsToSingle(unchecked((int)((uint)i * 16843009u)));
            var first = new Motion.Keyframe { Position = new Vector3(scalar, 2, 3),
                                              Rotation = new Quaternion(scalar, 0, .6f, .8f),
                                              TimeMS = unchecked(i * 123456789) };
            var second = new Motion.Keyframe { Position = null, Rotation = null, TimeMS = 300 };
            motion.SetKeyframes(new[] { first, second });
            var active = first;
            active.StartPosition = new Vector3(12, 20, 30);
            active.StartRotation = Quaternion.Identity;
            active.TimeTotal = 200;
            active.TimeMS = 150;
            Set(motion, "m_serializedPosition", new Vector3(12, 20, 30));
            Set(motion, "m_basePosition", new Vector3(13, 22, 33));
            Set(motion, "m_baseRotation", new Quaternion(0, 0, .6f, .8f));
            Set(motion, "m_currentFrame", active);
            Set(motion, "m_frames", new List<Motion.Keyframe> { active, second });
            Set(motion, "m_iterations", 2);
            Set(motion, "m_skipLoops", 1199);
            Set(motion, "m_mode", (Motion.PlayMode)(i % 7));
            Set(motion, "m_data", (Motion.DataFormat)(i % 9));
            Set(motion, "m_iterations", -i);
            Set(motion, "m_skipLoops", i * 17);
            Set(motion, "m_running", i % 2 == 0);
            Set(motion, "m_selected", true);
            Set(motion, "m_isCrossing", true);
            Set(motion, "m_waitingCrossing", true);
            var list = new List<Motion.Keyframe>();
            if (i % 4 != 0) {
                list.Add(active);
                list.Add(second);
                if (i % 4 == 1)
                    list.RemoveAt(0);
            }
            if (i % 8 == 0)
                list.Capacity = 8;
            Set(motion, "m_frames", list);
            var backing = (Motion.Keyframe[])Get(list, "_items");
            Set(motion, "m_keyframes", i % 5 == 0 ? null : i % 5 == 1 ? backing : new[] { first, second });
            var bytes = motion.Serialize();
            var decoded = Motion.FromData(null, bytes);
            Console.WriteLine(
                JsonSerializer.Serialize(new { nrbf = Convert.ToHexString(bytes), expected = State(decoded) }));
        }
    }

    static object Get(object o, string n) =>
        o.GetType().GetField(n, BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public).GetValue(o);
    static int B(float x) => BitConverter.SingleToInt32Bits(x);
    static int[] V(Vector3 v) => new[] { B(v.X), B(v.Y), B(v.Z) };
    static int[] Q(Quaternion q) => new[] { B(q.X), B(q.Y), B(q.Z), B(q.W) };
    static object F(Motion.Keyframe f) => new { p = f.Position.HasValue ? V(f.Position.Value) : Array.Empty<int>(),
                                                r = f.Rotation.HasValue ? Q(f.Rotation.Value) : Array.Empty<int>(),
                                                ms = f.TimeMS,
                                                total = f.TimeTotal,
                                                startP = V(f.StartPosition),
                                                startR = Q(f.StartRotation),
                                                angular = V(f.AngularVelocity) };
    static object[] Frames(IEnumerable<Motion.Keyframe> fs) => fs.Select(F).ToArray();

    static object State(Motion m) {
        var list = (List<Motion.Keyframe>)Get(m, "m_frames");
        var backing = (Motion.Keyframe[])Get(list, "_items");
        var keys = (Motion.Keyframe[])Get(m, "m_keyframes");
        return new { origin = V((Vector3)Get(m, "m_serializedPosition")),
                     @base = V((Vector3)Get(m, "m_basePosition")),
                     rotation = Q((Quaternion)Get(m, "m_baseRotation")),
                     current = F((Motion.Keyframe)Get(m, "m_currentFrame")),
                     frames = Frames(list),
                     backing = Frames(backing),
                     keys = keys == null ? null : Frames(keys),
                     version = (int)Get(list, "_version"),
                     shared = ReferenceEquals(keys, backing),
                     mode = (int)(Motion.PlayMode)Get(m, "m_mode"),
                     data = (int)(Motion.DataFormat)Get(m, "m_data"),
                     running = (bool)Get(m, "m_running"),
                     iterations = (int)Get(m, "m_iterations"),
                     skip = (int)Get(m, "m_skipLoops"),
                     timerStopped = (bool)Get(m, "m_timerStopped"),
                     selected = (bool)Get(m, "m_selected"),
                     crossing = (bool)Get(m, "m_isCrossing"),
                     waiting = (bool)Get(m, "m_waitingCrossing") };
    }
}
