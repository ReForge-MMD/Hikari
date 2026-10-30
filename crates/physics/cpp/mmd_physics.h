// C interface of the MMD rigid-body world. Rust calls it through `src/physics/ffi.rs`;
// the layouts of the structs below are mirrored there with #[repr(C)].
//
// Every transform crosses the boundary as 7 floats: position xyz, rotation xyzw.
// Coordinates are already right-handed (the PMX importer flips Z).

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MmdWorld MmdWorld;

enum MmdShape {
    MMD_SHAPE_SPHERE = 0,
    MMD_SHAPE_BOX = 1,
    MMD_SHAPE_CAPSULE = 2,
};

typedef struct MmdBodyDesc {
    uint32_t shape;
    // Sphere: radius. Box: half extents. Capsule: radius, cylinder height.
    float size[3];
    float position[3];
    float rotation[4];
    float mass;
    // Jolt damping coefficients (1/s), converted from MMD's per-second retention.
    float linear_damping;
    float angular_damping;
    float restitution;
    float friction;
    // Collision group index (0..15) and the groups this body collides with (bit set = collide).
    uint32_t group;
    uint32_t collide_mask;
    // 1: the body follows its bone (MMD mode 0). 0: simulated (modes 1 and 2).
    uint32_t kinematic;
} MmdBodyDesc;

typedef struct MmdJointDesc {
    int32_t body_a;
    int32_t body_b;
    float position[3];
    float rotation[4];
    float linear_lower[3];
    float linear_upper[3];
    float angular_lower[3];
    float angular_upper[3];
    // Spring stiffness per axis; 0 leaves the axis without a spring.
    float linear_spring[3];
    float angular_spring[3];
} MmdJointDesc;

typedef struct MmdWorldSettings {
    float gravity[3];
    // Damping ratio of the joint springs (0 = undamped).
    float spring_damping_ratio;
    uint32_t velocity_steps;
    uint32_t position_steps;
} MmdWorldSettings;

MmdWorld *mmd_world_create(const MmdWorldSettings *settings, uint32_t max_bodies);
void mmd_world_destroy(MmdWorld *world);
void mmd_world_set_gravity(MmdWorld *world, float x, float y, float z);

// Returns the body index, or -1 when the description is invalid.
int32_t mmd_world_add_body(MmdWorld *world, const MmdBodyDesc *desc);
// Returns the joint index, or -1 when a body index is out of range.
int32_t mmd_world_add_joint(MmdWorld *world, const MmdJointDesc *desc);
enum MmdRestOverlaps {
    // Bodies overlapping in the bind pose collide like any others.
    MMD_REST_OVERLAPS_COLLIDE = 0,
    // Overlapping simulated bodies ignore each other; contacts with bone-following
    // bodies stay (cloth keeps out of the legs).
    MMD_REST_OVERLAPS_IGNORE_DYNAMIC = 1,
    // Every pair overlapping in the bind pose ignores each other.
    MMD_REST_OVERLAPS_IGNORE_ALL = 2,
};

// Call once after the last joint, with the bodies in the bind pose: builds the broad
// phase and applies the rest-overlap policy.
void mmd_world_finish(MmdWorld *world, uint32_t rest_overlaps);

uint32_t mmd_world_body_count(const MmdWorld *world);

// Kinematic bodies move to `transforms[i]` over `dt`; other entries are ignored.
void mmd_world_move_kinematic(MmdWorld *world, const float *transforms, uint32_t count, float dt);
void mmd_world_step(MmdWorld *world, float dt, uint32_t collision_steps);
void mmd_world_read(const MmdWorld *world, float *transforms, uint32_t count);
// Teleport every body to `transforms` and stop it.
void mmd_world_reset(MmdWorld *world, const float *transforms, uint32_t count);

#ifdef __cplusplus
}
#endif
