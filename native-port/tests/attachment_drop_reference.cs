// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt.
// Executes the unchanged active AttachmentsModule drop overload against recording
// scene collaborators. Does not exercise the unused SceneObjectGroup.DetachToGround.
using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;

record struct UUID(Guid Value) {
    public static UUID Zero => default;
    public bool NotEqual(UUID other) => this != other;
    public bool IsZero() => this == Zero;
    public bool IsNotZero() => !IsZero();
}
record struct Vector3(float X, float Y, float Z);

record struct Quaternion(float X, float Y, float Z, float W) {
    public static Quaternion Identity => new(0, 0, 0, 1);
    public bool NotEqual(Quaternion other) => this != other;
}

[Flags]
enum PrimFlags : uint { TemporaryOnRez = 0x20000000, Other = 8 }

class Shape {
    public byte State = 5, LastAttachPoint;
}

class SceneObjectPart {
    public Shape Shape = new();
    public Vector3 AttachedPos = new(1, 2, 3), OffsetPosition = new(7, 8, 9);
    public uint ParentID = 77;
    public PrimFlags Flags = PrimFlags.TemporaryOnRez | PrimFlags.Other;
    public DateTime Rezzed;
    public int UndoClears, FullUpdates;
    public void SetParentLocalId(uint value) => ParentID = value;
    public void RemFlag(PrimFlags value) => Flags &= ~value;
    public void ClearUndoState() => UndoClears++;
    public void ScheduleFullUpdate() => FullUpdates++;
}

class SceneObjectGroup {
    public SceneObjectPart RootPart = new();
    public UUID AttachedAvatar, FromItemID, UUID;
    public string Name = "linked attachment";
    public uint LocalId = 42;
    public int PrimCount = 2, PhysicsApplied, PermissionMask, ScriptResumes, TerseUpdates;
    public bool Backup, HasGroupChanged, PhysicsSawGroundIdentity;
    public Vector3 AbsolutePosition = new(100, 100, 30);
    public Quaternion Rotation = new(0, 0, 1, 0);
    public uint AttachmentPoint => RootPart.Shape.State;
    public void ClearPartAttachmentData() => RootPart.Shape.State = 0;
    public void UpdateGroupRotationR(Quaternion value) => Rotation = value;

    public void ApplyPhysics() {
        PhysicsApplied++;
        PhysicsSawGroundIdentity =
            RootPart.ParentID == 0 && RootPart.Shape.State == 0 && AttachedAvatar.IsZero() && FromItemID.IsZero();
    }

    public void AttachToBackup() => Backup = true;
    public void RemoveScriptsPermissions(int mask) => PermissionMask = mask;
    public void ResumeScripts() => ScriptResumes++;
    public void ScheduleGroupForTerseUpdate() => TerseUpdates++;
}

class Appearance {
    public HashSet<UUID> Items = new();
    public bool DetachAttachment(UUID item) => Items.Remove(item);
}

class Client {
    public List<UUID> Removed = new();
    public void SendRemoveInventoryItem(UUID item) => Removed.Add(item);
}

interface IScenePresence {
    UUID UUID { get; }
    Vector3 AbsolutePosition { get; }
    object AttachmentsSyncLock { get; }
    Appearance Appearance { get; }
    Client ControllingClient { get; }
    void RemoveAttachment(SceneObjectGroup group);
}

class Presence : IScenePresence {
    public UUID UUID { get; set; }
    public Vector3 AbsolutePosition { get; set; } = new(100, 100, 30);
    public object AttachmentsSyncLock { get; } = new();
    public Appearance Appearance { get; } = new();
    public Client ControllingClient { get; } = new();
    public int Removed;
    public void RemoveAttachment(SceneObjectGroup group) => Removed++;
}

class Permissions {
    public bool Allow = true;
    public int Count;
    public Vector3 Position;

    public bool CanRezObject(int count, UUID actor, Vector3 position) {
        Count = count;
        Position = position;
        return Allow;
    }
}

class AvatarFactory {
    public int Saves;
    public void QueueAppearanceSave(UUID actor) => Saves++;
}

class Inventory {
    public UUID Actor;
    public List<UUID> Deleted = new();

    public void DeleteItems(UUID actor, List<UUID> ids) {
        Actor = actor;
        Deleted.AddRange(ids);
    }
}

class Events {
    public int Taints, Attaches;
    public UUID AttachedTo;
    public void TriggerParcelPrimCountTainted() => Taints++;

