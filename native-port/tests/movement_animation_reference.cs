using System;
using OpenMetaverse;
class SceneOptions
{
    public bool m_useFlySlow;
}
class PhysicsActor
{
    public ScenePresenceAnimator Host;
    internal bool flying;
    public bool IsColliding, PIDHoverActive;
    public Vector3 Velocity;
    public bool Flying
    {
        get => flying;
        set {
            flying = value;
            Host.Trace += "F:" + (value ? 1 : 0) + ";";
        }
    }
    public bool Selected
    {
        set {
            Host.Trace += "S:" + (value ? 1 : 0) + ";";
        }
    }
}
class ScenePresence
{
    public bool IsInTransit, SitGround, AgentControlStopActive, SetAlwaysRun;
    public uint ParentID, AgentControlFlags;
    public UUID ParentUUID;
    public PhysicsActor PhysicsActor;
    public SceneOptions Scene = new();
    public ScenePresenceAnimator Host;
    public void Jump(float force)
    {
        Host.Trace += "J:" + ScenePresenceAnimator.H(force) + ";";
    }
}
partial class ScenePresenceAnimator
{
    object m_animations = new();
    public string CurrentMovementAnimation { get; private set; }
    public bool isJumping, Falling;
    float m_lastFallVelocity;
    int m_animTickFall, m_animTickLand, m_animTickJump;
    ScenePresence m_scenePresence;
    public int Clock, Mode;
    public string Trace = "";
    public bool AppliedAttempt;
    public motionControlStates currentControlState = motionControlStates.onsurface;
    public bool TrySetMovementAnimation(string text)
    {
        AppliedAttempt = true;
        Trace += "A:" + text + ";";
        return (Mode & 128) == 0;
    }
    public static string H(float f) => BitConverter.SingleToUInt32Bits(f).ToString("x");
    static void Input(ScenePresence c, PhysicsActor actor, int frame, int mode)
    {
        c.IsInTransit = c.SitGround = c.AgentControlStopActive = c.SetAlwaysRun = false;
        c.ParentID = 0;
        c.ParentUUID = UUID.Zero;
        c.AgentControlFlags = 0;
        actor.IsColliding = true;
        actor.PIDHoverActive = false;
        actor.Velocity = Vector3.Zero;
        actor.flying = false;
        c.Scene.m_useFlySlow = (mode & 1) != 0;
        c.SetAlwaysRun = frame == 2;
        c.AgentControlStopActive = frame == 3;
        if (frame >= 1 && frame <= 3)
            c.AgentControlFlags = 1;
        if (frame >= 4 && frame <= 7)
            c.AgentControlFlags = (mode & 32) != 0 ? 1u << 23 : 16;
        if (frame == 4 && (mode & 2) != 0)
            c.AgentControlFlags |= 0x8000;
        if (frame >= 8 && frame <= 10)
        {
            actor.IsColliding = false;
            actor.Velocity.Z = frame == 8 ? 2f : -8f;
        }
        if (frame >= 14 && frame <= 19)
        {
            actor.IsColliding = false;
            actor.Velocity.Z = (mode & 4096) != 0   ? -.5f
                               : frame == 14        ? -4f
                               : (mode & 2048) != 0 ? -10f
                               : (mode & 1024) != 0 ? -14f
                                                    : -20f;
        }
        if (frame == 21 && (mode & 2) != 0)
            c.AgentControlFlags |= 0x8000;
        if (frame >= 23 && frame <= 27)
        {
            actor.flying = true;
            c.AgentControlFlags = (uint)(frame == 23 ? 1 : frame == 24 ? 16 : frame >= 26 ? 32 : 0);
            actor.IsColliding = frame == 27;
        }
        if (frame == 29)
            c.AgentControlFlags = 33;
        if (frame == 30)
            c.AgentControlFlags = 0x2000000;
        if (frame == 31)
            c.AgentControlFlags = 0x4000000;
        c.PhysicsActor = (mode & 4) != 0 && frame % 11 == 0 ? null : actor;
        if ((mode & 8) != 0 && frame % 13 == 0)
            c.SitGround = true;
        if ((mode & 16) != 0 && frame % 17 == 0)
            c.ParentID = 5;
        if ((mode & 16) != 0 && frame % 19 == 0)
            c.ParentUUID = new UUID("00112233-4455-6677-8899-aabbccddeeff");
        if ((mode & 64) != 0 && frame % 7 == 0)
            c.IsInTransit = true;
        if ((mode & 256) != 0 && frame >= 14 && frame <= 19)
            actor.PIDHoverActive = true;
    }
    static void Main()
    {
        string line;
        while ((line = Console.ReadLine()) != null)
        {
            var parts = line.Split(' ');
            int mode = int.Parse(parts[0]), basis = int.Parse(parts[1]);
            var context = new ScenePresence();
            var animator = new ScenePresenceAnimator(context) { Mode = mode };
            context.Host = animator;
            var actor = new PhysicsActor { Host = animator };
            for (int frame = 0; frame < 32; frame++)
            {
                animator.Clock = unchecked(basis + frame * 200);
                Input(context, actor, frame, mode);
                animator.Trace = "";
                animator.AppliedAttempt = false;
                var old = animator.CurrentMovementAnimation;
                bool applied = (mode & 512) != 0 && frame % 3 == 0 ? animator.ForceUpdateMovementAnimations()
                                                                   : animator.UpdateMovementAnimations();
                Console.WriteLine(
                    animator.CurrentMovementAnimation + "|" + (int)animator.currentControlState + "|" +
                    (animator.isJumping ? 1 : 0) + (animator.Falling ? 1 : 0) + "|" +
                    animator.m_animTickFall + "," + animator.m_animTickLand + "," + animator.m_animTickJump +
                    "|" + H(animator.m_lastFallVelocity) + "|" + (actor.flying ? 1 : 0) + "|" +
                    (old != animator.CurrentMovementAnimation ? 1 : 0) + (animator.AppliedAttempt ? 1 : 0) +
                    (applied ? 1 : 0) + "|" + animator.Trace);
            }
        }
    }
}
