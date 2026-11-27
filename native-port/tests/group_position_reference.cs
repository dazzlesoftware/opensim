using System;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using System.Threading;
using System.Runtime.CompilerServices;
using OpenMetaverse;
using SceneObjectGroup = Group;
using SceneObjectPart = Part;

enum ChatTypeEnum { DebugChannel }

[Flags]
enum scriptEvents { changed = 1 }

class Changed {
    public const uint POSITION = 1;
}

class Utils {
    public static byte[] StringToBytes(string s) => Array.Empty<byte>();
}

class Util {
    public static void FireAndForget(WaitCallback callback, object state, string name,
                                     bool flag) => Capture.Add("cross");
}

class Manager {
    public void TriggerParcelPrimCountTainted() => Capture.Add("taint");
}

class Scene {
    public bool LoadingPrims, DisableObjectTransfer, Inside;
    public Manager EventManager = new();

    public bool PositionIsInCurrentRegion(Vector3 v) {
        Capture.Add("inside");
        return Inside;
    }

    public void SimChat(byte[] text, ChatTypeEnum kind, int channel, Vector3 p, string name, UUID id,
                        bool b) => Capture.Add("debug");
}

class Part {
    public int Index;
    public Vector3 Raw, StatusSandboxPos;
    public bool Sandbox;
    public Vector3 GroupPosition {
        get => Raw;
        set {
            Raw = value;
            Capture.Add(Index == 0 ? "root" : "child" + Index);
        }
    }
    public Vector3 AbsolutePosition => Raw;
    public bool GetStatusSandbox() => Sandbox;
    public void ScriptSetPhysicsStatus(bool b) => Capture.Add("physicsOff");
    public void TriggerScriptChangedEvent(uint flags) => Capture.Add("changed" + Index);
}

class Parts {
    public Part[] Values;
    public Part[] GetArray() => Values;
}

class ScenePresence {
    public int Index;
    public void sitSOGmoved() => Capture.Add("sitter" + Index);
}

class Entity {
    public virtual Vector3 AbsolutePosition { get; set; }
}

class Group : Entity {
    public Scene Scene;
    public Part m_rootPart;
    public bool m_dupeInProgress, IsDeleted, inTransit, Attached;
    public string Name = "Object";
    public UUID UUID;
    public Parts m_parts;
    public ScenePresence[] m_sittingAvatars;
    public Part RootPart => m_rootPart;
    public scriptEvents ScriptEvents;
    public bool IsAttachmentCheckFull() => Attached;
    Group CrossAsync(Group g, Vector3 v, object tp) => g;

    void CrossAsyncCompleted(Group g) {}

    /*SOURCE_PROPERTY*/
}

class Capture {
    static List<string> events;
    public static void Add(string s) => events.Add(s);

    static Vector3 V(JsonElement f, string key) {
        var a = f.GetProperty(key)
                    .EnumerateArray()
                    .Select(x => BitConverter.UInt32BitsToSingle(uint.Parse(x.GetString())))
                    .ToArray();
        return new(a[0], a[1], a[2]);
    }

    static bool B(JsonElement f, string key) => f.GetProperty(key).GetBoolean();

    static void Main() {
        string line;
        while ((line = Console.ReadLine()) != null) {
            events = new();
            var f = JsonDocument.Parse(line).RootElement;
            var root =
                new Part { Raw = V(f, "position"), StatusSandboxPos = V(f, "origin"), Sandbox = B(f, "sandbox") };
            var g = new Group {
                m_rootPart = root,
                inTransit = B(f, "transit"),
                Attached = B(f, "attached"),
                m_dupeInProgress = B(f, "duplicate"),
                IsDeleted = B(f, "deleted"),
                ScriptEvents = B(f, "changed") ? scriptEvents.changed : 0,
                m_parts = new Parts { Values = new[] { root, new Part { Index = 1 }, new Part { Index = 2 } } },
                m_sittingAvatars = new[] { new ScenePresence { Index = 1 }, new ScenePresence { Index = 2 } }
            };
            if (B(f, "scene"))
                g.Scene = new Scene { Inside = B(f, "inside"), LoadingPrims = B(f, "loading"),
                                      DisableObjectTransfer = B(f, "disabled") };
            g.AbsolutePosition = V(f, "target");
            Console.WriteLine(
                JsonSerializer.Serialize(new { position = new[] { root.Raw.X, root.Raw.Y, root.Raw.Z }.Select(
                                                   x => BitConverter.SingleToUInt32Bits(x).ToString()),
                                               transit = g.inTransit, events }));
        }
    }
}
