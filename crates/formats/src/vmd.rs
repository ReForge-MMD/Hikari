//! Parser and serializer for Vocaloid Motion Data (VMD) format.

use std::io::{self, Cursor, Read};

#[derive(Debug, Clone, PartialEq)]
pub struct VmdMotion {
    pub version: u32,
    pub model_name: String,
    pub bone_frames: Vec<VmdBoneFrame>,
    pub morph_frames: Vec<VmdMorphFrame>,
    pub camera_frames: Vec<VmdCameraFrame>,
    pub light_frames: Vec<VmdLightFrame>,
    pub shadow_frames: Vec<VmdShadowFrame>,
    pub ik_frames: Vec<VmdIkFrame>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdBoneFrame {
    pub name: String,
    pub frame: u32,
    pub position: [f32; 3],
    pub rotation: [f32; 4], // Quaternion [X, Y, Z, W]
    pub interpolation: [u8; 64], // Bézier curves for X, Y, Z, Rot
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdMorphFrame {
    pub name: String,
    pub frame: u32,
    pub weight: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdCameraFrame {
    pub frame: u32,
    pub distance: f32,
    pub position: [f32; 3],
    pub rotation: [f32; 3], // Euler angles in radians
    pub interpolation: [u8; 24],
    pub fov: u32,
    pub perspective: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdLightFrame {
    pub frame: u32,
    pub color: [f32; 3],
    pub direction: [f32; 3],
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdShadowFrame {
    pub frame: u32,
    pub mode: u8,
    pub distance: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdIkSwitch {
    pub name: String,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct VmdIkFrame {
    pub frame: u32,
    pub show: bool,
    pub ik_switches: Vec<VmdIkSwitch>,
}

fn decode_shift_jis(bytes: &[u8]) -> String {
    let null_pos = bytes.iter().position(|&b| b == 0).unwrap_or(bytes.len());
    let clean = &bytes[..null_pos];
    let (res, _, _) = encoding_rs::SHIFT_JIS.decode(clean);
    res.into_owned()
}

struct VmdReader<'a> {
    cursor: Cursor<&'a [u8]>,
}

impl<'a> VmdReader<'a> {
    fn new(bytes: &'a [u8]) -> Self {
        Self {
            cursor: Cursor::new(bytes),
        }
    }

    fn read_exact(&mut self, buf: &mut [u8]) -> io::Result<()> {
        self.cursor.read_exact(buf)
    }

    fn read_u8(&mut self) -> io::Result<u8> {
        let mut b = [0u8; 1];
        self.cursor.read_exact(&mut b)?;
        Ok(b[0])
    }

    fn read_u32(&mut self) -> io::Result<u32> {
        let mut b = [0u8; 4];
        self.cursor.read_exact(&mut b)?;
        Ok(u32::from_le_bytes(b))
    }

    fn read_f32(&mut self) -> io::Result<f32> {
        let mut b = [0u8; 4];
        self.cursor.read_exact(&mut b)?;
        Ok(f32::from_le_bytes(b))
    }

    fn read_vec3(&mut self) -> io::Result<[f32; 3]> {
        Ok([self.read_f32()?, self.read_f32()?, self.read_f32()?])
    }

    fn read_vec4(&mut self) -> io::Result<[f32; 4]> {
        Ok([self.read_f32()?, self.read_f32()?, self.read_f32()?, self.read_f32()?])
    }
}

impl VmdMotion {
    /// Parses a VMD motion file from raw bytes.
    pub fn parse(bytes: &[u8]) -> Result<Self, String> {
        if bytes.len() < 50 {
            return Err("VMD data too short".to_string());
        }

        let mut r = VmdReader::new(bytes);
        let mut magic = [0u8; 30];
        r.read_exact(&mut magic).map_err(|e| e.to_string())?;

        let version = if magic.starts_with(b"Vocaloid Motion Data 0002") {
            2
        } else if magic.starts_with(b"Vocaloid Motion Data file") {
            1
        } else {
            return Err("Invalid VMD magic header".to_string());
        };

        let name_len = if version == 2 { 20 } else { 10 };
        let mut model_name_bytes = vec![0u8; name_len];
        r.read_exact(&mut model_name_bytes).map_err(|e| e.to_string())?;
        let model_name = decode_shift_jis(&model_name_bytes);

        // Bone keyframes
        let bone_count = r.read_u32().map_err(|e| e.to_string())?;
        let mut bone_frames = Vec::with_capacity(bone_count as usize);
        for _ in 0..bone_count {
            let mut name_buf = [0u8; 15];
            r.read_exact(&mut name_buf).map_err(|e| e.to_string())?;
            let name = decode_shift_jis(&name_buf);
            let frame = r.read_u32().map_err(|e| e.to_string())?;
            let position = r.read_vec3().map_err(|e| e.to_string())?;
            let rotation = r.read_vec4().map_err(|e| e.to_string())?;
            let mut interpolation = [0u8; 64];
            r.read_exact(&mut interpolation).map_err(|e| e.to_string())?;

            bone_frames.push(VmdBoneFrame {
                name,
                frame,
                position,
                rotation,
                interpolation,
            });
        }

        // Morph keyframes
        let morph_count = r.read_u32().unwrap_or(0);
        let mut morph_frames = Vec::with_capacity(morph_count as usize);
        for _ in 0..morph_count {
            let mut name_buf = [0u8; 15];
            r.read_exact(&mut name_buf).map_err(|e| e.to_string())?;
            let name = decode_shift_jis(&name_buf);
            let frame = r.read_u32().map_err(|e| e.to_string())?;
            let weight = r.read_f32().map_err(|e| e.to_string())?;

            morph_frames.push(VmdMorphFrame { name, frame, weight });
        }

        // Camera keyframes
        let camera_count = r.read_u32().unwrap_or(0);
        let mut camera_frames = Vec::with_capacity(camera_count as usize);
        for _ in 0..camera_count {
            let frame = r.read_u32().map_err(|e| e.to_string())?;
            let distance = r.read_f32().map_err(|e| e.to_string())?;
            let position = r.read_vec3().map_err(|e| e.to_string())?;
            let rotation = r.read_vec3().map_err(|e| e.to_string())?;
            let mut interpolation = [0u8; 24];
            r.read_exact(&mut interpolation).map_err(|e| e.to_string())?;
            let fov = r.read_u32().map_err(|e| e.to_string())?;
            let p_val = r.read_u8().map_err(|e| e.to_string())?;
            let perspective = p_val == 0;

            camera_frames.push(VmdCameraFrame {
                frame,
                distance,
                position,
                rotation,
                interpolation,
                fov,
                perspective,
            });
        }

        // Light keyframes
        let light_count = r.read_u32().unwrap_or(0);
        let mut light_frames = Vec::with_capacity(light_count as usize);
        for _ in 0..light_count {
            let frame = r.read_u32().map_err(|e| e.to_string())?;
            let color = r.read_vec3().map_err(|e| e.to_string())?;
            let direction = r.read_vec3().map_err(|e| e.to_string())?;
            light_frames.push(VmdLightFrame { frame, color, direction });
        }

        // Shadow keyframes
        let shadow_count = r.read_u32().unwrap_or(0);
        let mut shadow_frames = Vec::with_capacity(shadow_count as usize);
        for _ in 0..shadow_count {
            let frame = r.read_u32().map_err(|e| e.to_string())?;
            let mode = r.read_u8().map_err(|e| e.to_string())?;
            let distance = r.read_f32().map_err(|e| e.to_string())?;
            shadow_frames.push(VmdShadowFrame { frame, mode, distance });
        }

        // IK keyframes
        let ik_count = r.read_u32().unwrap_or(0);
        let mut ik_frames = Vec::with_capacity(ik_count as usize);
        for _ in 0..ik_count {
            let frame = r.read_u32().map_err(|e| e.to_string())?;
            let show = r.read_u8().map_err(|e| e.to_string())? != 0;

            let switch_count = r.read_u32().map_err(|e| e.to_string())?;
            let mut ik_switches = Vec::with_capacity(switch_count as usize);
            for _ in 0..switch_count {
                let mut name_buf = [0u8; 20];
                r.read_exact(&mut name_buf).map_err(|e| e.to_string())?;
                let name = decode_shift_jis(&name_buf);
                let enabled = r.read_u8().map_err(|e| e.to_string())? != 0;
                ik_switches.push(VmdIkSwitch { name, enabled });
            }

            ik_frames.push(VmdIkFrame { frame, show, ik_switches });
        }

        Ok(Self {
            version,
            model_name,
            bone_frames,
            morph_frames,
            camera_frames,
            light_frames,
            shadow_frames,
            ik_frames,
        })
    }
}