    public void TriggerOnAttach(uint local, UUID group, UUID attached) {
        Attaches++;
        AttachedTo = attached;
    }
}

class Scene {
    public SceneObjectGroup Group = new();
    public Permissions Permissions = new();
    public AvatarFactory AvatarFactory = new();
    public Inventory InventoryService = new();
    public Events EventManager = new();
    public SceneObjectGroup GetGroupByPrim(uint id) => id == Group.LocalId ? Group : null;
}

class Log {
    public void Debug(string text) {}
}

class ActiveModule {
    public bool Enabled = true;
    public int DebugLevel = 0;
    public Scene m_scene;
    readonly Log m_log = new();
        public void DetachSingleAttachmentToGround(IScenePresence sp, uint soLocalId, Vector3 absolutePos, Quaternion absoluteRot)
        {
            if (!Enabled)
                return;

            if (DebugLevel > 0)
                m_log.Debug(
                    $"[ATTACHMENTS MODULE]: DetachSingleAttachmentToGround() for {sp.UUID}, object {soLocalId}");

            SceneObjectGroup so = m_scene.GetGroupByPrim(soLocalId);
            if (so is null)
                return;

            if (so.AttachedAvatar.NotEqual(sp.UUID))
                return;

            UUID inventoryID = so.FromItemID;

            // As per Linden spec, drop is disabled for temp attachs
            if (inventoryID.IsZero())
                return;

            if (DebugLevel > 0)
                m_log.Debug(
                    $"[ATTACHMENTS MODULE]: In DetachSingleAttachmentToGround(), object is {so.Name} {so.LocalId}, associated item is {inventoryID}");

            lock (sp.AttachmentsSyncLock)
            {
                if (!m_scene.Permissions.CanRezObject(so.PrimCount, sp.UUID, sp.AbsolutePosition))
                    return;

                bool changed = false;
                if (inventoryID.IsNotZero())
                    changed = sp.Appearance.DetachAttachment(inventoryID);
                if (changed && m_scene.AvatarFactory != null)
                    m_scene.AvatarFactory.QueueAppearanceSave(sp.UUID);

                so.RootPart.Shape.LastAttachPoint = (byte)so.AttachmentPoint;

                sp.RemoveAttachment(so);
                so.FromItemID = UUID.Zero;

                so.AttachedAvatar = UUID.Zero;
                so.ClearPartAttachmentData();

                SceneObjectPart rootPart = so.RootPart;

                rootPart.SetParentLocalId(0);
                so.AbsolutePosition = absolutePos;
                if (absoluteRot.NotEqual(Quaternion.Identity))
                {
                    so.UpdateGroupRotationR(absoluteRot);
                }

                rootPart.RemFlag(PrimFlags.TemporaryOnRez);

                so.ApplyPhysics();

                rootPart.Rezzed = DateTime.Now;
                so.AttachToBackup();
                m_scene.EventManager.TriggerParcelPrimCountTainted();

                rootPart.ClearUndoState();

                List<UUID> uuids = new() { inventoryID };
                m_scene.InventoryService.DeleteItems(sp.UUID, uuids);
                sp.ControllingClient.SendRemoveInventoryItem(inventoryID);
            }

            m_scene.EventManager.TriggerOnAttach(so.LocalId, so.UUID, UUID.Zero);

            // Attach (NULL) stops scripts. We don't want that. Resume them.
            so.RemoveScriptsPermissions(4 | 2048); // take controls and camera control
            so.ResumeScripts();
            so.HasGroupChanged = true;
            so.RootPart.ScheduleFullUpdate();
            so.ScheduleGroupForTerseUpdate();
        }
}

static class Program {
    static string Extract(string source) {
        const string signature = "        public void DetachSingleAttachmentToGround(IScenePresence sp, uint " +
                                 "soLocalId, Vector3 absolutePos, Quaternion absoluteRot)";
        int start = source.IndexOf(signature, StringComparison.Ordinal);
        if (start < 0)
            throw new Exception("Original active overload signature missing");
        int cursor = source.IndexOf('{', start), depth = 1;
        for (cursor++; depth != 0; cursor++) {
            if (source[cursor] == '{')
                depth++;
            if (source[cursor] == '}')
                depth--;
        }
        return source.Substring(start, cursor - start).Replace("\r\n", "\n");
    }

    static void Check(bool value, string message) {
        if (!value)
            throw new Exception(message);
    }

