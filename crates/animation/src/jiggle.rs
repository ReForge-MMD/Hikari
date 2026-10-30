//! Inertial secondary motion of soft tissue (chest and hips).
//!
//! Simulates a damped 3D harmonic oscillator on top of the skeleton pose.
//! Front-to-back, vertical, and lateral stiffness ensure natural bounce without
//! mesh tearing or clipping.

use crate::math::{Quat, Vec3};
use hikari_formats::PmxModel;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Spring {
    /// Natural frequency of the bounce in Hz.
    pub frequency: f32,
    /// Damping ratio (0.2 - 0.3 for natural bounce with slight decay).
    pub damping: f32,
    /// Stiffness multipliers in torso frame: [sideways, vertical, front-to-back].
    pub stiffness: [f32; 3],
    /// Maximum allowed displacement as a fraction of model height.
    pub max_offset: f32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct JiggleSettings {
    pub enabled: bool,
    /// Scales overall jiggle offset (0.0 = off, 1.0 = standard).
    pub strength: f32,
    pub chest: Spring,
    pub hips: Spring,
}

impl Default for JiggleSettings {
    fn default() -> Self {
        Self {
            enabled: true,
            strength: 1.0,
            chest: Spring {
                frequency: 4.2,
                damping: 0.22,
                stiffness: [1.6, 1.0, 2.2],
                max_offset: 0.012,
            },
            hips: Spring {
                frequency: 3.4,
                damping: 0.30,
                stiffness: [1.4, 1.0, 2.0],
                max_offset: 0.009,
            },
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Region {
    Chest,
    Hips,
}

#[derive(Clone, Debug)]
struct Body {
    bone: usize,
    region: Region,
    frame: Option<usize>,
    position: Vec3,
    velocity: Vec3,
    offset: Vec3,
}

#[derive(Clone, Debug)]
pub struct Jiggle {
    bodies: Vec<Body>,
    height: f32,
    primed: bool,
    overrides: Vec<Option<Vec3>>, // Bone index -> position offset
}

const CHEST_KEYWORDS: [&str; 5] = ["胸", "乳", "おっぱい", "bust", "breast"];
const HIPS_KEYWORDS: [&str; 5] = ["尻", "ヒップ", "臀", "hip", "butt"];
const NOT_SOFT_KEYWORDS: [&str; 10] = ["上半身", "下半身", "胸元", "ネクタイ", "リボン", "腰", "花", "飾", "IK", "ＩＫ"];

fn words(name: &str) -> Vec<String> {
    name.to_lowercase()
        .split(|c: char| !c.is_ascii_alphabetic())
        .filter(|w| !w.is_empty())
        .map(str::to_string)
        .collect()
}

fn region_of(name: &str) -> Option<Region> {
    if NOT_SOFT_KEYWORDS.iter().any(|w| name.contains(w)) {
        return None;
    }
    let latin = words(name);
    let has = |list: &[&str]| {
        list.iter().any(|w| {
            if w.is_ascii() {
                latin.iter().any(|l| l == w)
            } else {
                name.contains(w)
            }
        })
    };

    if has(&CHEST_KEYWORDS) {
        Some(Region::Chest)
    } else if has(&HIPS_KEYWORDS) {
        Some(Region::Hips)
    } else {
        None
    }
}

fn side_of(name: &str) -> i8 {
    let latin = words(name);
    if name.contains('左') || latin.iter().any(|w| w == "l" || w == "left") {
        -1
    } else if name.contains('右') || latin.iter().any(|w| w == "r" || w == "right") {
        1
    } else {
        0
    }
}

impl Jiggle {
    pub fn new(pmx: &PmxModel) -> Self {
        let count = pmx.bones.len();
        let mut best: Vec<((Region, i8), usize)> = Vec::new();

        for (i, b) in pmx.bones.iter().enumerate() {
            let Some(region) = region_of(&b.name_local) else {
                continue;
            };
            let side = side_of(&b.name_local);
            if side == 0 {
                continue;
            }
            let key = (region, side);
            if !best.iter().any(|(k, _)| *k == key) {
                best.push((key, i));
            }
        }

        let bodies = best
            .iter()
            .map(|&((region, _), bone)| {
                let parent = if pmx.bones[bone].parent_index >= 0 {
                    Some(pmx.bones[bone].parent_index as usize)
                } else {
                    None
                };
                Body {
                    bone,
                    region,
                    frame: parent,
                    position: Vec3::ZERO,
                    velocity: Vec3::ZERO,
                    offset: Vec3::ZERO,
                }
            })
            .collect();

        let (lo_y, hi_y) = pmx
            .bones
            .iter()
            .fold((f32::MAX, f32::MIN), |(lo, hi), b| {
                (lo.min(b.position[1]), hi.max(b.position[1]))
            });
        let height = (hi_y - lo_y).max(1.0);

        Self {
            bodies,
            height,
            primed: false,
            overrides: vec![None; count],
        }
    }

    pub fn reset(&mut self) {
        self.primed = false;
    }

    pub fn body_count(&self) -> usize {
        self.bodies.len()
    }

    /// Steps the jiggle springs and returns displacement vectors for affected bones.
    pub fn step(
        &mut self,
        world_positions: &[Vec3],
        world_rotations: &[Quat],
        dt: f32,
        settings: &JiggleSettings,
    ) -> Option<&[Option<Vec3>]> {
        if !settings.enabled || settings.strength <= 0.0 || self.bodies.is_empty() {
            return None;
        }

        let restart = !self.primed || !(dt > 0.0) || dt > 0.25;
        self.primed = true;

        let substeps = ((dt / (1.0 / 240.0)).ceil() as u32).clamp(1, 16);
        let h = dt / substeps as f32;
        self.overrides.fill(None);

        for body in &mut self.bodies {
            let target = world_positions.get(body.bone).copied().unwrap_or(Vec3::ZERO);
            let frame = body
                .frame
                .and_then(|f| world_rotations.get(f).copied())
                .unwrap_or(Quat::IDENTITY);

            let spring = match body.region {
                Region::Chest => settings.chest,
                Region::Hips => settings.hips,
            };

            if restart {
                body.position = target;
                body.velocity = Vec3::ZERO;
                body.offset = Vec3::ZERO;
            } else {
                let omega = std::f32::consts::TAU * spring.frequency;
                let k = Vec3::new(
                    spring.stiffness[0] * omega * omega,
                    spring.stiffness[1] * omega * omega,
                    spring.stiffness[2] * omega * omega,
                );
                let c = Vec3::new(
                    spring.stiffness[0].sqrt() * 2.0 * spring.damping * omega,
                    spring.stiffness[1].sqrt() * 2.0 * spring.damping * omega,
                    spring.stiffness[2].sqrt() * 2.0 * spring.damping * omega,
                );

                let start = body.position - body.offset;
                let inv_frame = frame.inverse();

                for s in 1..=substeps {
                    let a_factor = s as f32 / substeps as f32;
                    let anchor = start + (target - start) * a_factor;
                    let d = inv_frame.rotate_vec3(body.position - anchor);
                    let v = inv_frame.rotate_vec3(body.velocity);

                    let acc = Vec3::new(
                        -k.x * d.x - c.x * v.x,
                        -k.y * d.y - c.y * v.y,
                        -k.z * d.z - c.z * v.z,
                    );

                    body.velocity = body.velocity + frame.rotate_vec3(acc) * h;
                    body.position = body.position + body.velocity * h;
                }

                let max_d = spring.max_offset * self.height;
                let mut offset = body.position - target;
                let len = offset.length();
                if len > max_d && len > 1e-6 {
                    offset = offset * (max_d / len);
                    body.position = target + offset;
                    let n = offset.normalize();
                    let dot = body.velocity.dot(n);
                    if dot > 0.0 {
                        body.velocity = body.velocity - n * dot;
                    }
                }
                body.offset = offset;
            }

            let shown = body.offset * settings.strength;
            self.overrides[body.bone] = Some(shown);
        }

        Some(&self.overrides)
    }
}
