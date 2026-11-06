using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using OpenMetaverse;
namespace OpenSim.Region.PhysicsModule.ubOde
{
    class Constants
    {
        public const float MinSimulationHeight = -100, MaxSimulationHeight = 65536,
                           MinTerrainHeightmap = -100, MaxTerrainHeightmap = 4000;
    }
    enum PIDHoverType
    {
        Ground,
        GroundAndWater,
        Absolute
    }
    enum changes
    {
        Add,
        Force,
        Orientation
    }
    enum ActorTypes
    {
        Ground
    }
    class PhysicsActor
    {
        public string Name;
        public int PhysicsActorType;
    }
    class NullPhysicsActor : PhysicsActor
    {
    }
    class Logger
    {
        public void Warn(string text)
        {
        }
    }
    class ContactPoint
    {
        public float PenetrationDepth, RelativeSpeed;
        public Vector3 Position, SurfaceNormal;
        public bool CharacterFeet;
    }
    partial class OdeCharacter
    {
        uint m_baseLocalID;
        string Name;
        static Logger m_log = new();
        ODEScene m_parent_scene;
        internal Vector3 _position, _velocity, _acceleration, m_rotationalVelocity, m_targetVelocity,
            m_lastFallVel, CollideNormal;
        Vector3 _zeroPosition, m_size;
        Quaternion m_orientation;
        Vector2 Orientation2D;
        public float CapsuleRadius, CapsuleSizeZ, AvaSizeXsq, AvaSizeYsq, feetOff, boneOff;
        internal IntPtr Body, collider, Amotor;
        UBOdeNative.Quaternion m_NativeOrientation2D;
        UBOdeNative.Mass ShellMass;
        float m_mass = 80, m_massInvTimeScaled = 1600, m_density = 60, m_sceneTimeStep,
              m_sceneInverseTimeStep, m_scenegravityForceZ, m_frictionmu;
        const float basePID_D = .55f, basePID_P = .225f;
        float PID_D, PID_P, m_walkMultiplier = 1f / 1.3f, m_runMultiplier = 1f / .8f;
        bool m_pidControllerActive = true, m_freemove, _zeroFlag, m_haveLastFallVel, m_isPhysical;
        internal bool m_flying, m_alwaysRun, m_iscolliding, m_iscollidingGround;
        internal int Colliderfilter;
        int m_bodydisablecontrol, m_colliderGroundfilter, m_colliderObjectfilter;
        bool m_iscollidingObj;
        List<ContactPoint> CollisionEventsThisFrame = new();
        bool m_useHoverPID;
        float m_PIDHoverTau, m_PIDHoverHeight, m_buoyancy;
        PIDHoverType m_PIDHoverType;
        public float MinimumGroundFlightOffset = 3;
        const CollisionCategories m_collisionCategories = CollisionCategories.Character,
                                  m_collisionFlags = CollisionCategories.Character |
                                                     CollisionCategories.Geom | CollisionCategories.VolumeDtc;
        void UpdateAABB2D()
        {
        }
        void AddCollisionEvent(uint id, ContactPoint p)
        {
        }
        void AddChange(changes what, object arg)
        {
            if (what == changes.Add)
                AvatarGeomAndBodyCreate();
            else if (what == changes.Force)
                changeForce((Vector3)arg);
            else if (what == changes.Orientation)
                changeOrientation((Quaternion)arg);
        }
        public void Destroy()
        {
            AvatarGeomAndBodyDestroy();
        }
        public void Target(Vector3 v)
        {
            changeTargetVelocity(v);
        }
        internal void Filter()
        {
            if (Colliderfilter < -1)
                Colliderfilter = -1;
            else
                IsColliding = false;
        }
    }
    partial class ODEScene
    {
        internal IntPtr world, TopSpace, TerrainGeom, HeightmapData;
        internal Vector3 WorldExtents = new(256, 256, 0);
        internal float ODE_STEPSIZE = .02f, gravityz = -9.8f, AvatarFriction = 0, WaterLevel = 20;
        int m_regionWidth = 256, m_regionHeight = 256, m_lastRegionWidth, m_lastRegionHeight,
            m_heightmapWidthSamples, m_heightmapHeightSamples;
        float[] m_terrainHeights;
        GCHandle m_terrainHeightsHandler;
        object SimulationLock = new(), OdeLock = new();
        internal Dictionary<IntPtr, PhysicsActor> actor_name_map = new();
        internal ODEScene(int mode)
        {
            world = UBOdeNative.WorldCreate();
            TopSpace = UBOdeNative.SimpleSpaceCreate(IntPtr.Zero);
            UBOdeNative.WorldSetAutoDisableFlag(world, false);
            UBOdeNative.WorldSetCFM(world, .0001f);
            UBOdeNative.WorldSetERP(world, .75f);
            UBOdeNative.WorldSetGravity(world, 0, 0, -9.8f);
            UBOdeNative.WorldSetLinearDamping(world, .001f);
            UBOdeNative.WorldSetAngularDamping(world, .002f);
            UBOdeNative.WorldSetAngularDampingThreshold(world, 0);
            UBOdeNative.WorldSetLinearDampingThreshold(world, 0);
            UBOdeNative.WorldSetMaxAngularSpeed(world, .49f * (1f / .0909f) * (float)Math.PI);
            UBOdeNative.WorldSetQuickStepNumIterations(world, 15);
            UBOdeNative.WorldSetContactSurfaceLayer(world, .002f);
            UBOdeNative.WorldSetContactMaxCorrectingVel(world, 60);
            var heights = new float[65536];
            for (int y = 0; y < 256; y++)
                for (int x = 0; x < 256; x++)
                    heights[y * 256 + x] = (mode & 1) != 0 ? 21f + x * .03125f + y * .015625f : 21f;
            SetTerrain(heights);
        }
        internal void Dispose(OdeCharacter c)
        {
            if (c.Amotor != IntPtr.Zero)
                UBOdeNative.JointDestroy(c.Amotor);
            if (c.collider != IntPtr.Zero)
                UBOdeNative.GeomDestroy(c.collider);
            if (c.Body != IntPtr.Zero)
                UBOdeNative.BodyDestroy(c.Body);
            if (TerrainGeom != IntPtr.Zero)
                UBOdeNative.GeomDestroy(TerrainGeom);
            if (HeightmapData != IntPtr.Zero)
                UBOdeNative.GeomOSTerrainDataDestroy(HeightmapData);
            if (m_terrainHeightsHandler.IsAllocated)
                m_terrainHeightsHandler.Free();
            UBOdeNative.SpaceDestroy(TopSpace);
            UBOdeNative.WorldDestroy(world);
        }
    }
    class UbodeReference
    {
        static string H(float f) => BitConverter.SingleToUInt32Bits(f).ToString("x");
        static string V(Vector3 v) => H(v.X) + "," + H(v.Y) + "," + H(v.Z);
        static Quaternion Orientation(int profile) => profile switch {
            0 => new Quaternion(0, 0, .70710677f, .70710677f),
            1 => new Quaternion(.1f, .2f, .3f, .4f),
            2 => new Quaternion(0, 0, 0, 0),
            3 => new Quaternion(.01f, .02f, .03f, .99995f),
            4 => new Quaternion(1, 0, .0011f, 0),
            5 => new Quaternion(1, 0, .0009f, 0),
            6 => new Quaternion(.001f, 0, 0, 0),
            _ => new Quaternion(-.3f, .4f, -.5f, -.6f)
        };
        static string Q(Quaternion q) => H(q.X) + "," + H(q.Y) + "," + H(q.Z) + "," + H(q.W);
        static void Main(string[] args)
        {
            NativeLibrary.SetDllImportResolver(
                typeof(UBOdeNative).Assembly,
                (name, assembly, path) => name == "ubode" ? NativeLibrary.Load(args[0]) : IntPtr.Zero);
            UBOdeNative.InitODE2(0);
            UBOdeNative.AllocateODEDataForThread(~0u);
            int mode = int.Parse(args[1]);
            var scene = new ODEScene(mode);
            var c = new OdeCharacter(1, "avatar", scene, new Vector3(128, 128, (mode & 2) != 0 ? 35f : 23f),
                                     new Vector3(.45f, .6f, 1.9f), 0, 3, 1.3f, .8f);
            c.m_flying = (mode & 4) != 0;
            for (int i = 0; i < 150; i++)
            {
                bool rejected = false;
                if ((mode & 1024) != 0 && (i == 10 || i == 11 || i == 90))
                {
                    var input = Orientation((mode >> 11) & 7);
                    var normalized = input;
                    normalized.Normalize();
                    var yaw = new Vector2(normalized.Z, normalized.W);
                    yaw.Normalize();
                    if (yaw == Vector2.Zero)
                        rejected = true;
                    else
                        c.Orientation = input;
                }
                if (i == 15 && (mode & 256) != 0)
                    c.AvatarJump(9.4f);
                if (i == 70 && (mode & 512) != 0)
                    c.AvatarJump(-2f);
                if (i == 20)
                    c.Target(new Vector3(4.096f, (mode & 8) != 0 ? 2f : 0f, (mode & 16) != 0 ? 1f : 0f));
                if (i == 50)
                    c.m_alwaysRun = (mode & 32) != 0;
                if (i == 80)
                    c.Target(Vector3.Zero);
                if (i == 100)
                    c.m_flying = (mode & 64) != 0;
                if (i == 120 && (mode & 128) != 0)
                {
                    c.Destroy();
                    c = new OdeCharacter(1, "avatar", scene, new Vector3(128, 128, 23),
                                         new Vector3(.45f, .6f, 1.9f), 0, 3, 1.3f, .8f);
                    c.m_flying = (mode & 4) != 0;
                }
                c.Move();
                var force = UBOdeNative.BodyGetForce(c.Body);
                c.Filter();
                UBOdeNative.WorldQuickStep(scene.world, .02f);
                Console.WriteLine(V(c._position) + "|" + V(c._velocity) + "|" + V(c._acceleration) + "|" +
                                  V(UBOdeNative.BodyGetPositionOMV(c.Body)) + "|" +
                                  V(UBOdeNative.BodyGetLinearVelOMV(c.Body)) + "|" +
                                  V(new Vector3(force.X, force.Y, force.Z)) + "|" +
                                  ((c.m_iscolliding || c.m_iscollidingGround) ? 1 : 0) +
                                  (c.m_iscollidingGround ? 1 : 0) + "|" + Q(c.Orientation) + "|" +
                                  Q(UBOdeNative.BodyGetQuaternionOMV(c.Body)) + "|" + (rejected ? 1 : 0));
            }
            scene.Dispose(c);
            UBOdeNative.CloseODE();
        }
    }
}
