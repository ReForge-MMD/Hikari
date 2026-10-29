//! Parser and data structures for MMD PMX (Polygon Model eXtended) 2.0 / 2.1 format.

use std::io::{self, Cursor, Read};

#[derive(Debug, Clone, PartialEq)]
pub struct PmxModel {
    pub version: f32,
    pub header: PmxHeader,
    pub info: PmxModelInfo,
    pub vertices: Vec<PmxVertex>,
    pub indices: Vec<u32>,
    pub textures: Vec<String>,
    pub materials: Vec<PmxMaterial>,
    pub bones: Vec<PmxBone>,
    pub morphs: Vec<PmxMorph>,
    pub display_frames: Vec<PmxDisplayFrame>,
    pub rigid_bodies: Vec<PmxRigidBody>,
    pub joints: Vec<PmxJoint>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PmxEncoding {
    Utf16Le = 0,
    Utf8 = 1,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxHeader {
    pub encoding: PmxEncoding,
    pub additional_vec4_count: u8,
    pub vertex_index_size: u8,
    pub texture_index_size: u8,
    pub material_index_size: u8,
    pub bone_index_size: u8,
    pub morph_index_size: u8,
    pub rigid_body_index_size: u8,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PmxModelInfo {
    pub name_local: String,
    pub name_universal: String,
    pub comment_local: String,
    pub comment_universal: String,
}

#[derive(Debug, Clone, PartialEq)]
pub enum PmxWeight {
    Bdef1(i32),
    Bdef2 {
        bone1: i32,
        bone2: i32,
        weight1: f32,
    },
    Bdef4 {
        bones: [i32; 4],
        weights: [f32; 4],
    },
    Sdef {
        bone1: i32,
        bone2: i32,
        weight1: f32,
        c: [f32; 3],
        r0: [f32; 3],
        r1: [f32; 3],
    },
    Qdef {
        bones: [i32; 4],
        weights: [f32; 4],
    },
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxVertex {
    pub position: [f32; 3],
    pub normal: [f32; 3],
    pub uv: [f32; 2],
    pub extra_vec4: Vec<[f32; 4]>,
    pub weight: PmxWeight,
    pub edge_scale: f32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxMaterial {
    pub name_local: String,
    pub name_universal: String,
    pub diffuse: [f32; 4],
    pub specular: [f32; 3],
    pub shininess: f32,
    pub ambient: [f32; 3],
    pub flags: u8,
    pub edge_color: [f32; 4],
    pub edge_size: f32,
    pub texture_index: i32,
    pub sphere_texture_index: i32,
    pub sphere_mode: u8,
    pub toon_flag: u8,
    pub toon_index: i32,
    pub comment: String,
    pub surface_count: i32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxIkLink {
    pub bone_index: i32,
    pub limits: Option<([f32; 3], [f32; 3])>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxIk {
    pub target_bone: i32,
    pub loop_count: i32,
    pub angle_limit: f32,
    pub links: Vec<PmxIkLink>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxBone {
    pub name_local: String,
    pub name_universal: String,
    pub position: [f32; 3],
    pub parent_index: i32,
    pub deform_layer: i32,
    pub flags: u16,
    pub connection_target: Result<i32, [f32; 3]>,
    pub append_parent: Option<(i32, f32)>,
    pub fixed_axis: Option<[f32; 3]>,
    pub local_axis: Option<([f32; 3], [f32; 3])>,
    pub external_parent_key: Option<i32>,
    pub ik: Option<PmxIk>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum PmxMorphData {
    Group(Vec<(i32, f32)>),
    Vertex(Vec<(u32, [f32; 3])>),
    Bone(Vec<(i32, [f32; 3], [f32; 4])>),
    Uv(Vec<(u32, [f32; 4])>),
    ExtraUv(u8, Vec<(u32, [f32; 4])>),
    Material(Vec<PmxMaterialMorph>),
    Flip(Vec<(i32, f32)>),
    Impulse(Vec<(i32, u8, [f32; 3], [f32; 3])>),
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxMaterialMorph {
    pub material_index: i32,
    pub calc_mode: u8,
    pub diffuse: [f32; 4],
    pub specular: [f32; 3],
    pub shininess: f32,
    pub ambient: [f32; 3],
    pub edge_color: [f32; 4],
    pub edge_size: f32,
    pub texture_tint: [f32; 4],
    pub sphere_tint: [f32; 4],
    pub toon_tint: [f32; 4],
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxMorph {
    pub name_local: String,
    pub name_universal: String,
    pub category: u8,
    pub data: PmxMorphData,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxDisplayFrameItem {
    pub is_morph: bool,
    pub index: i32,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxDisplayFrame {
    pub name_local: String,
    pub name_universal: String,
    pub is_special: bool,
    pub items: Vec<PmxDisplayFrameItem>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxRigidBody {
    pub name_local: String,
    pub name_universal: String,
    pub bone_index: i32,
    pub collision_group: u8,
    pub collision_mask: u16,
    pub shape: u8,
    pub size: [f32; 3],
    pub position: [f32; 3],
    pub rotation: [f32; 3],
    pub mass: f32,
    pub move_damping: f32,
    pub rot_damping: f32,
    pub restitution: f32,
    pub friction: f32,
    pub mode: u8,
}

#[derive(Debug, Clone, PartialEq)]
pub struct PmxJoint {
    pub name_local: String,
    pub name_universal: String,
    pub joint_type: u8,
    pub body_a: i32,
    pub body_b: i32,
    pub position: [f32; 3],
    pub rotation: [f32; 3],
    pub pos_min: [f32; 3],
    pub pos_max: [f32; 3],
    pub rot_min: [f32; 3],
    pub rot_max: [f32; 3],
    pub spring_pos: [f32; 3],
    pub spring_rot: [f32; 3],
}

// ------------------- Binary Reader Helpers -------------------

struct Reader<'a> {
    cursor: Cursor<&'a [u8]>,
    header: PmxHeader,
}

impl<'a> Reader<'a> {
    fn new(data: &'a [u8], header: PmxHeader) -> Self {
        Self {
            cursor: Cursor::new(data),
            header,
        }
    }

    fn read_u8(&mut self) -> io::Result<u8> {
        let mut buf = [0u8; 1];
        self.cursor.read_exact(&mut buf)?;
        Ok(buf[0])
    }

    fn read_u16(&mut self) -> io::Result<u16> {
        let mut buf = [0u8; 2];
        self.cursor.read_exact(&mut buf)?;
        Ok(u16::from_le_bytes(buf))
    }

    fn read_i32(&mut self) -> io::Result<i32> {
        let mut buf = [0u8; 4];
        self.cursor.read_exact(&mut buf)?;
        Ok(i32::from_le_bytes(buf))
    }

    fn read_f32(&mut self) -> io::Result<f32> {
        let mut buf = [0u8; 4];
        self.cursor.read_exact(&mut buf)?;
        Ok(f32::from_le_bytes(buf))
    }

    fn read_vec2(&mut self) -> io::Result<[f32; 2]> {
        Ok([self.read_f32()?, self.read_f32()?])
    }

    fn read_vec3(&mut self) -> io::Result<[f32; 3]> {
        Ok([self.read_f32()?, self.read_f32()?, self.read_f32()?])
    }

    fn read_vec4(&mut self) -> io::Result<[f32; 4]> {
        Ok([self.read_f32()?, self.read_f32()?, self.read_f32()?, self.read_f32()?])
    }

    fn read_string(&mut self) -> io::Result<String> {
        let len = self.read_i32()?;
        if len <= 0 {
            return Ok(String::new());
        }
        let mut bytes = vec![0u8; len as usize];
        self.cursor.read_exact(&mut bytes)?;

        match self.header.encoding {
            PmxEncoding::Utf8 => {
                Ok(String::from_utf8_lossy(&bytes).into_owned())
            }
            PmxEncoding::Utf16Le => {
                if bytes.len() % 2 != 0 {
                    bytes.pop();
                }
                let u16s: Vec<u16> = bytes
                    .chunks_exact(2)
                    .map(|c| u16::from_le_bytes([c[0], c[1]]))
                    .collect();
                Ok(String::from_utf16_lossy(&u16s))
            }
        }
    }

    fn read_index(&mut self, size: u8, signed: bool) -> io::Result<i32> {
        match size {
            1 => {
                let v = self.read_u8()?;
                if signed {
                    Ok(v as i8 as i32)
                } else {
                    Ok(v as i32)
                }
            }
            2 => {
                let mut buf = [0u8; 2];
                self.cursor.read_exact(&mut buf)?;
                if signed {
                    Ok(i16::from_le_bytes(buf) as i32)
                } else {
                    Ok(u16::from_le_bytes(buf) as i32)
                }
            }
            4 => {
                let mut buf = [0u8; 4];
                self.cursor.read_exact(&mut buf)?;
                Ok(i32::from_le_bytes(buf))
            }
            _ => Err(io::Error::new(io::ErrorKind::InvalidData, "invalid index size")),
        }
    }

    fn read_vertex_index(&mut self) -> io::Result<u32> {
        self.read_index(self.header.vertex_index_size, false).map(|v| v as u32)
    }

    fn read_bone_index(&mut self) -> io::Result<i32> {
        self.read_index(self.header.bone_index_size, true)
    }

    fn read_texture_index(&mut self) -> io::Result<i32> {
        self.read_index(self.header.texture_index_size, true)
    }

    fn read_material_index(&mut self) -> io::Result<i32> {
        self.read_index(self.header.material_index_size, true)
    }

    fn read_morph_index(&mut self) -> io::Result<i32> {
        self.read_index(self.header.morph_index_size, true)
    }

    fn read_rigid_body_index(&mut self) -> io::Result<i32> {
        self.read_index(self.header.rigid_body_index_size, true)
    }
}

impl PmxModel {
    /// Parses a PMX model from bytes.
    pub fn parse(bytes: &[u8]) -> Result<Self, String> {
        if bytes.len() < 12 || &bytes[0..4] != b"PMX " {
            return Err("Invalid PMX magic header".to_string());
        }

        let version = f32::from_le_bytes([bytes[4], bytes[5], bytes[6], bytes[7]]);
        let globals_count = bytes[8] as usize;
        if globals_count < 8 || bytes.len() < 9 + globals_count {
            return Err("Incomplete PMX globals header".to_string());
        }

        let globals = &bytes[9..9 + globals_count];
        let header = PmxHeader {
            encoding: if globals[0] == 0 { PmxEncoding::Utf16Le } else { PmxEncoding::Utf8 },
            additional_vec4_count: globals[1],
            vertex_index_size: globals[2],
            texture_index_size: globals[3],
            material_index_size: globals[4],
            bone_index_size: globals[5],
            morph_index_size: globals[6],
            rigid_body_index_size: globals[7],
        };

        let mut r = Reader::new(&bytes[9 + globals_count..], header.clone());

        // Model info
        let info = PmxModelInfo {
            name_local: r.read_string().map_err(|e| e.to_string())?,
            name_universal: r.read_string().map_err(|e| e.to_string())?,
            comment_local: r.read_string().map_err(|e| e.to_string())?,
            comment_universal: r.read_string().map_err(|e| e.to_string())?,
        };

        // Vertices
        let vertex_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut vertices = Vec::with_capacity(vertex_count.max(0) as usize);
        for _ in 0..vertex_count {
            let position = r.read_vec3().map_err(|e| e.to_string())?;
            let normal = r.read_vec3().map_err(|e| e.to_string())?;
            let uv = r.read_vec2().map_err(|e| e.to_string())?;
            let mut extra_vec4 = Vec::with_capacity(header.additional_vec4_count as usize);
            for _ in 0..header.additional_vec4_count {
                extra_vec4.push(r.read_vec4().map_err(|e| e.to_string())?);
            }
            let weight_type = r.read_u8().map_err(|e| e.to_string())?;
            let weight = match weight_type {
                0 => PmxWeight::Bdef1(r.read_bone_index().map_err(|e| e.to_string())?),
                1 => PmxWeight::Bdef2 {
                    bone1: r.read_bone_index().map_err(|e| e.to_string())?,
                    bone2: r.read_bone_index().map_err(|e| e.to_string())?,
                    weight1: r.read_f32().map_err(|e| e.to_string())?,
                },
                2 => {
                    let b = [
                        r.read_bone_index().map_err(|e| e.to_string())?,
                        r.read_bone_index().map_err(|e| e.to_string())?,
                        r.read_bone_index().map_err(|e| e.to_string())?,
                        r.read_bone_index().map_err(|e| e.to_string())?,
                    ];
                    let w = [
                        r.read_f32().map_err(|e| e.to_string())?,
                        r.read_f32().map_err(|e| e.to_string())?,
                        r.read_f32().map_err(|e| e.to_string())?,
                        r.read_f32().map_err(|e| e.to_string())?,
                    ];
                    PmxWeight::Bdef4 { bones: b, weights: w }
                }
                3 => PmxWeight::Sdef {
                    bone1: r.read_bone_index().map_err(|e| e.to_string())?,
                    bone2: r.read_bone_index().map_err(|e| e.to_string())?,
                    weight1: r.read_f32().map_err(|e| e.to_string())?,
                    c: r.read_vec3().map_err(|e| e.to_string())?,
                    r0: r.read_vec3().map_err(|e| e.to_string())?,
                    r1: r.read_vec3().map_err(|e| e.to_string())?,
                },
                4 => {
                    let b = [
                        r.read_bone_index().map_err(|e| e.to_string())?,
                        r.read_bone_index().map_err(|e| e.to_string())?,
                        r.read_bone_index().map_err(|e| e.to_string())?,
                        r.read_bone_index().map_err(|e| e.to_string())?,
                    ];
                    let w = [
                        r.read_f32().map_err(|e| e.to_string())?,
                        r.read_f32().map_err(|e| e.to_string())?,
                        r.read_f32().map_err(|e| e.to_string())?,
                        r.read_f32().map_err(|e| e.to_string())?,
                    ];
                    PmxWeight::Qdef { bones: b, weights: w }
                }
                _ => return Err(format!("Unknown vertex weight type: {weight_type}")),
            };
            let edge_scale = r.read_f32().map_err(|e| e.to_string())?;
            vertices.push(PmxVertex {
                position,
                normal,
                uv,
                extra_vec4,
                weight,
                edge_scale,
            });
        }

        // Indices
        let index_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut indices = Vec::with_capacity(index_count.max(0) as usize);
        for _ in 0..index_count {
            indices.push(r.read_vertex_index().map_err(|e| e.to_string())?);
        }

        // Textures
        let texture_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut textures = Vec::with_capacity(texture_count.max(0) as usize);
        for _ in 0..texture_count {
            textures.push(r.read_string().map_err(|e| e.to_string())?);
        }

        // Materials
        let material_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut materials = Vec::with_capacity(material_count.max(0) as usize);
        for _ in 0..material_count {
            let name_local = r.read_string().map_err(|e| e.to_string())?;
            let name_universal = r.read_string().map_err(|e| e.to_string())?;
            let diffuse = r.read_vec4().map_err(|e| e.to_string())?;
            let specular = r.read_vec3().map_err(|e| e.to_string())?;
            let shininess = r.read_f32().map_err(|e| e.to_string())?;
            let ambient = r.read_vec3().map_err(|e| e.to_string())?;
            let flags = r.read_u8().map_err(|e| e.to_string())?;
            let edge_color = r.read_vec4().map_err(|e| e.to_string())?;
            let edge_size = r.read_f32().map_err(|e| e.to_string())?;
            let texture_index = r.read_texture_index().map_err(|e| e.to_string())?;
            let sphere_texture_index = r.read_texture_index().map_err(|e| e.to_string())?;
            let sphere_mode = r.read_u8().map_err(|e| e.to_string())?;
            let toon_flag = r.read_u8().map_err(|e| e.to_string())?;
            let toon_index = if toon_flag == 0 {
                r.read_texture_index().map_err(|e| e.to_string())?
            } else {
                r.read_u8().map_err(|e| e.to_string())? as i32
            };
            let comment = r.read_string().map_err(|e| e.to_string())?;
            let surface_count = r.read_i32().map_err(|e| e.to_string())?;

            materials.push(PmxMaterial {
                name_local,
                name_universal,
                diffuse,
                specular,
                shininess,
                ambient,
                flags,
                edge_color,
                edge_size,
                texture_index,
                sphere_texture_index,
                sphere_mode,
                toon_flag,
                toon_index,
                comment,
                surface_count,
            });
        }

        // Bones
        let bone_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut bones = Vec::with_capacity(bone_count.max(0) as usize);
        for _ in 0..bone_count {
            let name_local = r.read_string().map_err(|e| e.to_string())?;
            let name_universal = r.read_string().map_err(|e| e.to_string())?;
            let position = r.read_vec3().map_err(|e| e.to_string())?;
            let parent_index = r.read_bone_index().map_err(|e| e.to_string())?;
            let deform_layer = r.read_i32().map_err(|e| e.to_string())?;
            let flags = r.read_u16().map_err(|e| e.to_string())?;

            let connection_target = if (flags & 0x0001) != 0 {
                Ok(r.read_bone_index().map_err(|e| e.to_string())?)
            } else {
                Err(r.read_vec3().map_err(|e| e.to_string())?)
            };

            let append_parent = if (flags & (0x0100 | 0x0200)) != 0 {
                let p = r.read_bone_index().map_err(|e| e.to_string())?;
                let ratio = r.read_f32().map_err(|e| e.to_string())?;
                Some((p, ratio))
            } else {
                None
            };

            let fixed_axis = if (flags & 0x0400) != 0 {
                Some(r.read_vec3().map_err(|e| e.to_string())?)
            } else {
                None
            };

            let local_axis = if (flags & 0x0800) != 0 {
                let x = r.read_vec3().map_err(|e| e.to_string())?;
                let z = r.read_vec3().map_err(|e| e.to_string())?;
                Some((x, z))
            } else {
                None
            };

            let external_parent_key = if (flags & 0x2000) != 0 {
                Some(r.read_i32().map_err(|e| e.to_string())?)
            } else {
                None
            };

            let ik = if (flags & 0x0020) != 0 {
                let target_bone = r.read_bone_index().map_err(|e| e.to_string())?;
                let loop_count = r.read_i32().map_err(|e| e.to_string())?;
                let angle_limit = r.read_f32().map_err(|e| e.to_string())?;
                let link_count = r.read_i32().map_err(|e| e.to_string())?;
                let mut links = Vec::with_capacity(link_count.max(0) as usize);
                for _ in 0..link_count {
                    let b = r.read_bone_index().map_err(|e| e.to_string())?;
                    let has_limits = r.read_u8().map_err(|e| e.to_string())? != 0;
                    let limits = if has_limits {
                        let min = r.read_vec3().map_err(|e| e.to_string())?;
                        let max = r.read_vec3().map_err(|e| e.to_string())?;
                        Some((min, max))
                    } else {
                        None
                    };
                    links.push(PmxIkLink {
                        bone_index: b,
                        limits,
                    });
                }
                Some(PmxIk {
                    target_bone,
                    loop_count,
                    angle_limit,
                    links,
                })
            } else {
                None
            };

            bones.push(PmxBone {
                name_local,
                name_universal,
                position,
                parent_index,
                deform_layer,
                flags,
                connection_target,
                append_parent,
                fixed_axis,
                local_axis,
                external_parent_key,
                ik,
            });
        }

        // Morphs
        let morph_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut morphs = Vec::with_capacity(morph_count.max(0) as usize);
        for _ in 0..morph_count {
            let name_local = r.read_string().map_err(|e| e.to_string())?;
            let name_universal = r.read_string().map_err(|e| e.to_string())?;
            let category = r.read_u8().map_err(|e| e.to_string())?;
            let morph_type = r.read_u8().map_err(|e| e.to_string())?;
            let count = r.read_i32().map_err(|e| e.to_string())?;

            let data = match morph_type {
                0 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_morph_index().map_err(|e| e.to_string())?;
                        let w = r.read_f32().map_err(|e| e.to_string())?;
                        list.push((idx, w));
                    }
                    PmxMorphData::Group(list)
                }
                1 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_vertex_index().map_err(|e| e.to_string())?;
                        let off = r.read_vec3().map_err(|e| e.to_string())?;
                        list.push((idx, off));
                    }
                    PmxMorphData::Vertex(list)
                }
                2 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_bone_index().map_err(|e| e.to_string())?;
                        let t = r.read_vec3().map_err(|e| e.to_string())?;
                        let rot = r.read_vec4().map_err(|e| e.to_string())?;
                        list.push((idx, t, rot));
                    }
                    PmxMorphData::Bone(list)
                }
                3 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_vertex_index().map_err(|e| e.to_string())?;
                        let off = r.read_vec4().map_err(|e| e.to_string())?;
                        list.push((idx, off));
                    }
                    PmxMorphData::Uv(list)
                }
                4..=7 => {
                    let extra_idx = morph_type - 4;
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_vertex_index().map_err(|e| e.to_string())?;
                        let off = r.read_vec4().map_err(|e| e.to_string())?;
                        list.push((idx, off));
                    }
                    PmxMorphData::ExtraUv(extra_idx, list)
                }
                8 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let material_index = r.read_material_index().map_err(|e| e.to_string())?;
                        let calc_mode = r.read_u8().map_err(|e| e.to_string())?;
                        let diffuse = r.read_vec4().map_err(|e| e.to_string())?;
                        let specular = r.read_vec3().map_err(|e| e.to_string())?;
                        let shininess = r.read_f32().map_err(|e| e.to_string())?;
                        let ambient = r.read_vec3().map_err(|e| e.to_string())?;
                        let edge_color = r.read_vec4().map_err(|e| e.to_string())?;
                        let edge_size = r.read_f32().map_err(|e| e.to_string())?;
                        let texture_tint = r.read_vec4().map_err(|e| e.to_string())?;
                        let sphere_tint = r.read_vec4().map_err(|e| e.to_string())?;
                        let toon_tint = r.read_vec4().map_err(|e| e.to_string())?;

                        list.push(PmxMaterialMorph {
                            material_index,
                            calc_mode,
                            diffuse,
                            specular,
                            shininess,
                            ambient,
                            edge_color,
                            edge_size,
                            texture_tint,
                            sphere_tint,
                            toon_tint,
                        });
                    }
                    PmxMorphData::Material(list)
                }
                9 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_morph_index().map_err(|e| e.to_string())?;
                        let w = r.read_f32().map_err(|e| e.to_string())?;
                        list.push((idx, w));
                    }
                    PmxMorphData::Flip(list)
                }
                10 => {
                    let mut list = Vec::with_capacity(count.max(0) as usize);
                    for _ in 0..count {
                        let idx = r.read_rigid_body_index().map_err(|e| e.to_string())?;
                        let local = r.read_u8().map_err(|e| e.to_string())?;
                        let vel = r.read_vec3().map_err(|e| e.to_string())?;
                        let torque = r.read_vec3().map_err(|e| e.to_string())?;
                        list.push((idx, local, vel, torque));
                    }
                    PmxMorphData::Impulse(list)
                }
                _ => return Err(format!("Unknown morph type: {morph_type}")),
            };

            morphs.push(PmxMorph {
                name_local,
                name_universal,
                category,
                data,
            });
        }

        // Display Frames
        let frame_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut display_frames = Vec::with_capacity(frame_count.max(0) as usize);
        for _ in 0..frame_count {
            let name_local = r.read_string().map_err(|e| e.to_string())?;
            let name_universal = r.read_string().map_err(|e| e.to_string())?;
            let is_special = r.read_u8().map_err(|e| e.to_string())? != 0;
            let count = r.read_i32().map_err(|e| e.to_string())?;
            let mut items = Vec::with_capacity(count.max(0) as usize);
            for _ in 0..count {
                let is_morph = r.read_u8().map_err(|e| e.to_string())? != 0;
                let index = if is_morph {
                    r.read_morph_index().map_err(|e| e.to_string())?
                } else {
                    r.read_bone_index().map_err(|e| e.to_string())?
                };
                items.push(PmxDisplayFrameItem { is_morph, index });
            }
            display_frames.push(PmxDisplayFrame {
                name_local,
                name_universal,
                is_special,
                items,
            });
        }

        // Rigid Bodies
        let body_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut rigid_bodies = Vec::with_capacity(body_count.max(0) as usize);
        for _ in 0..body_count {
            let name_local = r.read_string().map_err(|e| e.to_string())?;
            let name_universal = r.read_string().map_err(|e| e.to_string())?;
            let bone_index = r.read_bone_index().map_err(|e| e.to_string())?;
            let collision_group = r.read_u8().map_err(|e| e.to_string())?;
            let collision_mask = r.read_u16().map_err(|e| e.to_string())?;
            let shape = r.read_u8().map_err(|e| e.to_string())?;
            let size = r.read_vec3().map_err(|e| e.to_string())?;
            let position = r.read_vec3().map_err(|e| e.to_string())?;
            let rotation = r.read_vec3().map_err(|e| e.to_string())?;
            let mass = r.read_f32().map_err(|e| e.to_string())?;
            let move_damping = r.read_f32().map_err(|e| e.to_string())?;
            let rot_damping = r.read_f32().map_err(|e| e.to_string())?;
            let restitution = r.read_f32().map_err(|e| e.to_string())?;
            let friction = r.read_f32().map_err(|e| e.to_string())?;
            let mode = r.read_u8().map_err(|e| e.to_string())?;

            rigid_bodies.push(PmxRigidBody {
                name_local,
                name_universal,
                bone_index,
                collision_group,
                collision_mask,
                shape,
                size,
                position,
                rotation,
                mass,
                move_damping,
                rot_damping,
                restitution,
                friction,
                mode,
            });
        }

        // Joints
        let joint_count = r.read_i32().map_err(|e| e.to_string())?;
        let mut joints = Vec::with_capacity(joint_count.max(0) as usize);
        for _ in 0..joint_count {
            let name_local = r.read_string().map_err(|e| e.to_string())?;
            let name_universal = r.read_string().map_err(|e| e.to_string())?;
            let joint_type = r.read_u8().map_err(|e| e.to_string())?;
            let body_a = r.read_rigid_body_index().map_err(|e| e.to_string())?;
            let body_b = r.read_rigid_body_index().map_err(|e| e.to_string())?;
            let position = r.read_vec3().map_err(|e| e.to_string())?;
            let rotation = r.read_vec3().map_err(|e| e.to_string())?;
            let pos_min = r.read_vec3().map_err(|e| e.to_string())?;
            let pos_max = r.read_vec3().map_err(|e| e.to_string())?;
            let rot_min = r.read_vec3().map_err(|e| e.to_string())?;
            let rot_max = r.read_vec3().map_err(|e| e.to_string())?;
            let spring_pos = r.read_vec3().map_err(|e| e.to_string())?;
            let spring_rot = r.read_vec3().map_err(|e| e.to_string())?;

            joints.push(PmxJoint {
                name_local,
                name_universal,
                joint_type,
                body_a,
                body_b,
                position,
                rotation,
                pos_min,
                pos_max,
                rot_min,
                rot_max,
                spring_pos,
                spring_rot,
            });
        }

        Ok(Self {
            version,
            header,
            info,
            vertices,
            indices,
            textures,
            materials,
            bones,
            morphs,
            display_frames,
            rigid_bodies,
            joints,
        })
    }
}
