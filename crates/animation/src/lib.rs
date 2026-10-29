//! # Hikari Animation
//!
//! High-performance animation runtime: Bézier interpolation, skeleton hierarchy,
//! CCD-IK solver, and morph blending.

pub mod bezier;
pub mod math;
pub mod morph;
pub mod skeleton;
pub mod timeline;

pub use bezier::eval_mmd_bezier;
pub use math::{Mat4, Quat, Vec3};
pub use morph::MorphEvaluator;
pub use skeleton::Skeleton;
pub use timeline::MotionPlayer;
