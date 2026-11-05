// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt. ODE ABI adapted from source ODEApi.cs.
#include "opensim/physics/ubode.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#define ODE_CALL __cdecl
#else
#include <dlfcn.h>
#define ODE_CALL
#endif
namespace opensim::physics {
namespace {
struct Vec {
    float X = 0, Y = 0, Z = 0;
    bool IsZero() const {
        return X == 0 && Y == 0 && Z == 0;
    }
    float LengthSquared() const {
        return (X * X + Y * Y) + Z * Z;
    }
    static float Dot(Vec a, Vec b) {
        return (a.X * b.X + a.Y * b.Y) + a.Z * b.Z;
    }
};
Vec operator+(Vec a, Vec b) {
    return {a.X + b.X, a.Y + b.Y, a.Z + b.Z};
}
Vec operator-(Vec a, Vec b) {
    return {a.X - b.X, a.Y - b.Y, a.Z - b.Z};
}
Vec operator-(Vec a) {
    return {-a.X, -a.Y, -a.Z};
}
Vec operator*(Vec a, float b) {
    return {a.X * b, a.Y * b, a.Z * b};
}
Vec from(framework::Vector3 a) {
    return {a.x, a.y, a.z};
}
framework::Vector3 to(Vec a) {
    return {a.X, a.Y, a.Z};
}
bool finite(Vec a) {
    return std::isfinite(a.X) && std::isfinite(a.Y) && std::isfinite(a.Z);
}
bool sdk_sse41() {
    static const bool enabled = []() {
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
        int info[4];
        __cpuid(info, 1);
        return (info[2] & (1 << 19)) != 0;
#elif defined(__x86_64__) || defined(__i386__)
        unsigned a, b, c, d;
        return __get_cpuid(1, &a, &b, &c, &d) && (c & bit_SSE4_1) != 0;
#else
        return false;
#endif
    }();
    return enabled;
}
framework::Quaternion normalize(framework::Quaternion q) {
    float sum = sdk_sse41() ? (q.x * q.x + q.y * q.y) + (q.z * q.z + q.w * q.w)
                            : ((q.x * q.x + q.y * q.y) + q.z * q.z) + q.w * q.w;
    if (!(sum > 1.e-6f))
        return {0, 0, 0, 1};
    float length = std::sqrt(sum);
    if (sdk_sse41()) {
        q.x /= length;
        q.y /= length;
        q.z /= length;
        q.w /= length;
    } else {
        float inverse = 1.f / length;
        q.x *= inverse;
        q.y *= inverse;
        q.z *= inverse;
        q.w *= inverse;
    }
    return q;
}
struct NativeQuaternion {
    float W = 1, X = 0, Y = 0, Z = 0;
};
struct NativeVector {
    float X, Y, Z, padding;
};
struct NativeContact {
    int mode = 0x3004;
    float mu = 0, mu2 = 0, bounce = 0, bounce_velocity = 1.5f, erp = .75f, cfm = .0001f;
    float motion1 = 0, motion2 = 0, motion_normal = 0, slip1 = 0, slip2 = 0;
    NativeVector position{}, normal{};
    float depth = 0;
    void* geometry1 = nullptr;
    void* geometry2 = nullptr;
    int side1 = -1, side2 = -1;
    NativeVector direction{};
};
static_assert(sizeof(NativeContact) == 128 && offsetof(NativeContact, geometry1) == 88);
struct Mass {
    float mass;
    float c[4];
    float inertia[12];
};
static_assert(sizeof(void*) == 8 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
                  std::endian::native == std::endian::little,
              "ubODE profile requires64-bit little-endian IEEE binary32 ABI");
static_assert(sizeof(NativeVector) == 16 && sizeof(NativeQuaternion) == 16 && sizeof(Mass) == 68);
struct ContactPoint {
    float PenetrationDepth;
    Vec Position, SurfaceNormal;
    float RelativeSpeed;
    bool CharacterFeet;
};
float sign(float v) {
    return v > 0 ? 1.f : v < 0 ? -1.f : 0.f;
}
// MathF.Round uses midpoint-to-even. Arithmetic remains binary32 at each source operation.
float rounded(float v) {
    if (!std::isfinite(v) || std::abs(v) >= 1.e8f)
        return v;
    float scaled = v * 100.f;
    double d = scaled, lo = std::floor(d), fraction = d - lo;
    if (fraction > .5 || (fraction == .5 && std::fmod(lo, 2.) != 0))
        lo++;
    float result = static_cast<float>(lo) / 100.f;
    return result == 0 ? std::copysign(0.f, v) : result;
}
void SetSmooth(Vec& dst, const Vec& value) {
    dst.X = rounded(.4f * dst.X + .6f * value.X);
    dst.Y = rounded(.4f * dst.Y + .6f * value.Y);
    dst.Z = rounded(.4f * dst.Z + .6f * value.Z);
}
#define ODE_FUNCTIONS(X)                                                                                     \
    X(const char*, GetConfiguration, (), "dGetConfiguration")                                                \
    X(int, InitODE2, (unsigned), "dInitODE2")                                                                \
    X(void, CloseODE, (), "dCloseODE")                                                                       \
    X(int, AllocateODEDataForThread, (unsigned), "dAllocateODEDataForThread")                                \
    X(void*, WorldCreate, (), "dWorldCreate")                                                                \
    X(void, WorldDestroy, (void*), "dWorldDestroy")                                                          \
    X(void*, SimpleSpaceCreate, (void*), "dSimpleSpaceCreate")                                               \
    X(void, SpaceDestroy, (void*), "dSpaceDestroy")                                                          \
    X(void, WorldSetAutoDisableFlag, (void*, int), "dWorldSetAutoDisableFlag")                               \
    X(void, WorldSetCFM, (void*, float), "dWorldSetCFM")                                                     \
    X(void, WorldSetERP, (void*, float), "dWorldSetERP")                                                     \
    X(void, WorldSetGravity, (void*, float, float, float), "dWorldSetGravity")                               \
    X(void, WorldSetLinearDamping, (void*, float), "dWorldSetLinearDamping")                                 \
    X(void, WorldSetAngularDamping, (void*, float), "dWorldSetAngularDamping")                               \
    X(void, WorldSetAngularDampingThreshold, (void*, float), "dWorldSetAngularDampingThreshold")             \
    X(void, WorldSetLinearDampingThreshold, (void*, float), "dWorldSetLinearDampingThreshold")               \
    X(void, WorldSetMaxAngularSpeed, (void*, float), "dWorldSetMaxAngularSpeed")                             \
    X(void, WorldSetQuickStepNumIterations, (void*, int), "dWorldSetQuickStepNumIterations")                 \
    X(void, WorldSetContactSurfaceLayer, (void*, float), "dWorldSetContactSurfaceLayer")                     \
    X(void, WorldSetContactMaxCorrectingVel, (void*, float), "dWorldSetContactMaxCorrectingVel")             \
    X(void, WorldQuickStep, (void*, float), "dWorldQuickStep")                                               \
    X(void*, CreateCapsule, (void*, float, float), "dCreateCapsule")                                         \
    X(void, GeomCapsuleSetParams, (void*, float, float), "dGeomCapsuleSetParams")                            \
    X(void, GeomDestroy, (void*), "dGeomDestroy")                                                            \
    X(void, GeomSetCategoryBits, (void*, unsigned), "dGeomSetCategoryBits")                                  \
    X(void, GeomSetCollideBits, (void*, unsigned), "dGeomSetCollideBits")                                    \
    X(void, MassSetBoxTotal, (Mass*, float, float, float, float), "dMassSetBoxTotal")                        \
    X(void*, BodyCreate, (void*), "dBodyCreate")                                                             \
    X(void, BodyDestroy, (void*), "dBodyDestroy")                                                            \
    X(void, BodySetAutoDisableFlag, (void*, int), "dBodySetAutoDisableFlag")                                 \
    X(void, BodySetPosition, (void*, float, float, float), "dBodySetPosition")                               \
    X(void, BodySetMass, (void*, const Mass*), "dBodySetMass")                                               \
    X(void, GeomSetBody, (void*, void*), "dGeomSetBody")                                                     \
    X(void*, JointCreateAMotor, (void*, void*), "dJointCreateAMotor")                                        \
    X(void*, JointCreateContact, (void*, void*, const NativeContact*), "dJointCreateContact")                \
    X(void*, JointGroupCreate, (int), "dJointGroupCreate")                                                   \
    X(void, JointGroupEmpty, (void*), "dJointGroupEmpty")                                                    \
    X(void, JointGroupDestroy, (void*), "dJointGroupDestroy")                                                \
    X(void, JointAttach, (void*, void*, void*), "dJointAttach")                                              \
    X(void, JointSetAMotorMode, (void*, int), "dJointSetAMotorMode")                                         \
    X(void, JointSetAMotorNumAxes, (void*, int), "dJointSetAMotorNumAxes")                                   \
    X(void, JointSetAMotorAxis, (void*, int, int, float, float, float), "dJointSetAMotorAxis")               \
    X(void, JointSetAMotorAngle, (void*, int, float), "dJointSetAMotorAngle")                                \
    X(void, JointSetAMotorParam, (void*, int, float), "dJointSetAMotorParam")                                \
    X(void, JointDestroy, (void*), "dJointDestroy")                                                          \
    X(int, BodyIsEnabled, (void*), "dBodyIsEnabled")                                                         \
    X(void, BodyEnable, (void*), "dBodyEnable")                                                              \
    X(void, BodySetAngularVel, (void*, float, float, float), "dBodySetAngularVel")                           \
    X(void, BodySetLinearVel, (void*, float, float, float), "dBodySetLinearVel")                             \
    X(void, BodySetQuaternion, (void*, const NativeQuaternion*), "dBodySetQuaternion")                       \
    X(void, BodyAddForce, (void*, float, float, float), "dBodyAddForce")                                     \
    X(const float*, BodyGetQuaternion, (void*), "dBodyGetQuaternion")                                        \
    X(const float*, BodyGetForce, (void*), "dBodyGetForce")                                                  \
    X(const float*, BodyGetPosition, (void*), "dBodyGetPosition")                                            \
    X(const float*, BodyGetLinearVel, (void*), "dBodyGetLinearVel")                                          \
    X(const float*, BodyGetAngularVel, (void*), "dBodyGetAngularVel")
struct Api {
    void* module = nullptr;
    bool initialized = false;
    std::string config;
    std::recursive_mutex mutex;
#define DECLARE(ret, name, args, symbol) ret(ODE_CALL* name) args = nullptr;
    ODE_FUNCTIONS(DECLARE)
#undef DECLARE
    Vec BodyGetPositionOMV(void* body) {
        auto p = BodyGetPosition(body);
        return {p[0], p[1], p[2]};
    }
    Vec BodyGetLinearVelOMV(void* body) {
        auto p = BodyGetLinearVel(body);
        return {p[0], p[1], p[2]};
    }
    Vec BodyGetAngularVelOMVforAvatar(void* body) {
        auto p = BodyGetAngularVel(body);
        return {p[0], p[1], p[2]};
    }
    void allocate() {
        if (!AllocateODEDataForThread(~0u))
            throw std::runtime_error("ubODE thread allocation failed");
    }
    ~Api();
};
std::recursive_mutex& library_mutex() {
    static std::recursive_mutex value;
    return value;
}
std::map<void*, std::shared_ptr<Api>>& libraries() {
    static std::map<void*, std::shared_ptr<Api>> value;
    return value;
}
// Registry owns a strong reference until the last world lease is released under
// its lock. A weak registry could reinitialize while the old final destructor
// waits for this lock, then close the newly initialized process-global engine.
struct Lease {
    std::shared_ptr<Api> value;
    explicit Lease(std::shared_ptr<Api> v) : value(std::move(v)) {
    }
    Api* operator->() const {
        return value.get();
    }
    ~Lease() {
        std::lock_guard guard(library_mutex());
        auto it = libraries().find(value->module);
        if (it != libraries().end() && it->second == value && value.use_count() == 2)
            libraries().erase(it);
        value.reset();
    }
};
void unload(void* module) {
    if (!module)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}
Api::~Api() {
    std::lock_guard lock(library_mutex());
    if (initialized)
        CloseODE();
    libraries().erase(module);
    unload(module);
}
void* lookup(void* module, const char* symbol) {
#ifdef _WIN32
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), symbol));
#else
    return dlsym(module, symbol);
