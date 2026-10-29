use hikari_formats::{PmxModel, VmdMotion, VpdPose};

#[test]
fn test_parse_vpd_pose() {
    let vpd_content = r#"Vocaloid Pose Data file

ModelName.osm;
2;

Bone0{Center
  0.000000, 1.250000, 0.500000;
  0.000000, 0.000000, 0.000000, 1.000000;
}

Morph0{Blink
  0.750000;
}
"#;

    let pose = VpdPose::parse(vpd_content).expect("Failed to parse valid VPD");
    assert_eq!(pose.model_name, "ModelName.osm");
    assert_eq!(pose.bones.len(), 1);
    assert_eq!(pose.bones[0].name, "Center");
    assert_eq!(pose.bones[0].translation, [0.0, 1.25, 0.5]);
    assert_eq!(pose.bones[0].rotation, [0.0, 0.0, 0.0, 1.0]);

    assert_eq!(pose.morphs.len(), 1);
    assert_eq!(pose.morphs[0].name, "Blink");
    assert_eq!(pose.morphs[0].weight, 0.75);
}

#[test]
fn test_parse_vmd_synthetic() {
    let mut data = Vec::new();
    // 30 bytes header
    let header_str = b"Vocaloid Motion Data 0002\0\0\0\0\0";
    data.extend_from_slice(header_str);
    // 20 bytes model name
    data.extend_from_slice(&[0u8; 20]);
    // Bone count = 1
    data.extend_from_slice(&1u32.to_le_bytes());
    // Bone name: 15 bytes
    let mut name = [0u8; 15];
    name[0..4].copy_from_slice(b"Root");
    data.extend_from_slice(&name);
    // Frame = 10
    data.extend_from_slice(&10u32.to_le_bytes());
    // Pos = [1.0, 2.0, 3.0]
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&2.0f32.to_le_bytes());
    data.extend_from_slice(&3.0f32.to_le_bytes());
    // Rot = [0.0, 0.0, 0.0, 1.0]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    // Interpolation = 64 bytes
    data.extend_from_slice(&[20u8; 64]);

    // Morph count = 0
    data.extend_from_slice(&0u32.to_le_bytes());
    // Camera count = 0
    data.extend_from_slice(&0u32.to_le_bytes());
    // Light count = 0
    data.extend_from_slice(&0u32.to_le_bytes());
    // Shadow count = 0
    data.extend_from_slice(&0u32.to_le_bytes());
    // IK count = 0
    data.extend_from_slice(&0u32.to_le_bytes());

    let vmd = VmdMotion::parse(&data).expect("VMD parse failed");
    assert_eq!(vmd.bone_frames.len(), 1);
    assert_eq!(vmd.bone_frames[0].name, "Root");
    assert_eq!(vmd.bone_frames[0].frame, 10);
    assert_eq!(vmd.bone_frames[0].position, [1.0, 2.0, 3.0]);
    assert_eq!(vmd.bone_frames[0].rotation, [0.0, 0.0, 0.0, 1.0]);
    assert_eq!(vmd.bone_frames[0].interpolation[0], 20);
}

#[test]
fn test_parse_pmx_synthetic() {
    let mut data = Vec::new();
    // Magic: "PMX "
    data.extend_from_slice(b"PMX ");
    // Version: 2.0
    data.extend_from_slice(&2.0f32.to_le_bytes());
    // Globals count: 8
    data.push(8);
    // Globals: [utf8=1, add_vec4=0, v_idx=1, tex_idx=1, mat_idx=1, bone_idx=1, morph_idx=1, rb_idx=1]
    data.extend_from_slice(&[1, 0, 1, 1, 1, 1, 1, 1]);

    let push_str = |d: &mut Vec<u8>, s: &str| {
        let b = s.as_bytes();
        d.extend_from_slice(&(b.len() as i32).to_le_bytes());
        d.extend_from_slice(b);
    };

    // Model info
    push_str(&mut data, "TestModel");
    push_str(&mut data, "TestModel_EN");
    push_str(&mut data, "Comment");
    push_str(&mut data, "Comment_EN");

    // Vertex count = 1
    data.extend_from_slice(&1i32.to_le_bytes());
    // Pos = [0.0, 1.0, 2.0]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&2.0f32.to_le_bytes());
    // Normal = [0.0, 1.0, 0.0]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    // UV = [0.5, 0.5]
    data.extend_from_slice(&0.5f32.to_le_bytes());
    data.extend_from_slice(&0.5f32.to_le_bytes());
    // Weight type: 0 (BDEF1), bone index = 0
    data.push(0);
    data.push(0);
    // Edge scale = 1.0
    data.extend_from_slice(&1.0f32.to_le_bytes());

    // Indices count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Textures count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Materials count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Bones count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Morphs count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Display frames count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Rigid bodies count = 0
    data.extend_from_slice(&0i32.to_le_bytes());
    // Joints count = 0
    data.extend_from_slice(&0i32.to_le_bytes());

    let pmx = PmxModel::parse(&data).expect("PMX parse failed");
    assert_eq!(pmx.info.name_local, "TestModel");
    assert_eq!(pmx.info.name_universal, "TestModel_EN");
    assert_eq!(pmx.vertices.len(), 1);
    assert_eq!(pmx.vertices[0].position, [0.0, 1.0, 2.0]);
    assert_eq!(pmx.vertices[0].normal, [0.0, 1.0, 0.0]);
    assert_eq!(pmx.vertices[0].uv, [0.5, 0.5]);
}