    static void Main(string[] args) {
        string root = Path.GetFullPath(args[0]);
        string original =
            File.ReadAllText(Path.Combine(root, "OpenSim/Region/CoreModules/Avatar/Attachments/AttachmentsModule.cs"));
        string self = File.ReadAllText(Path.Combine(root, "native-port/tests/attachment_drop_reference.cs"));
        string method = Extract(original);
        Check(Extract(self) == method, "Reference active method changed from original source");
        var results = new List<object>();
        foreach (string mode in new[] { "identity_rotation", "explicit_rotation", "rez_denied", "foreign_avatar",
                                        "temporary_attachment", "disabled" }) {
            UUID actor = new(Guid.Parse("11111111-1111-1111-1111-111111111111"));
            UUID item = new(Guid.Parse("22222222-2222-2222-2222-222222222222"));
            var sp = new Presence { UUID = actor };
            sp.Appearance.Items.Add(item);
            var scene = new Scene();
            var group = scene.Group;
            group.AttachedAvatar = actor;
            group.FromItemID = item;
            group.UUID = new(Guid.Parse("33333333-3333-3333-3333-333333333333"));
            var module = new ActiveModule { m_scene = scene };
            if (mode == "rez_denied")
                scene.Permissions.Allow = false;
            if (mode == "foreign_avatar")
                group.AttachedAvatar = UUID.Zero;
            if (mode == "temporary_attachment")
                group.FromItemID = UUID.Zero;
            if (mode == "disabled")
                module.Enabled = false;
            var oldRotation = group.Rotation;
            var rotation = mode == "explicit_rotation" ? new Quaternion(0, 1, 0, 0) : Quaternion.Identity;
            module.DetachSingleAttachmentToGround(sp, group.LocalId, new Vector3(40, 50, 30), rotation);
            bool allowed = mode == "identity_rotation" || mode == "explicit_rotation";
            Check(group.RootPart.AttachedPos == new Vector3(1, 2, 3), "Active drop changed AttachedPos");
            Check(group.RootPart.OffsetPosition == new Vector3(7, 8, 9),
                  "Active drop changed independent OffsetPosition");
            Check(scene.InventoryService.Deleted.Count == (allowed ? 1 : 0), "Inventory delete differs");
            Check(group.RootPart.ParentID == (allowed ? 0u : 77u), "Parent identity differs");
            Check(group.RootPart.Shape.State == (allowed ? 0 : 5), "Attachment state differs");
            Check(((group.RootPart.Flags & PrimFlags.TemporaryOnRez) == 0) == allowed,
                  "Temporary flag removal differs");
            Check(group.RootPart.Flags.HasFlag(PrimFlags.Other), "Unrelated flag lost");
            Check(group.Rotation == (mode == "explicit_rotation" ? rotation : oldRotation),
                  "Rotation preservation differs");
            if (allowed) {
                Check(group.RootPart.Shape.LastAttachPoint == 5 && group.AbsolutePosition == new Vector3(40, 50, 30),
                      "Dropped transform differs");
                Check(group.PhysicsSawGroundIdentity && group.Backup && group.HasGroupChanged,
                      "Physics/backup drop identity differs");
                Check(group.PermissionMask == (4 | 2048) && group.ScriptResumes == 1 && group.RootPart.UndoClears == 1,
                      "Script/undo lifecycle differs");
                Check(scene.InventoryService.Actor == actor && scene.InventoryService.Deleted[0] == item &&
                          sp.ControllingClient.Removed[0] == item,
                      "Wrong inventory item deleted");
                Check(scene.EventManager.Attaches == 1 && scene.EventManager.AttachedTo.IsZero(),
                      "Detach event differs");
            }
            results.Add(
                new { mode, allowed,
                      attached_pos = new[] { group.RootPart.AttachedPos.X, group.RootPart.AttachedPos.Y,
                                             group.RootPart.AttachedPos.Z },
                      offset_position = new[] { group.RootPart.OffsetPosition.X, group.RootPart.OffsetPosition.Y,
                                                group.RootPart.OffsetPosition.Z },
                      parent = group.RootPart.ParentID, shape_state = group.RootPart.Shape.State,
                      flags = (uint)group.RootPart.Flags, inventory_deletes = scene.InventoryService.Deleted.Count,
                      physics = group.PhysicsApplied, script_resumes = group.ScriptResumes });
        }
        Console.WriteLine(JsonSerializer.Serialize(
            new { original_method_sha256 =
                      Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(method))).ToLowerInvariant(),
                  cases = results },
            new JsonSerializerOptions { WriteIndented = true }));
    }
}
