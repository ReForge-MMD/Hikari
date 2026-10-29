//! VMD motion timeline player and curve sampler.

use crate::bezier::{eval_mmd_bezier, unpack_bone_bezier};
use crate::math::{Quat, Vec3};
use crate::morph::MorphEvaluator;
use crate::skeleton::Skeleton;
use hikari_formats::vmd::{VmdBoneFrame, VmdMorphFrame, VmdMotion};
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct MotionPlayer {
    bone_tracks: HashMap<String, Vec<VmdBoneFrame>>,
    morph_tracks: HashMap<String, Vec<VmdMorphFrame>>,
    pub max_frame: u32,
}

impl MotionPlayer {
    pub fn new(motion: &VmdMotion) -> Self {
        let mut bone_tracks: HashMap<String, Vec<VmdBoneFrame>> = HashMap::new();
        let mut morph_tracks: HashMap<String, Vec<VmdMorphFrame>> = HashMap::new();
        let mut max_frame = 0;

        for bf in &motion.bone_frames {
            max_frame = max_frame.max(bf.frame);
            bone_tracks.entry(bf.name.clone()).or_default().push(bf.clone());
        }

        for mf in &motion.morph_frames {
            max_frame = max_frame.max(mf.frame);
            morph_tracks.entry(mf.name.clone()).or_default().push(mf.clone());
        }

        for track in bone_tracks.values_mut() {
            track.sort_by_key(|f| f.frame);
        }

        for track in morph_tracks.values_mut() {
            track.sort_by_key(|f| f.frame);
        }

        Self {
            bone_tracks,
            morph_tracks,
            max_frame,
        }
    }

    /// Evaluates all bone and morph curves at fractional frame `frame` (e.g. 12.5 at 60fps/120fps)
    /// and applies them to the skeleton and morph evaluator.
    pub fn evaluate_pose(&self, frame: f32, skeleton: &mut Skeleton, morphs: &mut MorphEvaluator) {
        // Reset all bone local transforms to rest pose before evaluating frame tracks
        for bone_idx in 0..skeleton.bones.len() {
            skeleton.local_translations[bone_idx] = Vec3::ZERO;
            skeleton.local_rotations[bone_idx] = Quat::IDENTITY;
        }

        // Evaluate bones
        for bone_idx in 0..skeleton.bones.len() {
            let name = &skeleton.bones[bone_idx].name;
            if let Some(keyframes) = self.bone_tracks.get(name) {
                let (trans, rot) = sample_bone_track(keyframes, frame);
                skeleton.set_bone_transform(bone_idx, trans, rot);
            }
        }
        skeleton.update_transforms();

        // Evaluate morphs
        morphs.reset();
        for (morph_name, keyframes) in &self.morph_tracks {
            let weight = sample_morph_track(keyframes, frame);
            morphs.set_weight(morph_name, weight);
        }
    }
}

fn sample_bone_track(frames: &[VmdBoneFrame], frame: f32) -> (Vec3, Quat) {
    if frames.is_empty() {
        return (Vec3::ZERO, Quat::IDENTITY);
    }
    if frames.len() == 1 || frame <= frames[0].frame as f32 {
        let p = frames[0].position;
        let r = frames[0].rotation;
        return (Vec3::from_array(p), Quat::from_array(r));
    }
    let last = &frames[frames.len() - 1];
    if frame >= last.frame as f32 {
        return (Vec3::from_array(last.position), Quat::from_array(last.rotation));
    }

    // Binary search for the surrounding keyframe pair
    let idx = match frames.binary_search_by(|f| (f.frame as f32).partial_cmp(&frame).unwrap()) {
        Ok(i) => return (Vec3::from_array(frames[i].position), Quat::from_array(frames[i].rotation)),
        Err(i) => i,
    };

    let f0 = &frames[idx - 1];
    let f1 = &frames[idx];

    let t = (frame - f0.frame as f32) / (f1.frame as f32 - f0.frame as f32);

    // Bézier interpolation for X, Y, Z
    let (x1, y1, x2, y2) = unpack_bone_bezier(&f1.interpolation, 0);
    let tx = eval_mmd_bezier(x1, y1, x2, y2, t);

    let (x1, y1, x2, y2) = unpack_bone_bezier(&f1.interpolation, 1);
    let ty = eval_mmd_bezier(x1, y1, x2, y2, t);

    let (x1, y1, x2, y2) = unpack_bone_bezier(&f1.interpolation, 2);
    let tz = eval_mmd_bezier(x1, y1, x2, y2, t);

    // Bézier interpolation for Rotation
    let (x1, y1, x2, y2) = unpack_bone_bezier(&f1.interpolation, 3);
    let tr = eval_mmd_bezier(x1, y1, x2, y2, t);

    let p0 = Vec3::from_array(f0.position);
    let p1 = Vec3::from_array(f1.position);
    let pos = Vec3::new(
        p0.x + (p1.x - p0.x) * tx,
        p0.y + (p1.y - p0.y) * ty,
        p0.z + (p1.z - p0.z) * tz,
    );

    let q0 = Quat::from_array(f0.rotation);
    let q1 = Quat::from_array(f1.rotation);
    let rot = q0.slerp(q1, tr);

    (pos, rot)
}

fn sample_morph_track(frames: &[VmdMorphFrame], frame: f32) -> f32 {
    if frames.is_empty() {
        return 0.0;
    }
    if frames.len() == 1 || frame <= frames[0].frame as f32 {
        return frames[0].weight;
    }
    let last = &frames[frames.len() - 1];
    if frame >= last.frame as f32 {
        return last.weight;
    }

    let idx = match frames.binary_search_by(|f| (f.frame as f32).partial_cmp(&frame).unwrap()) {
        Ok(i) => return frames[i].weight,
        Err(i) => i,
    };

    let f0 = &frames[idx - 1];
    let f1 = &frames[idx];
    let t = (frame - f0.frame as f32) / (f1.frame as f32 - f0.frame as f32);

    f0.weight + (f1.weight - f0.weight) * t
}
