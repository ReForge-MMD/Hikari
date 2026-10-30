//! # Hikari Physics
//!
//! High-performance Jolt Physics integration for MMD models.
//! Simulates rigid bodies, 6DOF spring constraints, hair, and cloth with
//! collision filtering, rest-pose overlap protection, and substepping.

pub mod coords;
pub mod ffi;
pub mod rig;
pub mod world;

pub use glam;
pub use rig::{BoneOverride, PhysicsRig, PhysicsSettings};
pub use world::{BodyTransform, RestOverlaps, World};
