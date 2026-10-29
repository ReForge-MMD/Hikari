use hikari_core::d3d11::D3D11Context;
use hikari_formats::PmxModel;
use hikari_render::{Camera, GpuMaterial, GpuMesh, SceneRenderer};
use std::ptr::null_mut;
use windows::Win32::Foundation::HWND;

#[test]
fn test_render_pipeline_creation_and_mesh_upload() {
    let ctx = D3D11Context::new(HWND(null_mut()), 0, 0)
        .expect("Failed to create headless D3D11Context");

    let renderer = SceneRenderer::new(&ctx)
        .expect("Failed to create SceneRenderer with SM5 shaders");

    let camera = Camera::default();
    assert!(renderer.update_camera(&ctx, &camera).is_ok());

    // Create a minimal synthetic PMX model
    let mut data = Vec::new();
    data.extend_from_slice(b"PMX ");
    data.extend_from_slice(&2.0f32.to_le_bytes());
    data.push(8);
    data.extend_from_slice(&[1, 0, 1, 1, 1, 1, 1, 1]);

    let push_str = |d: &mut Vec<u8>, s: &str| {
        let b = s.as_bytes();
        d.extend_from_slice(&(b.len() as i32).to_le_bytes());
        d.extend_from_slice(b);
    };

    push_str(&mut data, "Test");
    push_str(&mut data, "Test_EN");
    push_str(&mut data, "");
    push_str(&mut data, "");

    // 3 Vertices (1 triangle)
    data.extend_from_slice(&3i32.to_le_bytes());
    for i in 0..3 {
        data.extend_from_slice(&(i as f32).to_le_bytes());
        data.extend_from_slice(&0.0f32.to_le_bytes());
        data.extend_from_slice(&0.0f32.to_le_bytes());
        data.extend_from_slice(&0.0f32.to_le_bytes());
        data.extend_from_slice(&1.0f32.to_le_bytes());
        data.extend_from_slice(&0.0f32.to_le_bytes());
        data.extend_from_slice(&0.0f32.to_le_bytes());
        data.extend_from_slice(&0.0f32.to_le_bytes());
        data.push(0); // weight type = BDEF1
        data.push(0); // bone = 0
        data.extend_from_slice(&1.0f32.to_le_bytes()); // edge
    }

    // Indices: 0, 1, 2
    data.extend_from_slice(&3i32.to_le_bytes());
    data.push(0);
    data.push(1);
    data.push(2);

    // Textures: 0
    data.extend_from_slice(&0i32.to_le_bytes());

    // Materials: 1
    data.extend_from_slice(&1i32.to_le_bytes());
    push_str(&mut data, "Mat1");
    push_str(&mut data, "Mat1_EN");
    // Diffuse [1, 1, 1, 1]
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    // Specular [0, 0, 0]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&5.0f32.to_le_bytes()); // shininess
    // Ambient [0.2, 0.2, 0.2]
    data.extend_from_slice(&0.2f32.to_le_bytes());
    data.extend_from_slice(&0.2f32.to_le_bytes());
    data.extend_from_slice(&0.2f32.to_le_bytes());
    data.push(0); // flags
    // Edge color [0, 0, 0, 1]
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&0.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes());
    data.extend_from_slice(&1.0f32.to_le_bytes()); // edge size
    data.push(255); // tex idx = -1
    data.push(255); // sphere idx = -1
    data.push(0); // sphere mode
    data.push(0); // toon flag
    data.push(255); // toon idx
    push_str(&mut data, ""); // memo
    data.extend_from_slice(&3i32.to_le_bytes()); // surface count = 3

    // 0 bones, 0 morphs, 0 display, 0 bodies, 0 joints
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());
    data.extend_from_slice(&0i32.to_le_bytes());

    let pmx = PmxModel::parse(&data).expect("Failed to parse PMX");

    let gpu_mesh = GpuMesh::from_pmx(&ctx, &pmx).expect("Failed to create GpuMesh");
    assert_eq!(gpu_mesh.vertex_count, 3);
    assert_eq!(gpu_mesh.index_count, 3);
    assert_eq!(gpu_mesh.subsets.len(), 1);

    let gpu_mat = GpuMaterial::from_pmx_material(&ctx, &pmx.materials[0])
        .expect("Failed to create GpuMaterial");
    assert_eq!(gpu_mat.name, "Mat1");
}