#endif
}
std::shared_ptr<Api> load(const std::filesystem::path& path) {
    auto absolute = std::filesystem::canonical(path);
    std::lock_guard guard(library_mutex());
    void* handle = nullptr;
#ifdef _WIN32
    handle = LoadLibraryExW(absolute.c_str(), nullptr,
                            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    handle = dlopen(absolute.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!handle)
        throw std::runtime_error("Cannot load explicitly selected ubODE library");
    if (auto existing = libraries()[handle]) {
        unload(handle);
        return existing;
    }
    auto api = std::make_shared<Api>();
    api->module = handle;
#define LOAD(ret, name, args, symbol)                                                                        \
    {                                                                                                        \
        void* address = nullptr;                                                                             \
        address = lookup(handle, symbol);                                                                    \
        if (!address)                                                                                        \
            throw std::runtime_error("ubODE missing ABI symbol: " symbol);                                   \
        std::memcpy(&api->name, &address, sizeof(address));                                                  \
    }
    ODE_FUNCTIONS(LOAD)
#undef LOAD
    auto config = api->GetConfiguration();
    if (!config)
        throw std::runtime_error("ubODE configuration unavailable");
    api->config = config;
    if (api->config.find("ODE_single_precision") == std::string::npos ||
        api->config.find("ODE_OPENSIM") == std::string::npos ||
        api->config.find("OS0.13.4") == std::string::npos)
        throw std::runtime_error(
            "Unsupported ubODE ABI/configuration; expected single-precision OpenSim OS0.13.4");
    if (!api->InitODE2(0))
        throw std::runtime_error("ubODE initialization failed");
    api->initialized = true;
    api->allocate();
    libraries()[handle] = api;
    return api;
}
struct OdeScene {
    float water_height = 20.f;
    Lease api;
    void* world = nullptr;
    void* space = nullptr;
    void* contacts = nullptr;
    std::vector<float> terrain_heights;

    explicit OdeScene(std::shared_ptr<Api> value) : api(std::move(value)) {
    }
    void initialize(float frame_time) {
        std::lock_guard guard(api->mutex);
        api->allocate();
        world = api->WorldCreate();
        space = api->SimpleSpaceCreate(nullptr);
        contacts = api->JointGroupCreate(0);
        if (!world || !space || !contacts)
            throw std::runtime_error("ubODE world creation failed");
        api->WorldSetAutoDisableFlag(world, 0);
        api->WorldSetCFM(world, .0001f);
        api->WorldSetERP(world, .75f);
        api->WorldSetGravity(world, 0, 0, -9.8f);
        api->WorldSetLinearDamping(world, .001f);
        api->WorldSetAngularDamping(world, .002f);
        api->WorldSetAngularDampingThreshold(world, 0);
        api->WorldSetLinearDampingThreshold(world, 0);
        api->WorldSetMaxAngularSpeed(world, .49f * (1.f / frame_time) * 3.14159265358979323846f);
        api->WorldSetQuickStepNumIterations(world, 15);
        api->WorldSetContactSurfaceLayer(world, .002f);
        api->WorldSetContactMaxCorrectingVel(world, 60);
    }
    ~OdeScene() {
        if (!world && !space && !contacts)
            return;
        std::lock_guard guard(api->mutex);
        try {
            api->allocate();
        } catch (...) {
            return;
        }
        if (contacts)
            api->JointGroupDestroy(contacts);
        if (space)
            api->SpaceDestroy(space);
        if (world)
            api->WorldDestroy(world);
    }
};
} // namespace
struct UbodeWorld::Impl {
    std::shared_ptr<OdeScene> native_scene;
    Lease& api = native_scene->api;
    void*& world = native_scene->world;
    void*& TopSpace = native_scene->space;
    std::thread::id owner = std::this_thread::get_id();
    void* Body = nullptr;
    void* collider = nullptr;
    void* Amotor = nullptr;
    NativeQuaternion m_NativeOrientation2D;
    framework::Quaternion orientation{0, 0, 0, 1};
    Mass ShellMass{};
    Vec WorldExtents, _position, _zeroPosition, _velocity, _acceleration, m_rotationalVelocity,
        m_targetVelocity, m_lastFallVel, CollideNormal, last_force;
    float CapsuleSizeZ = 0, PID_D = 0, PID_P = 0, m_mass = 0, m_massInvTimeScaled = 0,
          m_scenegravityForceZ = 0;
    float capsule_radius = 0, half_x_squared = 0, half_y_squared = 0;
    const float m_sceneTimeStep = .02f, m_sceneInverseTimeStep = 50.f, m_walkMultiplier = 1.f / 1.3f,
                m_runMultiplier = 1.f / .8f;
    bool m_flying = false, m_alwaysRun = false, m_pidControllerActive = true, m_freemove = false,
         _zeroFlag = false, m_haveLastFallVel = false, m_iscolliding = false, m_iscollidingGround = false;
    int m_bodydisablecontrol = 0, Colliderfilter = 0, m_colliderGroundfilter = 0;
    int avatar_contact_filter = 0;
    // Unsupported hover/buoyancy configuration stays at original defaults.
    bool m_useHoverPID = false;
    float m_PIDHoverTau = 0, m_PIDHoverHeight = 0, m_buoyancy = 0,
          MinimumGroundFlightOffset = 3;
    HoverReference m_PIDHoverType = HoverReference::Ground;
    std::uint32_t width, height;
    int m_heightmapWidthSamples, m_heightmapHeightSamples;
    const std::vector<float>& heights = native_scene->terrain_heights;
    void check() const {
        if (std::this_thread::get_id() != owner)
            throw std::logic_error("ubODE world accessed from another thread");
    }
    void avatar() const {
        check();
        if (!Body)
            throw std::logic_error("ubODE avatar not added");
    }
    void UpdateAABB2D() {
    } // No other geometry in this explicitly single-avatar profile.
    void AddCollisionEvent(unsigned, const ContactPoint&) {
    } // Source has no subscribers by default.
    void filter_collision() {
        if (Colliderfilter < -1)
            Colliderfilter = -1;
        else {
            if (--Colliderfilter < 0)
                Colliderfilter = 0;
            if (Colliderfilter < 6)
                m_iscolliding = false;
            else {
                m_pidControllerActive = true;
                m_iscolliding = true;
                m_freemove = false;
            }
        }
    }
    float horizontal_radius_squared_inverse(Vec direction) const {
        const float z = m_NativeOrientation2D.Z, w = m_NativeOrientation2D.W;
        const float cosine = 1.f - 2.f * z * z, sine = 2.f * z * w;
        const float x = cosine * direction.X + sine * direction.Y;
        const float y = cosine * direction.Y - sine * direction.X;
        return x * x / half_x_squared + y * y / half_y_squared;
    }
    bool collide_avatar(Impl& other) {
        auto delta = _position - other._position;
        const float radius = capsule_radius + other.capsule_radius;
        if (std::abs(delta.X) > radius || std::abs(delta.Y) > radius)
            return false;
        const float vertical = std::abs(delta.Z), height = CapsuleSizeZ + other.CapsuleSizeZ;
        if (vertical >= height)
            return false;
        const float cylinder = height - radius;
        float depth;
        Vec position = _position, normal;
        int feet = 0;
        if (vertical < cylinder) {
            position.Z -= .5f * delta.Z;
            const float squared = delta.X * delta.X + delta.Y * delta.Y;
            if (squared < 1e-6f) {
                const float z = m_NativeOrientation2D.Z;
                normal = {2.f * z * z - 1.f, 2.f * z * m_NativeOrientation2D.W, 0};
                depth = radius + .05f;
            } else {
                const float distance = std::sqrt(squared);
                if (distance > radius)
                    return false;
                normal = {delta.X / distance, delta.Y / distance, 0};
                const float first_radius = 1.f / std::sqrt(horizontal_radius_squared_inverse(normal));
                const float overlap = first_radius +
                                      1.f / std::sqrt(other.horizontal_radius_squared_inverse(normal)) -
                                      distance;
                if (overlap < .0005f)
                    return false;
                const float offset = first_radius - .5f * overlap;
                depth = offset <= 0 ? first_radius : overlap;
                if (offset > 0)
                    position = position - normal * offset;
            }
        } else {
            feet = delta.Z > 0 ? 1 : -1;
            delta.Z -= feet * cylinder;
            const float squared = delta.LengthSquared();
            if (squared < 1e-6f) {
                normal = {0, 0, delta.Z > 0 ? 1.f : -1.f};
                depth = radius;
            } else {
                const float distance = std::sqrt(squared);
                depth = radius - distance;
                if (depth < .00005f)
                    return false;
                normal = delta * (1.f / distance);
                if (distance > 1.f / std::sqrt(horizontal_radius_squared_inverse(normal)) +
                                   1.f / std::sqrt(other.horizontal_radius_squared_inverse(normal)))
                    return false;
                position = position - normal * (capsule_radius - .5f * depth);
            }
        }
        if (feet) {
            auto& standing = feet > 0 ? *this : other;
            standing.CollideNormal = feet > 0 ? normal : -normal;
            standing.Colliderfilter = std::min(9, standing.Colliderfilter + 3);
        }
        NativeContact contact;
        contact.position = {position.X, position.Y, position.Z, 0};
        contact.normal = {normal.X, normal.Y, normal.Z, 0};
        contact.depth = depth;
        auto joint = api->JointCreateContact(world, native_scene->contacts, &contact);
        if (!joint)
            return false;
        api->JointAttach(joint, Body, other.Body);
        avatar_contact_filter = other.avatar_contact_filter = 2;
        m_pidControllerActive = other.m_pidControllerActive = false;
        return true;
    }
    void sample_indices(float x, float y, int& ix, int& iy, float& dx, float& dy) const {
        x += 1.f;
        y += 1.f;
        if (x < 0) {
            ix = 0;
            dx = 0;
        } else if (x < m_heightmapWidthSamples - 1) {
            ix = static_cast<int>(x);
            dx = x - ix;
        } else {
            ix = m_heightmapWidthSamples - 2;
            dx = 0;
        }
        if (y < 0) {
            iy = 0;
            dy = 0;
        } else if (y < m_heightmapHeightSamples - 1) {
            iy = static_cast<int>(y);
            dy = y - iy;
        } else {
            iy = m_heightmapHeightSamples - 2;
            dy = 0;
        }
    }
    float GetTerrainHeightAtXY(float x, float y) const {
        int ix, iy;
        float dx, dy;
        sample_indices(x, y, ix, iy, dx, dy);
        auto p = heights.data() + iy * m_heightmapWidthSamples + ix;
        float h0 = *p, h1, h2;
        if (dy > dx) {
            p += m_heightmapWidthSamples;
            h2 = *p;
            h1 = (h2 - h0) * dy;
            ++p;
            h2 = (*p - h2) * dx;
        } else {
            ++p;
            h2 = *p;
            h1 = (h2 - h0) * dx;
            p += m_heightmapWidthSamples;
            h2 = (*p - h2) * dy;
        }
        return h0 + h1 + h2;
    }
    Vec GetTerrainNormalAtXY(float x, float y) const {
        int ix, iy;
        float dx, dy;
        sample_indices(x, y, ix, iy, dx, dy);
        auto p = heights.data() + iy * m_heightmapWidthSamples + ix;
        float h0, h1, h2, rx, ry;
        if (dy > dx) {
            h1 = *p;
            p += m_heightmapWidthSamples;
            h0 = *p;
            h2 = *(p + 1);
            rx = h0 - h2;
            ry = h1 - h0;
        } else {
            h2 = *p;
            ++p;
            h0 = *p;
            h1 = *(p + m_heightmapWidthSamples);
            rx = h2 - h0;
            ry = h0 - h1;
        }
        h0 = rx * rx + ry * ry + 1.f;
        h0 = 1.f / std::sqrt(h0);
        return {rx * h0, ry * h0, h0};
    }
    Impl(std::shared_ptr<OdeScene> value, const scene::Terrain& terrain, UbodeWorldOptions options)
        : native_scene(std::move(value)), width(terrain.width()), height(terrain.height()),
          m_heightmapWidthSamples(0), m_heightmapHeightSamples(0) {
        native_scene->water_height = options.water_height;
        if (!std::isfinite(options.scene_frame_time) || options.scene_frame_time <= 0)
            throw std::invalid_argument("Invalid ubODE scene frame time");
        if (width < 2 || height < 2 || width > 8192 || height > 8192)
            throw std::invalid_argument("Unsupported ubODE terrain dimensions");
        m_heightmapWidthSamples = int(width) + 3;
        m_heightmapHeightSamples = int(height) + 3;
        WorldExtents = {float(width), float(height), 0};
        update_terrain(terrain);
        native_scene->initialize(options.scene_frame_time);
    }
    void update_terrain(const scene::Terrain& terrain) {
        check();
        if (terrain.width() != width || terrain.height() != height)
            throw std::invalid_argument("ubODE terrain dimensions cannot change");
        const auto source = terrain.serialized_floats();
        std::vector<float> samples(std::size_t(m_heightmapWidthSamples) * m_heightmapHeightSamples);
        int yy = 0;
        for (int y = 0; y < m_heightmapHeightSamples; y++) {
            if (y > 1 && y < int(height) + 1)
                yy += int(width);
            int xx = 0;
            for (int x = 0; x < m_heightmapWidthSamples; x++) {
                if (x > 1 && x < int(width) + 1)
                    xx++;
                float v = source[yy + xx];
                if (!std::isfinite(v))
                    throw std::invalid_argument("Nonfinite ubODE terrain sample");
                samples[std::size_t(y) * m_heightmapWidthSamples + x] = std::clamp(v, -100.f, 4000.f);
            }
        }
        native_scene->terrain_heights.swap(samples);
    }
    Impl(const Impl& source)
        : native_scene(source.native_scene), WorldExtents(source.WorldExtents), width(source.width),
          height(source.height), m_heightmapWidthSamples(source.m_heightmapWidthSamples),
          m_heightmapHeightSamples(source.m_heightmapHeightSamples) {}
    void destroy_avatar() {
        if (Amotor) {
            api->JointDestroy(Amotor);
            Amotor = nullptr;
        }
        if (Body) {
            api->BodyDestroy(Body);
            Body = nullptr;
        }
        if (collider) {
            api->GeomDestroy(collider);
            collider = nullptr;
        }
    }
    ~Impl() {
        if (!Body && !collider && !Amotor)
            return;
        std::lock_guard guard(api->mutex);
        try {
            api->allocate();
        } catch (...) {
            return;
        }
        destroy_avatar();
    }
    void add(Vec p, Vec size, bool flying) {
        check();
        if (Body)
            throw std::logic_error("ubODE profile supports exactly one avatar");
        if (!finite(size))
            throw std::invalid_argument("Nonfinite ubODE avatar size");
        Vec half{size.X > .01f ? .5f * size.X : .01f, size.Y > .01f ? .5f * size.Y : .01f,
                 size.Z > .01f ? .5f * size.Z : .01f};
        float radius = std::max(half.X, half.Y), length = half.Z - radius;
        if (length < 0 || radius > 1000 || half.Z > 1000)
            throw std::invalid_argument("Unsupported ubODE capsule dimensions");
        capsule_radius = radius;
        half_x_squared = half.X * half.X;
        half_y_squared = half.Y * half.Y;
        avatar_contact_filter = 0;
        if (finite(p)) {
            if (p.Z > 65536.f || p.Z < -100.f)
                p.Z = GetTerrainHeightAtXY(127, 127) + 5;
            _position = p;
        } else
            _position = {WorldExtents.X * .5f, WorldExtents.Y * .5f, GetTerrainHeightAtXY(128, 128) + 10};
        _zeroPosition = _velocity = _acceleration = m_rotationalVelocity = m_targetVelocity = m_lastFallVel =
            CollideNormal = last_force = {};
        m_NativeOrientation2D = {};
        orientation = {0, 0, 0, 1};
        m_pidControllerActive = true;
        m_freemove = _zeroFlag = m_haveLastFallVel = m_iscolliding = m_iscollidingGround = m_alwaysRun =
            false;
        m_bodydisablecontrol = Colliderfilter = m_colliderGroundfilter = 0;
        m_flying = flying;
        CapsuleSizeZ = half.Z;
        collider = api->CreateCapsule(TopSpace, radius, 2.f * length);
        if (!collider)
            throw std::runtime_error("ubODE capsule creation failed");
        api->GeomSetCategoryBits(collider, 4);
        api->GeomSetCollideBits(collider, 4 | 2 | 0x2000);
        m_mass = 8.f * 3.f * half.X * half.Y * half.Z;
        api->MassSetBoxTotal(&ShellMass, m_mass, 2.f * half.X, 2.f * half.Y, 2.f * half.Z);
        m_massInvTimeScaled = m_mass * m_sceneInverseTimeStep;
        PID_D = .55f * m_massInvTimeScaled;
        PID_P = .225f * m_massInvTimeScaled;
        m_scenegravityForceZ = -9.8f * m_mass;
        Body = api->BodyCreate(world);
        if (!Body || !collider)
            throw std::runtime_error("ubODE avatar creation failed");
        api->BodySetAutoDisableFlag(Body, 1);
        api->BodySetPosition(Body, _position.X, _position.Y, _position.Z);
        api->BodySetMass(Body, &ShellMass);
        api->GeomSetBody(collider, Body);
        Amotor = api->JointCreateAMotor(world, nullptr);
        if (!Amotor)
            throw std::runtime_error("ubODE angular motor creation failed");
        api->JointAttach(Amotor, Body, nullptr);
        api->JointSetAMotorMode(Amotor, 0);
        api->JointSetAMotorNumAxes(Amotor, 3);
        for (int i = 0; i < 3; i++) {
            api->JointSetAMotorAxis(Amotor, i, 0, i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f);
            api->JointSetAMotorAngle(Amotor, i, 0);
            api->JointSetAMotorParam(Amotor, 8 + i * 256, 0);
            api->JointSetAMotorParam(Amotor, 7 + i * 256, .8f);
            api->JointSetAMotorParam(Amotor, i * 256, -1.e-5f);
            api->JointSetAMotorParam(Amotor, 1 + i * 256, 1.e-5f);
            api->JointSetAMotorParam(Amotor, 2 + i * 256, 0);
            api->JointSetAMotorParam(Amotor, 3 + i * 256, 5.e8f);
        }
    }
    void Move() {
        if (Body == nullptr)
            return;

        if (!api->BodyIsEnabled(Body)) {
            if (++m_bodydisablecontrol < 50)
                return;

            // clear residuals
            api->BodySetAngularVel(Body, 0.0f, 0.0f, 0.0f);
            api->BodySetLinearVel(Body, 0.0f, 0.0f, 0.0f);
            _zeroFlag = true;
            api->BodyEnable(Body);
        }

        m_bodydisablecontrol = 0;

        // the Amotor still lets avatar rotation to drift during colisions
        // so force it back to identity
        api->BodySetQuaternion(Body, &m_NativeOrientation2D);

        _position = api->BodyGetPositionOMV(Body);
        // check outbounds forcing to be in world
        bool fixbody = false;
        if ((std::isnan(_position.X) || std::isinf(_position.X))) {
            fixbody = true;
            _position.X = 128.0f;
        } else if (_position.X < 0.0f) {
            fixbody = true;
            _position.X = 0.01f;
        } else if (_position.X > WorldExtents.X - 0.01f) {
            fixbody = true;
            _position.X = WorldExtents.X - 0.01f;
        }

        if ((std::isnan(_position.Y) || std::isinf(_position.Y))) {
            fixbody = true;
            _position.Y = 128.0f;
        } else if (_position.Y < 0.0f) {
            fixbody = true;
            _position.Y = 0.01f;
        } else if (_position.Y > WorldExtents.Y - 0.01f) {
            fixbody = true;
            _position.Y = WorldExtents.Y - 0.01f;
        }

        if ((std::isnan(_position.Z) || std::isinf(_position.Z))) {
            fixbody = true;
            _position.Z = 128.0f;
        }

        if (fixbody) {
            m_freemove = false;
            api->BodySetPosition(Body, _position.X, _position.Y, _position.Z);
        }

        if (!m_pidControllerActive)
            _zeroPosition = _position;

        //Update AABB
        UpdateAABB2D();

        float aabbminz = _position.Z - CapsuleSizeZ;
        //float aabbmaxz = _position.Z + CapsuleSizeZ;

        bool tviszero = m_targetVelocity.IsZero();

        Vec ctv;
        if (tviszero)
            ctv = Vec{};
        else {
            if (m_alwaysRun) {
                ctv = Vec{m_targetVelocity.X * m_runMultiplier, m_targetVelocity.Y * m_runMultiplier,
                          m_targetVelocity.Z};
            } else {
                ctv = Vec{m_targetVelocity.X * m_walkMultiplier, m_targetVelocity.Y * m_walkMultiplier,
                          m_targetVelocity.Z};
            }
        }

        Vec vel = api->BodyGetLinearVelOMV(Body);

        float tmpX = _position.X;
        float tmpY = _position.Y;
        if (m_flying) {
            tmpX += vel.X * m_sceneTimeStep;
            tmpY += vel.Y * m_sceneTimeStep;
        }

        Vec vec = Vec{};
        float terrainheight = GetTerrainHeightAtXY(tmpX, tmpY);

        bool hoverPIDActive = false;
        if (m_useHoverPID && m_PIDHoverTau != 0 && m_PIDHoverHeight != 0) {
            float targetHoverHeight = m_PIDHoverHeight;
            switch (m_PIDHoverType) {
            case HoverReference::Ground:
                targetHoverHeight += terrainheight;
                break;

            case HoverReference::GroundAndWater:
                targetHoverHeight += std::max(terrainheight, native_scene->water_height);
                break;
            }

            hoverPIDActive = true;

            float fz = targetHoverHeight - _position.Z;

            //  if error is zero, use position control; otherwise, velocity control
            if (std::abs(fz) < 0.01f) {
                ctv.Z *= 0.1f;
            } else {
                _zeroFlag = false;
                fz /= m_PIDHoverTau;

                float tmp = std::abs(fz);
                if (tmp > 50.0f)
                    fz = 50.0f * sign(fz);
                else if (tmp < 0.1f)
                    fz = 0.1f * sign(fz);

                ctv.Z = fz + 0.1f * ctv.Z;
            }
        }

        if (aabbminz < terrainheight) {
            if (ctv.Z < 0.0f)
                ctv.Z = 0.0f;

            if (!m_haveLastFallVel) {
                m_lastFallVel = vel;
                m_haveLastFallVel = true;
            }

            float pidp50 = PID_P * 50;
            float depth = terrainheight - aabbminz;
            vec.Z = depth * pidp50;

            Vec n = GetTerrainNormalAtXY(tmpX, tmpY);
            if (!m_flying && !hoverPIDActive) {
                vec.Z -= vel.Z * PID_D;

                if (n.Z < 0.4f) {
                    vec.X = depth * pidp50 - vel.X * PID_D;
                    vec.X *= n.X;
                    vec.Y = depth * pidp50 - vel.Y * PID_D;
                    vec.Y *= n.Y;
                    vec.Z *= n.Z;
                    if (n.Z < 0.1f) {
                        // cancel the slope pose
                        n.X = 0.0f;
                        n.Y = 0.0f;
                        n.Z = 1.0f;
                    }
                }
            }

            if (depth < 0.2f) {
                m_colliderGroundfilter++;
                if (m_colliderGroundfilter > 2) {
                    m_iscolliding = true;
                    Colliderfilter = 2;

                    if (m_colliderGroundfilter > 10) {
                        m_colliderGroundfilter = 10;
                        m_freemove = false;
                    }

                    CollideNormal = n;

                    m_iscollidingGround = true;

                    ContactPoint contact{.PenetrationDepth = depth,
                                         .Position = Vec{_position.X, _position.Y, terrainheight},
                                         .SurfaceNormal = -n,
                                         .RelativeSpeed = Vec::Dot(m_lastFallVel, n),
                                         .CharacterFeet = true};
                    AddCollisionEvent(0, contact);
                    m_lastFallVel = Vec{};
                }
            }

            else {
                m_colliderGroundfilter -= 5;
                if (m_colliderGroundfilter <= 0) {
                    m_colliderGroundfilter = 0;
                    m_iscollidingGround = false;
                }
            }
        } else {
            m_haveLastFallVel = false;
            m_colliderGroundfilter -= 5;
            if (m_colliderGroundfilter <= 0) {
                m_colliderGroundfilter = 0;
                m_iscollidingGround = false;
            }
        }

        //******************************************
        if (!m_iscolliding)
            CollideNormal.Z = 0;

        tviszero = ctv.IsZero();
        if (!tviszero) {
            m_freemove = false;

            // movement relative to surface if moving on it
            // dont disturbe vertical movement, ie jumps
            if (m_iscolliding && !m_flying && ctv.Z == 0.0f && CollideNormal.Z > 0.2f &&
                CollideNormal.Z < 0.94f) {
                float p = ctv.X * CollideNormal.X + ctv.Y * CollideNormal.Y;
                ctv.X *= std::sqrt(1 - CollideNormal.X * CollideNormal.X);
                ctv.Y *= std::sqrt(1 - CollideNormal.Y * CollideNormal.Y);
                ctv.Z -= p;
                if (ctv.Z < 0.0f)
                    ctv.Z *= 2.0f;
            }
        }

        float breakfactor;
        if (!m_freemove) {
            //  if velocity is zero, use position control; otherwise, velocity control
            if (tviszero) {
                if (m_iscolliding || m_flying) {
                    //  keep track of where we stopped.  No more slippin' & slidin'
                    if (!_zeroFlag) {
                        _zeroFlag = true;
                        _zeroPosition = _position;
                        if (!m_pidControllerActive) {
                            float pidd0833 = PID_D * 0.833f;
                            vec.X -= vel.X * pidd0833;
                            vec.Y -= vel.Y * pidd0833;
                        }
                    }
                    if (m_pidControllerActive) {
                        // We only want to deactivate the PID Controller if we think we want to have our surrogate
                        // react to the physics scene by moving it's position.
                        // Avatar to Avatar collisions
                        // Prim to avatar collisions
                        float pidd2 = PID_D * 2.0f;
                        float pidp5 = PID_P * 5;

                        vec.X = -vel.X * pidd2 + (_zeroPosition.X - _position.X) * pidp5;
                        vec.Y = -vel.Y * pidd2 + (_zeroPosition.Y - _position.Y) * pidp5;
                        if (vel.Z > 0)
                            vec.Z += -vel.Z * PID_D + (_zeroPosition.Z - _position.Z) * PID_P;
                        else
                            vec.Z += (-vel.Z * PID_D + (_zeroPosition.Z - _position.Z) * PID_P) * 0.2f;
                    }
                } else {
                    _zeroFlag = false;
                    float pidd0833 = PID_D * 0.833f;
                    vec.X += (ctv.X - vel.X) * pidd0833;
                    vec.Y += (ctv.Y - vel.Y) * pidd0833;
                    // hack for  breaking on fall
                    if (ctv.Z == -9999.0f)
                        vec.Z += -vel.Z * PID_D - m_scenegravityForceZ;
                }
            } else {
                m_pidControllerActive = true;
                _zeroFlag = false;

                if (m_iscolliding) {
                    if (!m_flying) {
                        // we are on a surface
                        if (ctv.Z > 0.0f) {
                            // moving up or JUMPING
                            vec.Z += (ctv.Z - vel.Z) * PID_D * 2.0f;
                            vec.X += (ctv.X - vel.X) * PID_D;
                            vec.Y += (ctv.Y - vel.Y) * PID_D;
                        } else {
                            // we are moving down on a surface
                            if (ctv.Z == 0) {
                                if (vel.Z > 0)
                                    vec.Z -= vel.Z * PID_D * 2.0f;
                                vec.X += (ctv.X - vel.X) * PID_D;
                                vec.Y += (ctv.Y - vel.Y) * PID_D;
                            }
                            // intencionally going down
                            else {
                                if (ctv.Z < vel.Z)
                                    vec.Z += (ctv.Z - vel.Z) * PID_D;
                                else {
                                }

                                if (std::abs(ctv.X) > std::abs(vel.X))
                                    vec.X += (ctv.X - vel.X) * PID_D;
                                if (std::abs(ctv.Y) > std::abs(vel.Y))
                                    vec.Y += (ctv.Y - vel.Y) * PID_D;
                            }
                        }

                        // We're standing on something
                    } else {
                        // We're flying and colliding with something
                        float pidd00625 = PID_D * 0.0625f;
                        vec.X += (ctv.X - vel.X) * pidd00625;
                        vec.Y += (ctv.Y - vel.Y) * pidd00625;
                        vec.Z += (ctv.Z - vel.Z) * pidd00625;
                    }
                } else // ie not colliding
                {
                    if (m_flying || hoverPIDActive) //(!m_iscolliding && flying)
                    {
                        // we're in mid air suspended
                        vec.X += (ctv.X - vel.X) * PID_D;
                        vec.Y += (ctv.Y - vel.Y) * PID_D;
                        vec.Z += (ctv.Z - vel.Z) * PID_D;
                    }

                    else {
                        // we're not colliding and we're not flying so that means we're falling!
                        // m_iscolliding includes collisions with the ground.

                        // d.Vec pos = d.BodyGetPosition(Body);
                        float pidd0833 = PID_D * 0.833f;
                        vec.X += (ctv.X - vel.X) * pidd0833;
                        vec.Y += (ctv.Y - vel.Y) * pidd0833;
                        // hack for  breaking on fall
                        if (ctv.Z == -9999.0f)
                            vec.Z += -vel.Z * PID_D - m_scenegravityForceZ;
                    }
                }
            }
            float velLengthSquared = vel.LengthSquared();
            if (velLengthSquared > 2500.0f) // 50m/s apply breaks
            {
                breakfactor = 0.16f * m_mass;
                vec.X -= breakfactor * vel.X;
                vec.Y -= breakfactor * vel.Y;
                vec.Z -= breakfactor * vel.Z;
            }
        } else {
            breakfactor = m_mass;
            vec.X -= breakfactor * vel.X;
            vec.Y -= breakfactor * vel.Y;
            if (m_flying)
                vec.Z -= 0.5f * breakfactor * vel.Z;
            else
                vec.Z -= .16f * m_mass * vel.Z;
        }

        if (m_flying || hoverPIDActive) {
            vec.Z -= m_scenegravityForceZ;

            if (!hoverPIDActive) {
                //Added for auto fly height. Kitto Flora
                float target_altitude = terrainheight + MinimumGroundFlightOffset;

                if (_position.Z < target_altitude) {
                    vec.Z += (target_altitude - _position.Z) * PID_P * 5.0f;
                }
                // end add Kitto Flora
            }
        } else if (m_buoyancy != 0.0) {
            vec.Z -= m_scenegravityForceZ * m_buoyancy;
        }

        if ((vec.Z != 0 || vec.X != 0 || vec.Y != 0)) {
            last_force = vec;
            api->BodyAddForce(Body, vec.X, vec.Y, vec.Z);
        }

        if (_zeroFlag) {
            _velocity = Vec{};
            _acceleration = Vec{};
            m_rotationalVelocity = Vec{};
        } else {
            Vec a = _velocity; // previous velocity
            SetSmooth(_velocity, vel);
            a = (_velocity - a) * m_sceneInverseTimeStep;
            SetSmooth(_acceleration, a);

            m_rotationalVelocity = api->BodyGetAngularVelOMVforAvatar(Body);
        }
    }
};
UbodeWorld::UbodeWorld(const std::filesystem::path& library, const scene::Terrain& terrain,
                       UbodeWorldOptions options)
    : impl_(std::make_unique<Impl>(std::make_shared<OdeScene>(load(library)), terrain, options)) {
}
UbodeWorld::UbodeWorld(UbodeWorld& scene) {
    scene.impl_->check();
    impl_ = std::make_unique<Impl>(*scene.impl_);
}
UbodeWorld::~UbodeWorld() = default;
void UbodeWorld::add_avatar(framework::Vector3 p, framework::Vector3 size, bool flying) {
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    impl_->add(from(p), from(size), flying);
}
void UbodeWorld::resize_avatar(framework::Vector3 size) {
    auto& avatar = *impl_;
    std::lock_guard guard(avatar.api->mutex);
    avatar.api->allocate();
    avatar.check();
    const auto half = from(size) * .5f;
    const auto radius = std::max(half.X, half.Y);
    if (!avatar.Body || !finite(half) || half.X <= 0 || half.Y <= 0 || half.Z < radius || half.Z > 1000.f)
        return;
    const auto old_height = avatar.CapsuleSizeZ;
    if (half.Z != old_height) {
        avatar.CapsuleSizeZ = half.Z;
        avatar.capsule_radius = radius;
        avatar.half_x_squared = half.X * half.X;
        avatar.half_y_squared = half.Y * half.Y;
        avatar.api->GeomCapsuleSetParams(avatar.collider, radius, 2.f * (half.Z - radius));
        avatar.m_mass = 24.f * half.X * half.Y * half.Z;
        avatar.m_massInvTimeScaled = avatar.m_mass * avatar.m_sceneInverseTimeStep;
        avatar.PID_D = .55f * avatar.m_massInvTimeScaled;
        avatar.PID_P = .225f * avatar.m_massInvTimeScaled;
        avatar.m_scenegravityForceZ = -9.8f * avatar.m_mass;
        avatar.api->MassSetBoxTotal(&avatar.ShellMass, avatar.m_mass, 2.f * half.X, 2.f * half.Y,
                                    2.f * half.Z);
        avatar.api->BodySetMass(avatar.Body, &avatar.ShellMass);
        avatar._position.Z += half.Z - old_height;
        avatar.api->BodySetPosition(avatar.Body, avatar._position.X, avatar._position.Y, avatar._position.Z);
        avatar.m_bodydisablecontrol = 0;
        avatar._zeroFlag = false;
        avatar._velocity = avatar.m_targetVelocity = {};
    }
    avatar.m_freemove = false;
    avatar.m_pidControllerActive = true;
}
void UbodeWorld::remove_avatar() {
    impl_->check();
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    impl_->destroy_avatar();
}
void UbodeWorld::set_target_velocity(framework::Vector3 v) {
    impl_->avatar();
    if (!finite(from(v)))
        throw std::invalid_argument("Nonfinite ubODE target velocity");
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    impl_->m_targetVelocity = from(v);
    impl_->api->BodyEnable(impl_->Body);
}
void UbodeWorld::set_flying(bool value) {
    impl_->avatar();
    impl_->m_flying = value;
}
void UbodeWorld::set_running(bool value) {
    impl_->avatar();
    impl_->m_alwaysRun = value;
}
void UbodeWorld::set_water_height(float height) {
    if (!std::isfinite(height))
        throw std::invalid_argument("Nonfinite ubODE water height");
    impl_->native_scene->water_height = height;
}
void UbodeWorld::set_hover(float height, float tau, HoverReference reference, bool active) {
    impl_->avatar();
    if (!std::isfinite(height) || !std::isfinite(tau))
        throw std::invalid_argument("Nonfinite ubODE hover settings");
    impl_->m_PIDHoverHeight = height;
    impl_->m_PIDHoverTau = tau;
    impl_->m_PIDHoverType = reference;
    impl_->m_useHoverPID = active && height != 0.f;
}
void UbodeWorld::set_orientation(framework::Quaternion input) {
    impl_->avatar();
    auto q = normalize(input);
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    auto& c = *impl_;
    if (c.orientation != q) {
        float squared = q.z * q.z + q.w * q.w;
        // SDK Vector2.Normalize uses a double literal threshold; a zero yaw quaternion
        // aborts native ODE. Reject that unsupported source input before mutating state.
        if (!(static_cast<double>(squared) > 1.e-6))
            throw std::invalid_argument("ubODE orientation has no encodable yaw component");
        float inverse = 1.f / std::sqrt(squared);
        float z = q.z * inverse, w = q.w * inverse;
        if (!std::isfinite(z) || !std::isfinite(w))
            throw std::invalid_argument("Nonfinite ubODE yaw orientation");
        c.orientation = q;
        c.m_NativeOrientation2D = {w, 0, 0, z};
        c.api->BodySetQuaternion(c.Body, &c.m_NativeOrientation2D);
    }
    c.api->BodyEnable(c.Body);
}
void UbodeWorld::avatar_jump(float impulse) {
    impl_->avatar();
    if (!std::isfinite(impulse))
        throw std::invalid_argument("Nonfinite ubODE jump impulse");
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    auto& c = *impl_;
    // OdeCharacter.AvatarJump/changeForce/setFreeMove, with no collision event
    // subscribers or other object contacts in this profile.
    c.m_pidControllerActive = true;
    c._zeroFlag = false;
    c.m_targetVelocity = {};
    c.m_freemove = true;
    c.Colliderfilter = std::numeric_limits<int>::min();
    c.m_colliderGroundfilter = -1;
    c.m_iscolliding = false;
    c.m_iscollidingGround = false;
    float force = impulse * c.m_massInvTimeScaled;
    if (force != 0)
        c.api->BodyAddForce(c.Body, 0, 0, force);
    c.api->BodyEnable(c.Body);
}
void UbodeWorld::step(std::size_t count) {
    step_with({}, count);
}
void UbodeWorld::update_terrain(const scene::Terrain& terrain) {
    impl_->update_terrain(terrain);
}
void UbodeWorld::step_with(std::span<UbodeWorld* const> peers, std::size_t count) {
    impl_->avatar();
    for (std::size_t i = 0; i < peers.size(); ++i) {
        const auto peer = peers[i];
        if (!peer || peer == this || peer->impl_->native_scene != impl_->native_scene ||
            std::find(peers.begin(), peers.begin() + i, peer) != peers.begin() + i)
            throw std::invalid_argument("Invalid shared ubODE avatar controller");
        peer->impl_->avatar();
    }
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    const auto move = [](Impl& avatar) {
        avatar.Move();
        const auto force = avatar.api->BodyGetForce(avatar.Body);
        avatar.last_force = {force[0], force[1], force[2]};
    };
    const auto controller = [&](std::size_t index) -> Impl& {
        return index == 0 ? *impl_ : *peers[index - 1]->impl_;
    };
    while (count--) {
        move(*impl_);
        for (auto peer : peers)
            move(*peer->impl_);
        constexpr std::size_t max_contact_joints = 6000;
        std::size_t contact_joints = 0;
        for (std::size_t i = 0; i <= peers.size(); ++i) {
            auto& avatar = controller(i);
            const bool skip_contacts = avatar.Colliderfilter < -1;
            avatar.filter_collision();
            if (skip_contacts)
                continue;
            if (avatar.avatar_contact_filter > 0) {
                --avatar.avatar_contact_filter;
                avatar.m_pidControllerActive = avatar.avatar_contact_filter == 0;
            }
            for (std::size_t j = i + 1; j <= peers.size() && contact_joints < max_contact_joints; ++j) {
                auto& other = controller(j);
                if (other.Colliderfilter >= -1 && avatar.collide_avatar(other))
                    ++contact_joints;
            }
        }
        impl_->api->WorldQuickStep(impl_->world, .020f);
        impl_->api->JointGroupEmpty(impl_->native_scene->contacts);
    }
}
UbodeSnapshot UbodeWorld::snapshot() const {
    impl_->avatar();
    std::lock_guard guard(impl_->api->mutex);
    impl_->api->allocate();
    auto& c = *impl_;
    return {to(c._position),
            to(c._velocity),
            to(c._acceleration),
            to(c.api->BodyGetPositionOMV(c.Body)),
            to(c.api->BodyGetLinearVelOMV(c.Body)),
            to(c.last_force),
            c.m_iscolliding || c.m_iscollidingGround,
            c.m_iscollidingGround,
            c.m_flying,
            c.m_alwaysRun,
            c.orientation,
            [&] {
                auto q = c.api->BodyGetQuaternion(c.Body);
                return framework::Quaternion{q[1], q[2], q[3], q[0]};
            }()};
}
float UbodeWorld::terrain_height(float x, float y) const {
    impl_->check();
    return impl_->GetTerrainHeightAtXY(x, y);
}
framework::Vector3 UbodeWorld::terrain_normal(float x, float y) const {
    impl_->check();
    return to(impl_->GetTerrainNormalAtXY(x, y));
}
std::string UbodeWorld::configuration() const {
    impl_->check();
    return impl_->api->config;
}
}
