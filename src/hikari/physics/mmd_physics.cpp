// MMD rigid bodies and 6DOF spring joints on Jolt Physics.
//
// One world per model. Bodies following their bone are kinematic and move with
// MoveKinematic, so they push simulated bodies with a real velocity; simulated bodies
// are dynamic. MMD's 16 collision groups become a GroupFilter. Joints are Jolt
// SixDOF constraints: MMD's limits map onto the axis limits, and its springs onto
// position motors that pull each free axis back to the rest frame.

#include "hikari/physics/mmd_physics.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/GroupFilter.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Constraints/SixDOFConstraint.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <unordered_set>
#include <vector>

using namespace JPH;

namespace {

constexpr ObjectLayer kLayerKinematic = 0;
constexpr ObjectLayer kLayerDynamic = 1;
constexpr ObjectLayer kLayerStatic = 2;
constexpr uint kNumLayers = 3;

class BroadPhaseLayers final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return kNumLayers; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer layer) const override {
        return BroadPhaseLayer(uint8(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "KINEMATIC"
                                     : (layer.GetValue() == 1 ? "DYNAMIC" : "STATIC");
    }
#endif
};

// Kinematic bodies never collide with each other; everything else may.
class ObjectVsBroadPhase final : public ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(ObjectLayer layer, BroadPhaseLayer bp) const override {
        return layer == kLayerDynamic || bp.GetValue() == kLayerDynamic;
    }
};

class ObjectPairs final : public ObjectLayerPairFilter {
public:
    bool ShouldCollide(ObjectLayer a, ObjectLayer b) const override {
        return a == kLayerDynamic || b == kLayerDynamic;
    }
};

// MMD collision groups: a pair collides when each body's mask contains the other's group.
// As in MikuMikuDance, two bodies joined by a joint never collide: neighbouring links
// of a hair strand or skirt overlap at rest, and pushing them apart tears the mesh.
// The group id of a Jolt CollisionGroup is the body index.
class MmdGroupFilter final : public GroupFilter {
public:
    std::vector<uint16_t> group_bit;
    std::vector<uint16_t> mask;
    std::unordered_set<uint64_t> linked;

    static uint64_t pair(uint32_t a, uint32_t b) {
        return a < b ? (uint64_t(a) << 32) | b : (uint64_t(b) << 32) | a;
    }

    bool CanCollide(const CollisionGroup& a, const CollisionGroup& b) const override {
        const uint32_t ia = a.GetGroupID();
        const uint32_t ib = b.GetGroupID();
        if (ia >= mask.size() || ib >= mask.size())
            return true;
        if ((mask[ia] & group_bit[ib]) == 0 || (mask[ib] & group_bit[ia]) == 0)
            return false;
        return linked.find(pair(ia, ib)) == linked.end();
    }
};

// Jolt's factory, type registry, and thread pool are process-wide; worlds may come and go on any thread.
std::mutex g_jolt_lock;
int g_jolt_users = 0;
JobSystemThreadPool* g_job_system = nullptr;

void jolt_acquire() {
    std::lock_guard<std::mutex> guard(g_jolt_lock);
    if (g_jolt_users++ == 0) {
        RegisterDefaultAllocator();
        Factory::sInstance = new Factory();
        RegisterTypes();
        g_job_system = new JobSystemThreadPool(cMaxPhysicsJobs, cMaxPhysicsBarriers);
    }
}

void jolt_release() {
    std::lock_guard<std::mutex> guard(g_jolt_lock);
    if (--g_jolt_users == 0) {
        delete g_job_system;
        g_job_system = nullptr;
        UnregisterTypes();
        delete Factory::sInstance;
        Factory::sInstance = nullptr;
    }
}

bool finite(const float* v, int n) {
    for (int i = 0; i < n; ++i)
        if (!std::isfinite(v[i]))
            return false;
    return true;
}

