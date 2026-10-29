//! Morph blending and deformation accumulator.

use crate::math::Vec3;
use hikari_formats::pmx::{PmxModel, PmxMorph};
use std::collections::HashMap;

#[derive(Debug, Clone)]
pub struct MorphEvaluator {
    pub morphs: Vec<PmxMorph>,
    pub vertex_count: usize,
    pub morph_weights: HashMap<String, f32>,
}

impl MorphEvaluator {
    pub fn new() -> Self {
        Self {
            morphs: Vec::new(),
            vertex_count: 0,
            morph_weights: HashMap::new(),
        }
    }

    /// Creates an evaluator initialized with the PMX model's morph definitions and vertex count.
    pub fn from_pmx(pmx: &PmxModel) -> Self {
        Self {
            morphs: pmx.morphs.clone(),
            vertex_count: pmx.vertices.len(),
            morph_weights: HashMap::new(),
        }
    }

    pub fn set_weight(&mut self, name: &str, weight: f32) {
        self.morph_weights.insert(name.to_string(), weight.clamp(0.0, 1.0));
    }

    pub fn get_weight(&self, name: &str) -> f32 {
        self.morph_weights.get(name).copied().unwrap_or(0.0)
    }

    pub fn reset(&mut self) {
        self.morph_weights.clear();
    }

    /// Evaluates cumulative vertex position and UV offsets based on active morph weights.
    /// Supports direct vertex morphs, UV morphs, and nested group morphs.
    pub fn compute_offsets(&self) -> (Vec<Vec3>, Vec<[f32; 2]>) {
        if self.vertex_count == 0 || self.morphs.is_empty() {
            return (Vec::new(), Vec::new());
        }

        let mut final_vertex_weights = HashMap::<usize, f32>::new();
        let mut final_uv_weights = HashMap::<usize, f32>::new();

        fn accumulate_morph(
            morphs: &[PmxMorph],
            idx: usize,
            current_weight: f32,
            depth: usize,
            v_weights: &mut HashMap<usize, f32>,
            uv_weights: &mut HashMap<usize, f32>,
        ) {
            if depth > 4 || idx >= morphs.len() || current_weight <= 1e-5 {
                return;
            }
            let morph = &morphs[idx];
            match &morph.data {
                hikari_formats::pmx::PmxMorphData::Vertex(_) => {
                    *v_weights.entry(idx).or_insert(0.0) += current_weight;
                }
                hikari_formats::pmx::PmxMorphData::Uv(_) => {
                    *uv_weights.entry(idx).or_insert(0.0) += current_weight;
                }
                hikari_formats::pmx::PmxMorphData::Group(items) => {
                    for &(sub_idx, sub_ratio) in items {
                        if sub_idx >= 0 {
                            accumulate_morph(
                                morphs,
                                sub_idx as usize,
                                current_weight * sub_ratio,
                                depth + 1,
                                v_weights,
                                uv_weights,
                            );
                        }
                    }
                }
                _ => {}
            }
        }

        for (i, morph) in self.morphs.iter().enumerate() {
            let w = self.get_weight(&morph.name_local);
            if w > 1e-5 {
                accumulate_morph(
                    &self.morphs,
                    i,
                    w,
                    0,
                    &mut final_vertex_weights,
                    &mut final_uv_weights,
                );
            }
        }

        let mut pos_offsets = vec![Vec3::ZERO; self.vertex_count];
        let mut uv_offsets = vec![[0.0f32; 2]; self.vertex_count];

        for (&morph_idx, &total_w) in &final_vertex_weights {
            if let hikari_formats::pmx::PmxMorphData::Vertex(deltas) = &self.morphs[morph_idx].data {
                for &(v_idx, delta) in deltas {
                    let idx = v_idx as usize;
                    if idx < self.vertex_count {
                        pos_offsets[idx] = pos_offsets[idx] + Vec3::from_array(delta) * total_w;
                    }
                }
            }
        }

        for (&morph_idx, &total_w) in &final_uv_weights {
            if let hikari_formats::pmx::PmxMorphData::Uv(deltas) = &self.morphs[morph_idx].data {
                for &(v_idx, delta) in deltas {
                    let idx = v_idx as usize;
                    if idx < self.vertex_count {
                        uv_offsets[idx][0] += delta[0] * total_w;
                        uv_offsets[idx][1] += delta[1] * total_w;
                    }
                }
            }
        }

        (pos_offsets, uv_offsets)
    }

    /// Evaluates cumulative vertex position offsets for the entire model based on active morph weights.
    pub fn compute_vertex_offsets(&self, pmx: &PmxModel) -> Vec<Vec3> {
        let mut eval = Self::from_pmx(pmx);
        eval.morph_weights = self.morph_weights.clone();
        eval.compute_offsets().0
    }
}
