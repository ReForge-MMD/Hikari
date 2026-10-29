//! Parser and serializer for Vocaloid Pose Data (VPD) format.

#[derive(Debug, Clone, PartialEq)]
pub struct VpdPose {
    pub model_name: String,
    pub bones: Vec<VpdBonePose>,
    pub morphs: Vec<VpdMorphPose>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VpdBonePose {
    pub index: u32,
    pub name: String,
    pub translation: [f32; 3],
    pub rotation: [f32; 4], // Quaternion [X, Y, Z, W]
}

#[derive(Debug, Clone, PartialEq)]
pub struct VpdMorphPose {
    pub index: u32,
    pub name: String,
    pub weight: f32,
}

impl VpdPose {
    /// Parses a VPD text file. Supports Shift-JIS, UTF-8, and ASCII.
    pub fn parse(content: &str) -> Result<Self, String> {
        let mut lines = content.lines().map(|l| l.trim()).filter(|l| !l.is_empty());
        
        let header = lines.next().ok_or_else(|| "Empty VPD file".to_string())?;
        if !header.starts_with("Vocaloid Pose Data file") {
            return Err("Invalid VPD header".to_string());
        }

        let model_line = lines.next().unwrap_or("");
        let model_name = model_line.trim_end_matches(';').trim().to_string();

        let _count_line = lines.next().unwrap_or("");

        let mut bones = Vec::new();
        let mut morphs = Vec::new();

        let mut current_block_name = String::new();
        let mut current_block_lines = Vec::new();

        for line in lines {
            if line.contains('{') {
                if let Some((name, rest)) = line.split_once('{') {
                    current_block_name = name.trim().to_string();
                    current_block_lines.clear();
                    let trimmed_rest = rest.trim();
                    if !trimmed_rest.is_empty() {
                        current_block_lines.push(trimmed_rest);
                    }
                }
            } else if line.starts_with('}') {
                if current_block_name.starts_with("Bone") {
                    if let Ok(pose) = parse_bone_block(&current_block_name, &current_block_lines) {
                        bones.push(pose);
                    }
                } else if current_block_name.starts_with("Morph") {
                    if let Ok(pose) = parse_morph_block(&current_block_name, &current_block_lines) {
                        morphs.push(pose);
                    }
                }
                current_block_name.clear();
                current_block_lines.clear();
            } else if !line.is_empty() {
                current_block_lines.push(line);
            }
        }

        Ok(Self {
            model_name,
            bones,
            morphs,
        })
    }
}

fn parse_bone_block(header: &str, lines: &[&str]) -> Result<VpdBonePose, String> {
    let index: u32 = header
        .trim_start_matches("Bone")
        .parse()
        .map_err(|e| format!("Invalid bone index in {header}: {e}"))?;

    let name = lines.first().unwrap_or(&"").trim().trim_end_matches(';').to_string();

    let mut trans = [0.0f32; 3];
    let mut rot = [0.0f32, 0.0, 0.0, 1.0];

    if lines.len() >= 3 {
        // Line 1: trans
        let t_parts: Vec<&str> = lines[1].trim_end_matches(';').split(',').map(|s| s.trim()).collect();
        if t_parts.len() == 3 {
            trans[0] = t_parts[0].parse().unwrap_or(0.0);
            trans[1] = t_parts[1].parse().unwrap_or(0.0);
            trans[2] = t_parts[2].parse().unwrap_or(0.0);
        }
        // Line 2: rot
        let r_parts: Vec<&str> = lines[2].trim_end_matches(';').split(',').map(|s| s.trim()).collect();
        if r_parts.len() == 4 {
            rot[0] = r_parts[0].parse().unwrap_or(0.0);
            rot[1] = r_parts[1].parse().unwrap_or(0.0);
            rot[2] = r_parts[2].parse().unwrap_or(0.0);
            rot[3] = r_parts[3].parse().unwrap_or(1.0);
        }
    }

    Ok(VpdBonePose {
        index,
        name,
        translation: trans,
        rotation: rot,
    })
}

fn parse_morph_block(header: &str, lines: &[&str]) -> Result<VpdMorphPose, String> {
    let index: u32 = header
        .trim_start_matches("Morph")
        .parse()
        .map_err(|e| format!("Invalid morph index in {header}: {e}"))?;

    let name = lines.first().unwrap_or(&"").trim().trim_end_matches(';').to_string();
    let mut weight = 0.0f32;
    if lines.len() >= 2 {
        weight = lines[1].trim_end_matches(';').trim().parse().unwrap_or(0.0);
    }

    Ok(VpdMorphPose {
        index,
        name,
        weight,
    })
}