Quat load_quat(const float* q) {
    Quat r(q[0], q[1], q[2], q[3]);
    const float len = r.Length();
    return len > 1.0e-6f ? r / len : Quat::sIdentity();
}

} // namespace

struct MmdWorld {
    BroadPhaseLayers bp_layers;
    ObjectVsBroadPhase object_vs_bp;
    ObjectPairs object_pairs;
    TempAllocatorImpl temp{8 * 1024 * 1024};
    PhysicsSystem system;
    Ref<MmdGroupFilter> groups = new MmdGroupFilter();
    std::vector<BodyID> bodies;
    std::vector<bool> kinematic;
    BodyID ground;
    bool ground_enabled = false;
    int32_t joints = 0;
    float spring_damping_ratio = 0.0f;
};

namespace {

void create_ground(MmdWorld* world) {
    if (!world || !world->ground.IsInvalid()) {
        return;
    }
    // The top of the box sits exactly at Y = 0. Half extents keep it far larger
    // than any MMD scene while staying well inside float precision.
    Ref<Shape> shape = new BoxShape(Vec3(5000.0f, 0.5f, 5000.0f));
    BodyCreationSettings settings(shape, RVec3(0.0f, -0.5f, 0.0f), Quat::sIdentity(),
                                  EMotionType::Static, kLayerStatic);
    settings.mAllowSleeping = true;
    settings.mFriction = 0.8f;
    settings.mRestitution = 0.0f;
    // No group filter: MMD's per-body collision masks cannot disable the floor.
    settings.mCollisionGroup = CollisionGroup();
    // Keep the rest-overlap pass from treating the floor as a PMX body.
    settings.mUserData = 0xFFFFFFFFu;
    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    world->ground = bodies.CreateAndAddBody(settings, EActivation::DontActivate);
    world->ground_enabled = !world->ground.IsInvalid();
}

void destroy_ground(MmdWorld* world) {
    if (!world || world->ground.IsInvalid()) {
        return;
    }
    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    bodies.RemoveBody(world->ground);
    bodies.DestroyBody(world->ground);
    world->ground = BodyID();
    world->ground_enabled = false;
}

} // namespace

