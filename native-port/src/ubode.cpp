// Copyright (c) Contributors, http://opensimulator.org/
// BSD license: native-port/LICENSE.txt. ODE ABI adapted from source ODEApi.cs.
#include "opensim/physics/ubode.hpp"
#include "opensim/physics/mesh_asset.hpp"
#include "opensim/framework/sdk_quaternion.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <map>
#include <limits>
#include <mutex>
#include <numbers>
#include <set>
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
constexpr float world_gravity_z = -9.8f;

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
NativeQuaternion to_native(framework::Quaternion orientation) {
    return {orientation.w, orientation.x, orientation.y, orientation.z};
}
bool finite(framework::Quaternion rotation) {
    return std::isfinite(rotation.x) && std::isfinite(rotation.y) &&
           std::isfinite(rotation.z) && std::isfinite(rotation.w);
}
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
static_assert(sizeof(NativeContact) == 128 && offsetof(NativeContact, position) == 48 &&
              offsetof(NativeContact, geometry1) == 88);
struct Mass {
    float mass;
    float c[4];
    float inertia[12];
};
using NearCallback = void(ODE_CALL*)(void*, void*, void*);
using TriCallback = int(ODE_CALL*)(void*, void*, int);
using TriArrayCallback = void(ODE_CALL*)(void*, void*, const int*, int);
using TriRayCallback = int(ODE_CALL*)(void*, void*, int, float, float);
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
    X(void*, HashSpaceCreate, (void*), "dHashSpaceCreate")                                                   \
    X(void, SpaceCollide, (void*, void*, NearCallback), "dSpaceCollide")                                     \
    X(void, SpaceCollide2, (void*, void*, void*, NearCallback), "dSpaceCollide2")                             \
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
    X(void*, CreateBox, (void*, float, float, float), "dCreateBox")                                           \
    X(void*, CreateSphere, (void*, float), "dCreateSphere")                                                   \
    X(void*, TriMeshDataCreate, (), "dGeomTriMeshDataCreate")                                                 \
    X(void, TriMeshDataDestroy, (void*), "dGeomTriMeshDataDestroy")                                          \
    X(void, TriMeshDataBuild, (void*, const void*, int, int, const void*, int, int),                         \
      "dGeomTriMeshDataBuildSingle")                                                                          \
    X(void, TriMeshDataPreprocess, (void*), "dGeomTriMeshDataPreprocess")                                    \
    X(void*, CreateTriMesh, (void*, void*, TriCallback, TriArrayCallback, TriRayCallback),                  \
      "dCreateTriMesh")                                                                                     \
    X(int, GeomGetClass, (void*), "dGeomGetClass")                                                            \
    X(float, GeomSphereGetRadius, (void*), "dGeomSphereGetRadius")                                            \
    X(void*, CreateOSTerrain, (void*, void*, int), "dCreateOSTerrain")                                        \
    X(void*, TerrainDataCreate, (), "dGeomOSTerrainDataCreate")                                              \
    X(void, TerrainDataDestroy, (void*), "dGeomOSTerrainDataDestroy")                                        \
    X(void, TerrainDataBuild, (void*, const float*, int, float, int, int, float, int),                       \
      "dGeomOSTerrainDataBuild")                                                                             \
    X(void, TerrainDataSetBounds, (void*, float, float), "dGeomOSTerrainDataSetBounds")                       \
    X(void, GeomSetPosition, (void*, float, float, float), "dGeomSetPosition")                                \
    X(void, GeomSetQuaternion, (void*, const NativeQuaternion*), "dGeomSetQuaternion")                        \
    X(void, GeomSetOffsetPosition, (void*, float, float, float), "dGeomSetOffsetPosition")                     \
    X(void, GeomSetOffsetQuaternion, (void*, const NativeQuaternion*), "dGeomSetOffsetQuaternion")           \
    X(void, GeomGetAABB, (void*, float*), "dGeomGetAABB")                                                    \
    X(int, Collide, (void*, void*, int, void*, int), "dCollide")                                               \
    X(void, GeomCapsuleSetParams, (void*, float, float), "dGeomCapsuleSetParams")                            \
    X(void, GeomDestroy, (void*), "dGeomDestroy")                                                            \
    X(void, GeomSetCategoryBits, (void*, unsigned), "dGeomSetCategoryBits")                                  \
    X(void, GeomSetCollideBits, (void*, unsigned), "dGeomSetCollideBits")                                    \
    X(void, MassSetBoxTotal, (Mass*, float, float, float, float), "dMassSetBoxTotal")                        \
    X(void, MassAdd, (Mass*, const Mass*), "dMassAdd")                                                         \
    X(void, MassRotate, (Mass*, const float*), "dMassRotate")                                                 \
    X(void, MassTranslate, (Mass*, float, float, float), "dMassTranslate")                                     \
    X(void*, BodyCreate, (void*), "dBodyCreate")                                                             \
    X(void, BodyDestroy, (void*), "dBodyDestroy")                                                            \
    X(void, BodySetAutoDisableFlag, (void*, int), "dBodySetAutoDisableFlag")                                 \
    X(void, BodySetPosition, (void*, float, float, float), "dBodySetPosition")                               \
    X(void, BodySetMass, (void*, const Mass*), "dBodySetMass")                                               \
    X(void, BodySetGravityMode, (void*, int), "dBodySetGravityMode")                                         \
    X(void, GeomSetBody, (void*, void*), "dGeomSetBody")                                                     \
    X(void, GeomSetData, (void*, void*), "dGeomSetData")                                                       \
    X(void*, GeomGetData, (void*), "dGeomGetData")                                                            \
    X(void*, GeomGetBody, (void*), "dGeomGetBody")                                                            \
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
    X(void, BodyAddTorque, (void*, float, float, float), "dBodyAddTorque")                                   \
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
    struct Collider {
        void* geom = nullptr;
        void* mesh_data = nullptr;
        std::vector<float> vertices;
        std::vector<std::int32_t> indices;
        framework::Vector3 inertia_size;
        framework::Vector3 centroid;
        framework::PrimContactMaterial contact_material;
    };
    struct Box {
        void* body = nullptr;
        std::vector<Collider> colliders;
        framework::Vector3 center_of_mass;
        Vec constant_force;
        Vec constant_torque;
    };

    float water_height = 20.f;
    Lease api;
    void* world = nullptr;
    void* space = nullptr;
    void* contacts = nullptr;
    std::vector<float> terrain_heights;
    void* terrain_data = nullptr;
    void* terrain_geom = nullptr;
    void* object_space = nullptr;
    std::map<framework::Uuid, Box> boxes;
    std::size_t dynamic_box_count = 0;
    static constexpr unsigned dynamic_box_category = 1u, static_box_category = 2u;
    static constexpr unsigned avatar_category = 4u;
    static constexpr int max_pair_contacts = 8;

    explicit OdeScene(std::shared_ptr<Api> value) : api(std::move(value)) {
    }
    void refresh_collision_terrain_bounds(int width, int height) {
        const auto [lowest, highest] = std::minmax_element(terrain_heights.begin(), terrain_heights.end());
        api->TerrainDataSetBounds(terrain_data, *lowest, *highest);
        if (terrain_geom)
            api->GeomSetPosition(terrain_geom, static_cast<float>(width - 3) * .5f,
                                 static_cast<float>(height - 3) * .5f, 0.f);
    }
    void ensure_collision_terrain(int width, int height) {
        if (terrain_geom)
            return;
        if (!terrain_data)
            terrain_data = api->TerrainDataCreate();
        if (!terrain_data)
            throw std::runtime_error("ubODE terrain data creation failed");
        api->TerrainDataBuild(terrain_data, terrain_heights.data(), 0, 1.f, width, height, 1.f, 0);
        refresh_collision_terrain_bounds(width, height);
        terrain_geom = api->CreateOSTerrain(nullptr, terrain_data, 1);
        if (!terrain_geom)
            throw std::runtime_error("ubODE terrain geometry creation failed");
        api->GeomSetPosition(terrain_geom, static_cast<float>(width - 3) * .5f,
                             static_cast<float>(height - 3) * .5f, 0.f);
    }
    void ensure_object_space() {
        if (!object_space)
            object_space = api->HashSpaceCreate(nullptr);
        if (!object_space)
            throw std::runtime_error("ubODE object space creation failed");
    }
    void destroy_box(const Box& box) {
        for (const auto& collider : box.colliders) {
            if (collider.geom)
                api->GeomDestroy(collider.geom);
            if (collider.mesh_data)
                api->TriMeshDataDestroy(collider.mesh_data);
        }
        if (box.body)
            api->BodyDestroy(box.body);
    }
    static std::vector<float> profile_fractions(int count, const framework::LinearPrimPath& path) {
        const bool cut = path.profile_begin > 0.f || path.profile_end < 1.f;
        std::vector<float> fractions;
        fractions.reserve(count + 2);
        if (cut)
            fractions.push_back(path.profile_begin);
        for (int i = 0; i < count; ++i) {
            const float fraction = static_cast<float>(i) / static_cast<float>(count);
            if (!cut || (fraction > path.profile_begin && fraction < path.profile_end))
                fractions.push_back(fraction);
        }
        if (cut)
            fractions.push_back(path.profile_end);
        return fractions;
    }
    static std::array<float, 2> profile_point(float fraction, int count, double angle_start) {
        const auto profile_vertex = [&](int corner) {
            const double angle = angle_start + 2. * std::numbers::pi * (corner % count) / count;
            return std::array{static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle))};
        };
        const float position = fraction * static_cast<float>(count);
        const int corner = static_cast<int>(position);
        const float offset = position - static_cast<float>(corner);
        const auto first = profile_vertex(corner);
        const auto next = profile_vertex(corner + 1);
        return {std::lerp(first[0], next[0], offset), std::lerp(first[1], next[1], offset)};
    }
    static void stitch_hollow_caps(std::vector<std::int32_t>& indices, const std::vector<float>& outer,
                                   const std::vector<float>& inner, bool cut, int wall_vertices,
                                   int top_ring, int inner_top_ring) {
        const int ring_size = static_cast<int>(outer.size());
        const int inner_size = static_cast<int>(inner.size());
        const int edges = ring_size - (cut ? 1 : 0);
        const int inner_edges = inner_size - (cut ? 1 : 0);
        // Connect nearby profile angles so the cap triangles stay outside the opening.
        const auto fraction_at = [](const auto& fractions, int vertex) {
            return vertex < static_cast<int>(fractions.size()) ? fractions[vertex] : 1.f;
        };
        int outer_edge = 0;
        int inner_edge = 0;
        while (outer_edge < edges || inner_edge < inner_edges) {
            const int a = outer_edge % ring_size;
            const int b = inner_edge % inner_size;
            const float outer_span = std::abs(fraction_at(outer, outer_edge + 1) -
                                              fraction_at(inner, inner_edge));
            const float inner_span = std::abs(fraction_at(inner, inner_edge + 1) -
                                              fraction_at(outer, outer_edge));
            if (outer_edge < edges && (inner_edge == inner_edges || outer_span <= inner_span)) {
                const int next = ++outer_edge % ring_size;
                indices.insert(indices.end(), {a, wall_vertices + b, next,
                                               top_ring + a, top_ring + next, inner_top_ring + b});
            } else {
                const int next = ++inner_edge % inner_size;
                indices.insert(indices.end(), {a, wall_vertices + b, wall_vertices + next,
                                               top_ring + a, inner_top_ring + next, inner_top_ring + b});
            }
        }
    }
    void build_linear_mesh(Collider& collider, framework::Vector3 size, int sides, float hollow,
                           const framework::LinearPrimPath& path) {
        auto& vertices = collider.vertices;
        auto& indices = collider.indices;
        const bool has_hole = hollow > 0.f;
        const bool cut = path.profile_begin > 0.f || path.profile_end < 1.f;
        const double angle_start = sides == 4 ? std::numbers::pi * 1.25 : 0.;
        const auto outer = profile_fractions(sides, path);
        const int inner_sides = path.hollow_segments ? path.hollow_segments : sides;
        const auto inner = has_hole ? profile_fractions(inner_sides, path) : std::vector<float>{};

        const float twist = std::abs(path.twist_end - path.twist_begin);
        // Both Extrude and Path.Create add subdivisions for linear twist.
        const int steps = 1 + (twist > .01f ? 2 * static_cast<int>(twist * 3.66f) : 0);
        const int ring_size = static_cast<int>(outer.size());
        const int edges = cut ? ring_size - 1 : ring_size;
        const int inner_size = static_cast<int>(inner.size());
        const int inner_edges = cut ? inner_size - 1 : inner_size;
        const int wall_vertices = (steps + 1) * ring_size;
        const int top_ring = steps * ring_size;
        const int inner_top_ring = wall_vertices + steps * inner_size;
        vertices.reserve((wall_vertices + (has_hole ? (steps + 1) * inner_size : steps + 1)) * 3);
        const int wall_edges = edges + (has_hole ? inner_edges : 0);
        indices.reserve(6 * wall_edges * (steps + 1) + (cut ? 12 * steps : 0));
        const auto path_position = [&](int step) {
            return std::lerp(path.begin, path.end, static_cast<float>(step) / static_cast<float>(steps));
        };
        const auto scale_at = [](float taper, float position) {
            return taper >= 0.f ? 1.f - taper * position : 1.f + taper * (1.f - position);
        };
        const auto append_wall = [&](const std::vector<float>& fractions, int count, float radius) {
            std::vector<std::array<float, 2>> profile;
            profile.reserve(fractions.size());
            for (const float fraction : fractions)
                profile.push_back(profile_point(fraction, count, angle_start));
            for (int step = 0; step <= steps; ++step) {
                const float position = path_position(step);
                const float rotation = std::lerp(path.twist_begin, path.twist_end, position);
                const float cosine = std::cos(rotation);
                const float sine = std::sin(rotation);
                const float scale_x = radius * scale_at(path.taper_x, position);
                const float scale_y = radius * scale_at(path.taper_y, position);
                for (const auto& point : profile) {
                    const float x = point[0] * scale_x;
                    const float y = point[1] * scale_y;
                    vertices.push_back(size.x * (x * cosine - y * sine + path.shear_x * position));
                    vertices.push_back(size.y * (x * sine + y * cosine + path.shear_y * position));
                    vertices.push_back(size.z * (position - .5f));
                }
            }
        };
        const float radius = sides == 4 ? std::numbers::sqrt2_v<float> * .5f : .5f;
        append_wall(outer, sides, radius);
        if (has_hole) {
            float inner_radius = radius * hollow;
            if (sides == 3)
                inner_radius = inner_sides == 4 ? .5f * std::min(hollow, .7f) * .707f : inner_radius * .5f;
            else if (sides == 4 && inner_sides != 4)
                inner_radius *= .707f;
            else if (sides == 24 && inner_sides == 4)
                inner_radius *= 1.414f;
            append_wall(inner, inner_sides, inner_radius);
        } else {
            for (int step = 0; step <= steps; ++step) {
                const float position = path_position(step);
                vertices.insert(vertices.end(), {size.x * path.shear_x * position,
                                                 size.y * path.shear_y * position,
                                                 size.z * (position - .5f)});
            }
        }
        for (int layer = 0; layer < steps; ++layer) {
            const int lower = layer * ring_size;
            const int upper = lower + ring_size;
            for (int i = 0; i < edges; ++i) {
                const int next = (i + 1) % ring_size;
                indices.insert(indices.end(), {lower + i, lower + next, upper + i,
                                               lower + next, upper + next, upper + i});
            }
            if (has_hole) {
                const int inner_lower = wall_vertices + layer * inner_size;
                const int inner_upper = inner_lower + inner_size;
                for (int i = 0; i < inner_edges; ++i) {
                    const int next = (i + 1) % inner_size;
                    indices.insert(indices.end(), {inner_lower + i, inner_upper + i, inner_lower + next,
                                                   inner_lower + next, inner_upper + i, inner_upper + next});
                }
            }
            if (cut) {
                const int inner_start = wall_vertices + (has_hole ? layer * inner_size : layer);
                const int inner_start_top = inner_start + (has_hole ? inner_size : 1);
                const int inner_end = inner_start + (has_hole ? inner_edges : 0);
                const int inner_end_top = inner_start_top + (has_hole ? inner_edges : 0);
                indices.insert(indices.end(), {lower, upper, inner_start, upper, inner_start_top, inner_start,
                                               lower + edges, inner_end, upper + edges,
                                               upper + edges, inner_end, inner_end_top});
            }
        }
        if (has_hole) {
            stitch_hollow_caps(indices, outer, inner, cut, wall_vertices, top_ring, inner_top_ring);
            return;
        }
        const int bottom_center = wall_vertices;
        const int top_center = bottom_center + steps;
        for (int i = 0; i < edges; ++i) {
            const int next = (i + 1) % ring_size;
            const int top = i + top_ring;
            const int next_top = next + top_ring;
            indices.insert(indices.end(), {bottom_center, next, i, top_center, top, next_top});
        }
    }
    void build_ring_mesh(Collider& collider, framework::Vector3 size, int profile_sides, float hollow,
                         const framework::LinearPrimPath& path) {
        constexpr int path_sides = 24;
        const double start = profile_sides == 4 ? std::numbers::pi * .25 : std::numbers::pi;
        const double profile_radius = profile_sides == 4 ? std::numbers::sqrt2 * .5 : .5;
        const bool profile_cut = path.profile_begin > 0.f || path.profile_end < 1.f;
        const bool has_hole = hollow > .001f;
        const auto outer_fractions = profile_fractions(profile_sides, path);
        std::vector<std::array<float, 2>> profile;
        for (const float fraction : outer_fractions)
            profile.push_back(profile_point(fraction, profile_sides, start));
        const int outer_size = static_cast<int>(profile.size());
        const int inner_sides = path.hollow_segments ? path.hollow_segments : profile_sides;
        const auto inner_fractions = has_hole ? profile_fractions(inner_sides, path) : std::vector<float>{};
        float inner_scale = hollow;
        if (profile_sides == 3)
            inner_scale = inner_sides == 4 ? std::min(hollow, .7f) * .707f : hollow * .5f;
        else if (profile_sides == 4 && inner_sides != 4)
            inner_scale *= .707f;
        else if (profile_sides > 4 && inner_sides == 4)
            inner_scale = std::min(hollow, .7f) / .7f;
        for (const float fraction : inner_fractions) {
            const auto point = profile_point(fraction, inner_sides, start);
            profile.push_back({point[0] * inner_scale, point[1] * inner_scale});
        }
        if (profile_cut && !has_hole)
            profile.push_back({0.f, 0.f});
        const int ring_size = static_cast<int>(profile.size());
        const double path_radius = .5 * (1. - path.hole_y);
        constexpr double turn = 2. * std::numbers::pi;
        const double shear_phase = .9 * path.shear_y / turn;
        const double begin = path.begin * path.revolutions - shear_phase;
        const double end = path.end * path.revolutions - shear_phase;
        const double axial_shear = path.shear_x * (.25 + .5 * (.5 - path.hole_y));
        const double y_shear_scale = 1. + .25 * std::abs(path.shear_y);
        auto& vertices = collider.vertices;
        auto& indices = collider.indices;
        const bool capped = path.begin > 0.f || path.end < 1.f ||
                            path.taper_x != 0.f || path.taper_y != 0.f || path.twist_begin != path.twist_end ||
                            path.radius_offset != 0.f || path.skew != 0.f;
        const bool closed = !capped && std::abs(path.revolutions - std::round(path.revolutions)) < 1.e-6f;
        std::vector<double> fractions;
        fractions.reserve(static_cast<int>(path_sides * path.revolutions) + 2);
        fractions.push_back(begin);
        for (int step = static_cast<int>(begin * path_sides) + 1; ; ++step) {
            const double fraction = static_cast<double>(step) / path_sides;
            constexpr double endpoint_tolerance = 1.e-7;
            if (fraction >= end - endpoint_tolerance)
                break;
            if (fraction > begin + endpoint_tolerance)
                fractions.push_back(fraction);
        }
        if (!closed)
            fractions.push_back(end);
        const int rings = static_cast<int>(fractions.size());
        const int segments = closed ? rings : rings - 1;
        vertices.reserve(3 * rings * ring_size);
        const int cap_triangles = has_hole && !profile_cut ? ring_size : ring_size - 2;
        indices.reserve(6 * segments * ring_size + (capped ? 6 * cap_triangles : 0));
        const auto scale_at = [](float taper, double fraction) {
            if (taper > .01f)
                return 1. - taper * fraction;
            if (taper < -.01f)
                return 1. + taper * (1. - fraction);
            return 1.;
        };
        for (const double fraction : fractions) {
            const double path_fraction = fraction / path.revolutions;
            const double angle = turn * fraction;
            const double profile_angle_on_path = angle + path.shear_y;
            const double cosine = std::cos(profile_angle_on_path);
            const double sine = std::sin(profile_angle_on_path);
            const double twist = std::lerp(static_cast<double>(path.twist_begin),
                                          static_cast<double>(path.twist_end), path_fraction);
            const double twist_cosine = std::cos(twist);
            const double twist_sine = std::sin(twist);
            const double scale_x = profile_radius * path.hole_x * (1. - std::abs(path.skew)) *
                                   scale_at(path.taper_x, path_fraction);
            const double scale_y = profile_radius * path.hole_y * scale_at(path.taper_y, path_fraction);
            const double axial_offset = path.skew * ((fraction + shear_phase) / path.revolutions - .5) +
                                        axial_shear * std::sin(angle);
            double radius_scale = 1.;
            if (path.radius_offset > .001f)
                radius_scale -= path.radius_offset * path_fraction;
            else if (path.radius_offset < .001f)
                radius_scale += path.radius_offset * (1. - path_fraction);
            const double center_radius = path_radius * radius_scale;
            const double center_y = y_shear_scale * center_radius * std::cos(angle);
            for (const auto& point : profile) {
                const double x = point[0] * scale_x;
                const double y = point[1] * scale_y;
                const double radial_offset = x * twist_sine + y * twist_cosine;
                vertices.insert(vertices.end(), {static_cast<float>(size.x * (axial_offset + x * twist_cosine -
                                                                            y * twist_sine)),
                                                static_cast<float>(size.y * (center_y + radial_offset * cosine)),
                                                static_cast<float>(size.z * (center_radius + radial_offset) * sine)});
            }
        }
        for (int ring = 0; ring < segments; ++ring) {
            const int current_ring = ring * ring_size;
            const int next_ring = ((ring + 1) % rings) * ring_size;
            const auto append_edge = [&](int point, int next) {
                indices.insert(indices.end(), {current_ring + point, current_ring + next, next_ring + point,
                                               current_ring + next, next_ring + next, next_ring + point});
            };
            if (has_hole) {
                for (int point = 0; point + 1 < outer_size; ++point)
                    append_edge(point, point + 1);
                for (int point = outer_size; point + 1 < ring_size; ++point)
                    append_edge(point + 1, point);
                if (profile_cut) {
                    append_edge(outer_size - 1, ring_size - 1);
                    append_edge(outer_size, 0);
                } else {
                    append_edge(outer_size - 1, 0);
                    append_edge(outer_size, ring_size - 1);
                }
            } else {
                for (int point = 0; point < ring_size; ++point)
                    append_edge(point, (point + 1) % ring_size);
            }
        }
        if (capped) {
            const int last = (rings - 1) * ring_size;
            if (has_hole) {
                stitch_hollow_caps(indices, outer_fractions, inner_fractions, profile_cut,
                                   outer_size, last, last + outer_size);
                return;
            }
            const int anchor = profile_cut ? ring_size - 1 : 0;
            const int first = profile_cut ? 0 : 1;
            const int last_edge = profile_cut ? ring_size - 2 : ring_size - 1;
            for (int point = first; point < last_edge; ++point)
                indices.insert(indices.end(), {anchor, point + 1, point, last + anchor,
                                               last + point, last + point + 1});
        }
    }
    void build_ellipsoid_mesh(Collider& collider, framework::Vector3 size, int profile_sides) {
        // The half-circle profile is revolved about the prim's local X axis.
        constexpr int path_sides = 24;
        const int profile_steps = profile_sides / 2;
        auto& vertices = collider.vertices;
        auto& indices = collider.indices;
        const int ring_count = profile_steps - 1;
        vertices.reserve((ring_count * path_sides + 2) * 3);
        indices.reserve(6 * ring_count * path_sides);
        for (int ring = 1; ring < profile_steps; ++ring) {
            const double latitude = std::numbers::pi * ring / profile_steps;
            const float x = static_cast<float>(-std::cos(latitude)) * size.x * .5f;
            const double radius = std::sin(latitude) * .5;
            for (int i = 0; i < path_sides; ++i) {
                const double angle = 2. * std::numbers::pi * i / path_sides;
                vertices.insert(vertices.end(), {x, static_cast<float>(std::cos(angle) * radius * size.y),
                                                static_cast<float>(std::sin(angle) * radius * size.z)});
            }
        }
        vertices.insert(vertices.end(), {-size.x * .5f, 0.f, 0.f, size.x * .5f, 0.f, 0.f});
        const int negative_pole = ring_count * path_sides;
        const int positive_pole = negative_pole + 1;
        for (int i = 0; i < path_sides; ++i) {
            const int next = (i + 1) % path_sides;
            indices.insert(indices.end(), {negative_pole, next, i});
            for (int ring = 0; ring < ring_count - 1; ++ring) {
                const int base = ring * path_sides;
                const int upper = base + path_sides;
                indices.insert(indices.end(), {base + i, base + next, upper + i,
                                               base + next, upper + next, upper + i});
            }
            const int last = (ring_count - 1) * path_sides;
            indices.insert(indices.end(), {positive_pole, last + i, last + next});
        }
    }
    void create_triangle_mesh(Collider& collider) {
        const auto& vertices = collider.vertices;
        const auto& indices = collider.indices;
        collider.mesh_data = api->TriMeshDataCreate();
        if (!collider.mesh_data)
            return;
        // ubODE retains pointers to both arrays for the lifetime of the geometry.
        api->TriMeshDataBuild(collider.mesh_data, vertices.data(), 3 * sizeof(float),
                              static_cast<int>(vertices.size() / 3), indices.data(),
                              static_cast<int>(indices.size()), 3 * sizeof(std::int32_t));
        api->TriMeshDataPreprocess(collider.mesh_data);
        collider.geom = api->CreateTriMesh(object_space, collider.mesh_data, nullptr, nullptr, nullptr);
    }
    int collect_contacts(void* first, void* second, std::span<NativeContact> collision,
                         std::size_t budget) {
        if (!budget)
            return 0;
        const int limit = static_cast<int>(std::min(budget, collision.size()));
        return api->Collide(first, second, limit, &collision.front().position, sizeof(NativeContact));
    }
    void attach_contact(const NativeContact& contact, void* first_body, void* second_body,
                        std::size_t& budget) {
        if (auto joint = api->JointCreateContact(world, contacts, &contact)) {
            api->JointAttach(joint, first_body, second_body);
            --budget;
        }
    }
    framework::PrimContactMaterial contact_material(void* geom) const {
        constexpr framework::PrimContactMaterial terrain_material{.3f, .001f};
        if (geom == terrain_geom)
            return terrain_material;
        return static_cast<const Collider*>(api->GeomGetData(geom))->contact_material;
    }
    void collide(void* first_geom, void* second_geom, void* first_body, void* second_body,
                 std::size_t& budget) {
        NativeContact collision[max_pair_contacts];
        const int count = collect_contacts(first_geom, second_geom, collision, budget);
        if (count <= 0)
            return;
        const auto first_material = contact_material(first_geom);
        const auto second_material = contact_material(second_geom);
        const float friction = std::sqrt(first_material.friction * second_material.friction);
        const float restitution = first_material.restitution * second_material.restitution;
        for (int i = 0; i < count; ++i) {
            auto& contact = collision[i];
            contact.mu = friction;
            contact.bounce = restitution;
            attach_contact(contact, first_body, second_body, budget);
        }
    }
    void apply_prim_forces() {
        for (const auto& [id, box] : boxes) {
            const auto force = box.constant_force;
            if (!force.IsZero())
                api->BodyAddForce(box.body, force.X, force.Y, force.Z);
            const auto torque = box.constant_torque;
            if (!torque.IsZero())
                api->BodyAddTorque(box.body, torque.X, torque.Y, torque.Z);
        }
    }
    void collide_dynamic_boxes(std::size_t& budget) {
        if (!dynamic_box_count)
            return;
        for (const auto& [id, box] : boxes) {
            if (!box.body)
                continue;
            for (const auto& collider : box.colliders) {
                // The native terrain collider uses the box's cached bounds.
                float bounds[6];
                api->GeomGetAABB(collider.geom, bounds);
                collide(collider.geom, terrain_geom, box.body, nullptr, budget);
                if (!budget)
                    return;
            }
        }

        struct CollisionPass {
            OdeScene& scene;
            std::size_t& budget;

            static void ODE_CALL on_pair(void* context, void* first, void* second) {
                auto& pass = *static_cast<CollisionPass*>(context);
                auto& scene = pass.scene;
                const auto first_body = scene.api->GeomGetBody(first);
                const auto second_body = scene.api->GeomGetBody(second);
                if (first_body != second_body)
                    scene.collide(first, second, first_body, second_body, pass.budget);
            }
        };
        CollisionPass pass{*this, budget};
        api->SpaceCollide(object_space, &pass, CollisionPass::on_pair);
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
        api->WorldSetGravity(world, 0, 0, world_gravity_z);
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
        for (const auto& [id, box] : boxes)
            destroy_box(box);
        if (object_space)
            api->SpaceDestroy(object_space);
        if (terrain_geom)
            api->GeomDestroy(terrain_geom);
        if (terrain_data)
            api->TerrainDataDestroy(terrain_data);
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
    float feet_offset = 0.f;
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
    void set_free_move();
    void avatar() const {
        check();
        if (!Body)
            throw std::logic_error("ubODE avatar not added");
    }
    void AddCollisionEvent(unsigned, const ContactPoint&) {
    } // Source has no subscribers by default.
    void filter_collision() {
        if (Colliderfilter < -1) {
            Colliderfilter = -1;
            return;
        }

        Colliderfilter = std::max(0, Colliderfilter - 1);
        m_iscolliding = Colliderfilter >= 6;
        if (m_iscolliding) {
            m_pidControllerActive = true;
            m_freemove = false;
        }
    }
    void collide_with_boxes(std::size_t& budget) {
        if (!native_scene->object_space || native_scene->boxes.empty() || !budget)
            return;
        struct CollisionPass {
            Impl& avatar;
            std::size_t& budget;

            static void ODE_CALL on_pair(void* context, void* first, void* second) {
                auto& pass = *static_cast<CollisionPass*>(context);
                auto& avatar = pass.avatar;
                avatar.collide_box(first == avatar.collider ? second : first, pass.budget);
            }
        };
        CollisionPass pass{*this, budget};
        api->SpaceCollide2(collider, native_scene->object_space, &pass, CollisionPass::on_pair);
    }
    void collide_box(void* geom, std::size_t& budget) {
        auto& scene = *native_scene;
        NativeContact contacts[OdeScene::max_pair_contacts];
        const int count = scene.collect_contacts(collider, geom, contacts, budget);
        const auto box_body = api->GeomGetBody(geom);
        constexpr int sphere_class = 0;
        const bool small_dynamic_sphere = box_body && api->GeomGetClass(geom) == sphere_class &&
                                          api->GeomSphereGetRadius(geom) < .5f;
        for (int i = 0; i < count; ++i) {
            auto& contact = contacts[i];
            const float height = contact.position.Z - _position.Z;
            if (!small_dynamic_sphere && height <= feet_offset) {
                const bool on_top = contact.normal.Z > .35f;
                if ((on_top || !m_flying) && height < .3f - CapsuleSizeZ) {
                    CollideNormal = {contact.normal.X, contact.normal.Y, contact.normal.Z};
                    Colliderfilter = std::min(9, Colliderfilter + 3);
                    if (Colliderfilter >= 6) {
                        m_iscolliding = true;
                        m_pidControllerActive = true;
                        m_freemove = false;
                    }
                }
                if (!on_top && !m_flying) {
                    Vec offset{contact.position.X - _position.X, contact.position.Y - _position.Y,
                               height - feet_offset - .2f};
                    offset = offset * (1.f / std::sqrt(offset.LengthSquared()));
                    const float horizontal = std::abs(offset.X);
                    contact.depth = std::min(.25f, horizontal > 1e-6f
                        ? contact.depth / horizontal * contact.normal.X : contact.depth * 10.f);
                    contact.normal = {-offset.X, -offset.Y, -offset.Z, 0.f};
                }
            }
            scene.attach_contact(contact, Body, box_body, budget);
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
        std::lock_guard guard(api->mutex);
        if (native_scene->terrain_geom) {
            api->allocate();
            // The native terrain retains the address of the sample buffer.
            std::copy(samples.begin(), samples.end(), native_scene->terrain_heights.begin());
            native_scene->refresh_collision_terrain_bounds(m_heightmapWidthSamples, m_heightmapHeightSamples);
        } else {
            native_scene->terrain_heights.swap(samples);
        }
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
        feet_offset = -half.Z + std::min(.6f, half.Z * .45f);
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
        api->GeomSetCategoryBits(collider, OdeScene::avatar_category);
        api->GeomSetCollideBits(collider, OdeScene::avatar_category | OdeScene::dynamic_box_category |
                                        OdeScene::static_box_category | 0x2000u);
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
    impl_->m_buoyancy = 0.f;
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
        avatar.feet_offset = -half.Z + std::min(.6f, half.Z * .9f);
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
void UbodeWorld::set_buoyancy(float buoyancy) {
    impl_->avatar();
    if (std::isfinite(buoyancy))
        impl_->m_buoyancy = buoyancy;
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
void UbodeWorld::Impl::set_free_move() {
    m_pidControllerActive = true;
    _zeroFlag = false;
    m_targetVelocity = {};
    m_freemove = true;
    Colliderfilter = std::numeric_limits<int>::min();
    m_colliderGroundfilter = -1;
    m_iscolliding = false;
    m_iscollidingGround = false;
}
void UbodeWorld::set_momentum(framework::Vector3 momentum) {
    auto& avatar = *impl_;
    avatar.avatar();
    const auto velocity = from(momentum);
    if (!finite(velocity))
        return;
    std::lock_guard guard(avatar.api->mutex);
    avatar.api->allocate();
    avatar._velocity = velocity;
    avatar.set_free_move();
    avatar.api->BodySetLinearVel(avatar.Body, velocity.X, velocity.Y, velocity.Z);
    avatar.api->BodyEnable(avatar.Body);
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
    c.set_free_move();
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
void UbodeWorld::add_dynamic_box(framework::Uuid id, framework::Vector3 size, float mass,
                                 const UbodeBodyState& state) {
    const UbodePrimPart part{size, {}, {0, 0, 0, 1}, mass};
    add_dynamic_linkset(id, {&part, 1}, state);
}
void UbodeWorld::add_dynamic_linkset(framework::Uuid id, std::span<const UbodePrimPart> parts,
                                     const UbodeBodyState& state, UbodePrimForces forces) {
    add_prims(id, parts, state, true, forces);
}
void UbodeWorld::add_static_box(framework::Uuid id, framework::Vector3 size, framework::Vector3 position,
                                framework::Quaternion rotation) {
    add_static_prim(id, framework::PrimType::Box, size, position, rotation);
}
void UbodeWorld::add_static_prim(framework::Uuid id, framework::PrimType type, framework::Vector3 size,
                                 framework::Vector3 position, framework::Quaternion rotation,
                                 std::uint8_t mesh_segments, float hollow, framework::LinearPrimPath path,
                                 std::shared_ptr<const AssetPhysicsMesh> asset_mesh,
                                 framework::PrimContactMaterial contact_material) {
    const UbodePrimPart part{size, {}, {0, 0, 0, 1}, 0.f, type, mesh_segments, hollow, path,
                            std::move(asset_mesh), contact_material};
    add_prims(id, {&part, 1}, {position, rotation, {}, {}}, false);
}
void UbodeWorld::add_prims(framework::Uuid id, std::span<const UbodePrimPart> parts,
                           const UbodeBodyState& state, bool dynamic, UbodePrimForces forces) {
    impl_->check();
    if (id.is_zero() || parts.empty() || !finite(from(state.position)) || !finite(state.rotation) ||
        !finite(from(state.velocity)) || !finite(from(state.angular_velocity)) ||
        !(forces.gravity_modifier >= -1.f && forces.gravity_modifier <= 28.f))
        throw std::invalid_argument("Invalid ubODE box");
    for (const auto& part : parts) {
        const bool mesh = part.mesh_segments == 6 || part.mesh_segments == 24;
        const bool native_sphere = part.mesh_segments == 0 &&
                                   part.size.x == part.size.y && part.size.y == part.size.z;
        const auto& path = part.path;
        const bool swept = part.type == framework::PrimType::Torus || part.type == framework::PrimType::Tube ||
                           part.type == framework::PrimType::Ring;
        const bool swept_mesh = (part.type == framework::PrimType::Torus && part.mesh_segments == 24) ||
                                (part.type == framework::PrimType::Tube && part.mesh_segments == 4) ||
                                (part.type == framework::PrimType::Ring && part.mesh_segments == 3);
        const bool asset_mesh = part.type == framework::PrimType::Sculpt && part.asset_mesh && !part.mesh_segments;
        const bool supported_type = asset_mesh || swept_mesh || (part.type == framework::PrimType::Box &&
                                     (part.mesh_segments == 0 || part.mesh_segments == 4)) ||
                                    (part.type == framework::PrimType::Cylinder && mesh) ||
                                    (part.type == framework::PrimType::Prism && part.mesh_segments == 3) ||
                                    (part.type == framework::PrimType::Sphere && (mesh || native_sphere));
        const bool valid_hollow_profile = !path.hollow_segments ||
                                          (part.type != framework::PrimType::Sphere && part.hollow > 0.f &&
                                           (path.hollow_segments == 3 || path.hollow_segments == 4 ||
                                            path.hollow_segments == 24));
        const float twist_limit = swept ? 8.1f : 4.1f;
        const bool valid_path = path.begin >= 0.f && path.begin < path.end && path.end <= 1.f &&
                                path.profile_begin >= 0.f && path.profile_begin < path.profile_end &&
                                path.profile_end <= 1.f &&
                                std::abs(path.taper_x) <= 1.f && std::abs(path.taper_y) <= 1.f &&
                                std::isfinite(path.shear_x) && std::isfinite(path.shear_y) &&
                                std::abs(path.twist_begin) <= twist_limit && std::abs(path.twist_end) <= twist_limit;
        if (!finite(from(part.size)) || part.size.x <= 0 || part.size.y <= 0 || part.size.z <= 0 ||
            !supported_type || !finite(from(part.offset)) || !finite(part.rotation) ||
            !valid_path || !valid_hollow_profile ||
            (swept && (!(path.hole_x > 0.f && path.hole_x <= 1.f &&
                         path.hole_y > 0.f && path.hole_y <= .5f &&
                         std::abs(path.radius_offset) <= 1.28f && std::abs(path.skew) <= 1.28f &&
                         std::abs(path.shear_x) <= 1.28f && std::abs(path.shear_y) <= 1.28f &&
                         path.revolutions >= 1.f && path.revolutions <= 4.825f))) ||
            !std::isfinite(part.hollow) ||
            part.hollow < 0.f || part.hollow > .95f ||
            (part.hollow > 0.f && (!part.mesh_segments || part.type == framework::PrimType::Sphere)) ||
            (dynamic && (!std::isfinite(part.mass) || part.mass <= 0)))
            throw std::invalid_argument("Invalid ubODE prim part");
    }

    auto& scene = *impl_->native_scene;
    std::lock_guard guard(scene.api->mutex);
    scene.api->allocate();
    if (dynamic)
        scene.ensure_collision_terrain(impl_->m_heightmapWidthSamples, impl_->m_heightmapHeightSamples);
    scene.ensure_object_space();
    OdeScene::Box empty;
    empty.colliders.resize(parts.size());
    const auto [entry, inserted] = scene.boxes.try_emplace(id, std::move(empty));
    if (!inserted)
        throw std::invalid_argument("Duplicate ubODE box ID");

    auto& box = entry->second;
    const auto fail_creation = [&] {
        scene.destroy_box(box);
        scene.boxes.erase(entry);
        throw std::runtime_error("ubODE box creation failed");
    };
    const auto orientation = normalize(state.rotation);
    const auto native_rotation = to_native(orientation);
    const unsigned box_collisions = dynamic ? OdeScene::dynamic_box_category | OdeScene::static_box_category
                                            : OdeScene::dynamic_box_category;
    try {
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto& part = parts[i];
            const auto size = part.size;
            auto& collider = box.colliders[i];
            collider.inertia_size = size;
            if (part.type == framework::PrimType::Sculpt) {
                const auto& mesh = *part.asset_mesh;
                if (mesh.vertices.empty() || mesh.vertices.size() > std::numeric_limits<int>::max() ||
                    mesh.indices.empty() || mesh.indices.size() > std::numeric_limits<int>::max() ||
                    mesh.indices.size() % 3)
                    throw std::invalid_argument("Invalid asset collider geometry");
                collider.vertices.reserve(mesh.vertices.size() * 3);
                for (const auto vertex : mesh.vertices) {
                    const Vec scaled{vertex.x * size.x, vertex.y * size.y, vertex.z * size.z};
                    if (!finite(scaled))
                        throw std::invalid_argument("Nonfinite asset collider vertex");
                    collider.vertices.insert(collider.vertices.end(), {scaled.X, scaled.Y, scaled.Z});
                }
                collider.indices.reserve(mesh.indices.size());
                const auto point = [&](std::uint32_t index) {
                    if (index >= mesh.vertices.size())
                        throw std::invalid_argument("Asset collider index outside vertex list");
                    const auto start = static_cast<std::size_t>(index) * 3;
                    return std::array{collider.vertices[start], collider.vertices[start + 1],
                                      collider.vertices[start + 2]};
                };
                std::set<std::array<float, 3>> used_points;
                for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
                    const auto a = mesh.indices[triangle];
                    const auto b = mesh.indices[triangle + 1];
                    const auto c = mesh.indices[triangle + 2];
                    const auto first = point(a), second = point(b), third = point(c);
                    if (first == second || first == third || second == third)
                        continue;
                    collider.indices.insert(collider.indices.end(), {static_cast<std::int32_t>(a),
                        static_cast<std::int32_t>(b), static_cast<std::int32_t>(c)});
                    if (dynamic)
                        used_points.insert({first, second, third});
                }
                if (collider.indices.empty())
                    throw std::invalid_argument("Asset collider has no nondegenerate triangles");
                if (dynamic) {
                    auto minimum = *used_points.begin();
                    auto maximum = minimum;
                    std::array<double, 3> sum{};
                    for (const auto& vertex : used_points) {
                        for (std::size_t axis = 0; axis < 3; ++axis) {
                            minimum[axis] = std::min(minimum[axis], vertex[axis]);
                            maximum[axis] = std::max(maximum[axis], vertex[axis]);
                            sum[axis] += vertex[axis];
                        }
                    }
                    const auto extent = [&](std::size_t axis) {
                        return std::max(.001f, maximum[axis] - minimum[axis]);
                    };
                    collider.inertia_size = {extent(0), extent(1), extent(2)};
                    const double count = static_cast<double>(used_points.size());
                    collider.centroid = {static_cast<float>(sum[0] / count), static_cast<float>(sum[1] / count),
                                         static_cast<float>(sum[2] / count)};
                    if (!finite(from(collider.inertia_size)))
                        throw std::invalid_argument("Nonfinite asset collider bounds");
                }
                scene.create_triangle_mesh(collider);
            } else if (part.mesh_segments) {
                if (part.type == framework::PrimType::Sphere)
                    scene.build_ellipsoid_mesh(collider, size, part.mesh_segments);
                else if (part.type == framework::PrimType::Torus || part.type == framework::PrimType::Tube ||
                         part.type == framework::PrimType::Ring)
                    scene.build_ring_mesh(collider, size, part.mesh_segments, part.hollow, part.path);
                else
                    scene.build_linear_mesh(collider, size, part.mesh_segments, part.hollow, part.path);
                scene.create_triangle_mesh(collider);
            } else if (part.type == framework::PrimType::Sphere) {
                collider.geom = scene.api->CreateSphere(scene.object_space, size.x * .5f);
            } else {
                collider.geom = scene.api->CreateBox(scene.object_space, size.x, size.y, size.z);
            }
            if (!collider.geom)
                throw std::runtime_error("ubODE prim collider creation failed");
            const auto material = part.contact_material;
            collider.contact_material = {
                std::isfinite(material.friction) ? std::clamp(material.friction, 0.f, 255.f) : .6f,
                std::isfinite(material.restitution) ? std::clamp(material.restitution, 0.f, 1.f) : .5f
            };
            scene.api->GeomSetData(collider.geom, &collider);
            scene.api->GeomSetCategoryBits(collider.geom, dynamic ? OdeScene::dynamic_box_category
                                                                 : OdeScene::static_box_category);
            scene.api->GeomSetCollideBits(collider.geom, box_collisions | OdeScene::avatar_category);
        }
    } catch (...) {
        scene.destroy_box(box);
        scene.boxes.erase(entry);
        throw;
    }
    if (!dynamic) {
        const auto geom = box.colliders.front().geom;
        scene.api->GeomSetQuaternion(geom, &native_rotation);
        scene.api->GeomSetPosition(geom, state.position.x, state.position.y, state.position.z);
        return;
    }

    box.body = scene.api->BodyCreate(scene.world);
    if (!box.body)
        fail_creation();
    Mass inertia{};
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        const auto& collider = box.colliders[i];
        const auto size = collider.inertia_size;
        Mass part_inertia{};
        scene.api->MassSetBoxTotal(&part_inertia, part.mass, size.x, size.y, size.z);
        const auto rotation = normalize(part.rotation);
        const auto x = framework::sdk_rotate_vector({1, 0, 0}, rotation);
        const auto y = framework::sdk_rotate_vector({0, 1, 0}, rotation);
        const auto z = framework::sdk_rotate_vector({0, 0, 1}, rotation);
        const float matrix[] = {x.x, y.x, z.x, 0, x.y, y.y, z.y, 0, x.z, y.z, z.z, 0};
        scene.api->MassRotate(&part_inertia, matrix);
        const auto centroid = framework::sdk_rotate_vector(collider.centroid, rotation);
        const auto offset = from(part.offset) + from(centroid);
        scene.api->MassTranslate(&part_inertia, offset.X, offset.Y, offset.Z);
        scene.api->MassAdd(&inertia, &part_inertia);
    }
    box.center_of_mass = {inertia.c[0], inertia.c[1], inertia.c[2]};
    scene.api->MassTranslate(&inertia, -inertia.c[0], -inertia.c[1], -inertia.c[2]);
    scene.api->BodySetMass(box.body, &inertia);
    const float gravity_scale = (1.f - forces.buoyancy) * forces.gravity_modifier;
    const float gravity_force_z = inertia.mass * (world_gravity_z * gravity_scale);
    if (const auto force = from(forces.force); finite(force))
        box.constant_force = force;
    if (const auto torque = from(forces.torque); finite(torque))
        box.constant_torque = torque;
    const float combined_force_z = box.constant_force.Z + gravity_force_z;
    if (gravity_scale != 1.f && std::isfinite(combined_force_z)) {
        scene.api->BodySetGravityMode(box.body, 0);
        box.constant_force.Z = combined_force_z;
    }
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const auto& part = parts[i];
        const auto offset = from(part.offset) - from(box.center_of_mass);
        const auto rotation = to_native(normalize(part.rotation));
        const auto geom = box.colliders[i].geom;
        scene.api->GeomSetBody(geom, box.body);
        if (!offset.IsZero())
            scene.api->GeomSetOffsetPosition(geom, offset.X, offset.Y, offset.Z);
        if (rotation.W != 1.f || rotation.X != 0.f || rotation.Y != 0.f || rotation.Z != 0.f)
            scene.api->GeomSetOffsetQuaternion(geom, &rotation);
    }
    const auto center = from(state.position) + from(framework::sdk_rotate_vector(box.center_of_mass, orientation));
    scene.api->BodySetQuaternion(box.body, &native_rotation);
    scene.api->BodySetPosition(box.body, center.X, center.Y, center.Z);
    scene.api->BodySetLinearVel(box.body, state.velocity.x, state.velocity.y, state.velocity.z);
    scene.api->BodySetAngularVel(box.body, state.angular_velocity.x, state.angular_velocity.y,
                                state.angular_velocity.z);
    ++scene.dynamic_box_count;
}
void UbodeWorld::remove_box(framework::Uuid id) {
    impl_->check();
    auto& scene = *impl_->native_scene;
    std::lock_guard guard(scene.api->mutex);
    const auto entry = scene.boxes.find(id);
    if (entry == scene.boxes.end())
        return;
    scene.api->allocate();
    if (entry->second.body)
        --scene.dynamic_box_count;
    scene.destroy_box(entry->second);
    scene.boxes.erase(entry);
}
UbodeBodyState UbodeWorld::body_state(framework::Uuid id) const {
    impl_->check();
    auto& scene = *impl_->native_scene;
    std::lock_guard guard(scene.api->mutex);
    scene.api->allocate();
    const auto& box = scene.boxes.at(id);
    const auto body = box.body;
    if (!body)
        throw std::invalid_argument("Static ubODE box has no body");
    const auto rotation = scene.api->BodyGetQuaternion(body);
    const auto angular = scene.api->BodyGetAngularVel(body);
    const framework::Quaternion orientation{rotation[1], rotation[2], rotation[3], rotation[0]};
    const auto center_offset = from(framework::sdk_rotate_vector(box.center_of_mass, orientation));
    return {to(scene.api->BodyGetPositionOMV(body) - center_offset), orientation,
            to(scene.api->BodyGetLinearVelOMV(body)), {angular[0], angular[1], angular[2]}};
}
void UbodeWorld::step_with(std::span<UbodeWorld* const> peers, std::size_t count) {
    impl_->check();
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
    for (std::size_t frame = 0; frame < count; ++frame) {
        if (impl_->Body)
            move(*impl_);
        for (auto peer : peers)
            move(*peer->impl_);
        std::size_t contact_budget = 6000;
        for (std::size_t i = 0; i <= peers.size(); ++i) {
            auto& avatar = controller(i);
            if (!avatar.Body)
                continue;
            const bool skip_contacts = avatar.Colliderfilter < -1;
            avatar.filter_collision();
            if (skip_contacts)
                continue;
            if (avatar.avatar_contact_filter > 0) {
                --avatar.avatar_contact_filter;
                avatar.m_pidControllerActive = avatar.avatar_contact_filter == 0;
            }
            avatar.collide_with_boxes(contact_budget);
            for (std::size_t j = i + 1; j <= peers.size() && contact_budget; ++j) {
                auto& other = controller(j);
                if (other.Colliderfilter >= -1 && avatar.collide_avatar(other))
                    --contact_budget;
            }
        }
        impl_->native_scene->collide_dynamic_boxes(contact_budget);
        impl_->native_scene->apply_prim_forces();
        impl_->api->WorldQuickStep(impl_->world, time_step);
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
float UbodeWorld::avatar_mass() const {
    impl_->check();
    return impl_->Body ? impl_->m_mass : 0.f;
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