extern "C" {

MmdWorld* mmd_world_create(const MmdWorldSettings* settings, uint32_t max_bodies) {
    jolt_acquire();
    auto* world = new MmdWorld();
    const uint32_t capacity = std::max<uint32_t>(max_bodies, 16);
    world->system.Init(capacity, 0, capacity * 8, capacity * 8, world->bp_layers, world->object_vs_bp,
                       world->object_pairs);
    PhysicsSettings physics = world->system.GetPhysicsSettings();
    physics.mNumVelocitySteps = std::max<uint32_t>(settings->velocity_steps, 1);
    physics.mNumPositionSteps = std::max<uint32_t>(settings->position_steps, 1);
    // MMD rigs never rest: hair and cloth follow the dancer every frame.
    physics.mAllowSleeping = false;
    physics.mBaumgarte = 0.55f;
    physics.mConstraintWarmStart = false;
    physics.mUseBodyPairContactCache = true;
    physics.mPenetrationSlop = 0.02f;
    physics.mSpeculativeContactDistance = 0.15f;
    physics.mLinearCastThreshold = 0.05f;
    physics.mLinearCastMaxPenetration = 0.25f;
    world->system.SetPhysicsSettings(physics);
    world->system.SetGravity(Vec3(settings->gravity[0], settings->gravity[1], settings->gravity[2]));
    world->spring_damping_ratio = std::max(0.0f, settings->spring_damping_ratio);
    create_ground(world);
    return world;
}

void mmd_world_destroy(MmdWorld* world) {
    if (!world)
        return;
    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    if (!world->bodies.empty()) {
        bodies.RemoveBodies(world->bodies.data(), int(world->bodies.size()));
        bodies.DestroyBodies(world->bodies.data(), int(world->bodies.size()));
    }
    destroy_ground(world);
    delete world;
    jolt_release();
}

void mmd_world_set_gravity(MmdWorld* world, float x, float y, float z) {
    if (world)
        world->system.SetGravity(Vec3(x, y, z));
}

void mmd_world_set_ground(MmdWorld* world, int32_t enabled) {
    if (!world)
        return;
    if (enabled != 0)
        create_ground(world);
    else
        destroy_ground(world);
}

int32_t mmd_world_add_body(MmdWorld* world, const MmdBodyDesc* desc) {
    if (!world || !desc || !finite(desc->size, 3) || !finite(desc->position, 3) ||
        !finite(desc->rotation, 4))
        return -1;
    const float min_size = 1.0e-3f;
    Ref<Shape> shape;
    switch (desc->shape) {
    case MMD_SHAPE_SPHERE:
        shape = new SphereShape(std::max(desc->size[0], min_size));
        break;
    case MMD_SHAPE_BOX: {
        const Vec3 half(std::max(desc->size[0], min_size), std::max(desc->size[1], min_size),
                        std::max(desc->size[2], min_size));
        shape = new BoxShape(half, std::min(cDefaultConvexRadius, half.ReduceMin() * 0.5f));
        break;
    }
    case MMD_SHAPE_CAPSULE:
        shape = new CapsuleShape(std::max(desc->size[1] * 0.5f, min_size),
                                 std::max(desc->size[0], min_size));
        break;
    default:
        return -1;
    }

    const uint32_t index = uint32_t(world->bodies.size());
    const bool kinematic = desc->kinematic != 0;
    BodyCreationSettings settings(
        shape, RVec3(desc->position[0], desc->position[1], desc->position[2]),
        load_quat(desc->rotation),
        kinematic ? EMotionType::Kinematic : EMotionType::Dynamic,
        kinematic ? kLayerKinematic : kLayerDynamic);
    settings.mAllowSleeping = false;
    settings.mFriction = kinematic ? 0.0f : std::max(0.0f, desc->friction);
    settings.mRestitution = 0.0f;
    settings.mLinearDamping = std::max(0.0f, desc->linear_damping);
    settings.mAngularDamping = std::max(0.0f, desc->angular_damping);
    settings.mMaxLinearVelocity = kinematic ? 500.0f : 120.0f;
    settings.mMaxAngularVelocity = 4.0f * JPH_PI * 60.0f;
    settings.mMotionQuality =
        kinematic ? EMotionQuality::LinearCast : EMotionQuality::Discrete;
    settings.mOverrideMassProperties = EOverrideMassProperties::CalculateInertia;
    settings.mMassPropertiesOverride.mMass = desc->mass > 0.0f ? desc->mass : 1.0f;
    settings.mCollisionGroup = CollisionGroup(world->groups, CollisionGroup::GroupID(index), 0);
    settings.mUserData = index;

    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    const BodyID id = bodies.CreateAndAddBody(settings, EActivation::Activate);
    if (id.IsInvalid())
        return -1;
    world->bodies.push_back(id);
    world->kinematic.push_back(kinematic);
    world->groups->group_bit.push_back(uint16_t(1u << (desc->group & 15u)));
    world->groups->mask.push_back(uint16_t(desc->collide_mask));
    return int32_t(index);
}

int32_t mmd_world_add_joint(MmdWorld* world, const MmdJointDesc* desc) {
    if (!world || !desc)
        return -1;
    const int32_t count = int32_t(world->bodies.size());
    if (desc->body_a < 0 || desc->body_b < 0 || desc->body_a >= count ||
        desc->body_b >= count || desc->body_a == desc->body_b)
        return -1;
    if (world->kinematic[desc->body_a] && world->kinematic[desc->body_b])
        return -1;

    const Quat rotation = load_quat(desc->rotation);
    SixDOFConstraintSettings settings;
    settings.mSpace = EConstraintSpace::WorldSpace;
    settings.mPosition1 = settings.mPosition2 =
        RVec3(desc->position[0], desc->position[1], desc->position[2]);
    settings.mAxisX1 = settings.mAxisX2 = rotation.RotateAxisX();
    settings.mAxisY1 = settings.mAxisY2 = rotation.RotateAxisY();
    settings.mSwingType = ESwingType::Pyramid;
    const bool anchored =
        world->kinematic[desc->body_a] || world->kinematic[desc->body_b];
    settings.mConstraintPriority = anchored ? 1 : 0;

    using A = SixDOFConstraintSettings::EAxis;
    for (int i = 0; i < 3; ++i) {
        const A axis = A(A::TranslationX + i);
        const float lo = desc->linear_lower[i];
        const float hi = desc->linear_upper[i];
        if (lo > hi)
            settings.MakeFreeAxis(axis);
        else if (lo == hi)
            settings.MakeFixedAxis(axis);
        else
            settings.SetLimitedAxis(axis, lo, hi);
    }
    for (int i = 0; i < 3; ++i) {
        const A axis = A(A::RotationX + i);
        const float lo = std::clamp(desc->angular_lower[i], -JPH_PI, JPH_PI);
        const float hi = std::clamp(desc->angular_upper[i], -JPH_PI, JPH_PI);
        if (desc->angular_lower[i] > desc->angular_upper[i])
            settings.MakeFreeAxis(axis);
        else if (hi - lo < 1.0e-4f)
            settings.MakeFixedAxis(axis);
        else
            settings.SetLimitedAxis(axis, std::min(lo, 0.0f), std::max(hi, 0.0f));
    }

    const float ratio = world->spring_damping_ratio;
    bool motors[6] = {};
    for (int i = 0; i < 6; ++i) {
        const float k = i < 3 ? desc->linear_spring[i] : desc->angular_spring[i - 3];
        const A axis = A(i);
        if (!(k > 0.0f) || !std::isfinite(k) || settings.IsFixedAxis(axis))
            continue;
        const float max_force = 10000.0f;
        const float max_torque = 10000.0f;
        settings.mMotorSettings[i] = MotorSettings(ESpringMode::StiffnessAndDamping, k,
                                                    2.0f * ratio * std::sqrt(k), max_force,
                                                    max_torque);
        motors[i] = true;
    }

    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    TwoBodyConstraint* constraint = bodies.CreateConstraint(
        &settings, world->bodies[desc->body_a], world->bodies[desc->body_b]);
    if (!constraint)
        return -1;
    auto* six = static_cast<SixDOFConstraint*>(constraint);
    for (int i = 0; i < 6; ++i)
        if (motors[i])
            six->SetMotorState(A(i), EMotorState::Position);
    six->SetTargetPositionCS(Vec3::sZero());
    six->SetTargetOrientationCS(Quat::sIdentity());
    world->system.AddConstraint(constraint);
    world->groups->linked.insert(
        MmdGroupFilter::pair(uint32_t(desc->body_a), uint32_t(desc->body_b)));
    return world->joints++;
}

void mmd_world_finish(MmdWorld* world, uint32_t rest_overlaps) {
    if (!world)
        return;
    world->system.OptimizeBroadPhase();
    if (rest_overlaps == MMD_REST_OVERLAPS_COLLIDE)
        return;
    const NarrowPhaseQuery& query = world->system.GetNarrowPhaseQueryNoLock();
    const BodyLockInterfaceNoLock& locks = world->system.GetBodyLockInterfaceNoLock();
    CollideShapeSettings settings;
    settings.mMaxSeparationDistance = 0.0f;
    const bool keep_kinematic = rest_overlaps == MMD_REST_OVERLAPS_IGNORE_DYNAMIC;
    for (uint32_t i = 0; i < world->bodies.size(); ++i) {
        if (world->kinematic[i])
            continue;
        const Body* body = locks.TryGetBody(world->bodies[i]);
        if (!body)
            continue;
        AllHitCollisionCollector<CollideShapeCollector> hits;
        query.CollideShape(body->GetShape(), Vec3::sOne(), body->GetCenterOfMassTransform(),
                           settings, RVec3::sZero(), hits);
        for (const CollideShapeResult& hit : hits.mHits) {
            const Body* other = locks.TryGetBody(hit.mBodyID2);
            if (!other)
                continue;
            const uint32_t j = uint32_t(other->GetUserData());
            if (j == i || j >= world->bodies.size() ||
                (keep_kinematic && world->kinematic[j]))
                continue;
            if (hit.mPenetrationDepth > 0.0f)
                world->groups->linked.insert(MmdGroupFilter::pair(i, j));
        }
    }
}

uint32_t mmd_world_body_count(const MmdWorld* world) {
    return world ? uint32_t(world->bodies.size()) : 0;
}

void mmd_world_move_kinematic(MmdWorld* world, const float* transforms, uint32_t count, float dt) {
    if (!world || !transforms || !(dt > 0.0f))
        return;
    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    const uint32_t n = std::min<uint32_t>(count, uint32_t(world->bodies.size()));
    for (uint32_t i = 0; i < n; ++i) {
        if (!world->kinematic[i])
            continue;
        const float* t = transforms + i * 7;
        if (!finite(t, 7))
            continue;
        bodies.MoveKinematic(world->bodies[i], RVec3(t[0], t[1], t[2]), load_quat(t + 3), dt);
    }
}

void mmd_world_add_impulse(MmdWorld* world, uint32_t index, int32_t local, const float* velocity,
                           const float* torque) {
    if (!world || index >= world->bodies.size() || world->kinematic[index] || !velocity ||
        !torque)
        return;
    if (!finite(velocity, 3) || !finite(torque, 3))
        return;
    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    Vec3 linear(velocity[0], velocity[1], velocity[2]);
    Vec3 angular(torque[0], torque[1], torque[2]);
    if (local != 0) {
        const Quat rotation = bodies.GetRotation(world->bodies[index]);
        linear = rotation * linear;
        angular = rotation * angular;
    }
    bodies.AddLinearAndAngularVelocity(world->bodies[index], linear, angular);
}

void mmd_world_step(MmdWorld* world, float dt, uint32_t collision_steps) {
    if (!world || !(dt > 0.0f))
        return;
    world->system.Update(dt, int(std::max<uint32_t>(collision_steps, 1)), &world->temp,
                         g_job_system);
}

void mmd_world_read(const MmdWorld* world, float* transforms, uint32_t count) {
    if (!world || !transforms)
        return;
    const BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    const uint32_t n = std::min<uint32_t>(count, uint32_t(world->bodies.size()));
    for (uint32_t i = 0; i < n; ++i) {
        RVec3 position;
        Quat rotation;
        bodies.GetPositionAndRotation(world->bodies[i], position, rotation);
        float* t = transforms + i * 7;
        t[0] = float(position.GetX());
        t[1] = float(position.GetY());
        t[2] = float(position.GetZ());
        t[3] = rotation.GetX();
        t[4] = rotation.GetY();
        t[5] = rotation.GetZ();
        t[6] = rotation.GetW();
    }
}

void mmd_world_reset(MmdWorld* world, const float* transforms, uint32_t count) {
    if (!world || !transforms)
        return;
    BodyInterface& bodies = world->system.GetBodyInterfaceNoLock();
    const uint32_t n = std::min<uint32_t>(count, uint32_t(world->bodies.size()));
    for (uint32_t i = 0; i < n; ++i) {
        const float* t = transforms + i * 7;
        if (!finite(t, 7))
            continue;
        bodies.SetPositionAndRotation(world->bodies[i], RVec3(t[0], t[1], t[2]),
                                      load_quat(t + 3), EActivation::Activate);
        bodies.SetLinearAndAngularVelocity(world->bodies[i], Vec3::sZero(), Vec3::sZero());
    }
}

} // extern "C"
