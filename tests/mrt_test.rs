use hikari::d3d11::D3D11Context;
use hikari::targets::RenderTargetManager;
use windows::Win32::Foundation::HWND;
use windows::Win32::Graphics::Dxgi::Common::*;

#[test]
fn test_mrt_render_target_manager() {
    let ctx = D3D11Context::new(HWND(std::ptr::null_mut()), 0, 0)
        .expect("Failed to create headless D3D11 context");

    let mut mgr = RenderTargetManager::new();

    // 1. Create 4 MRT G-Buffer targets (Albedo, Normal, Material, LightAcc)
    mgr.create_target(&ctx.device, "GBuffer_Albedo", 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, false)
        .expect("Failed to create albedo target");

    mgr.create_target(&ctx.device, "GBuffer_Normal", 512, 512, DXGI_FORMAT_R16G16B16A16_FLOAT, false)
        .expect("Failed to create normal target");

    mgr.create_target(&ctx.device, "GBuffer_Material", 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, false)
        .expect("Failed to create material target");

    mgr.create_target(&ctx.device, "Light_Accumulation", 512, 512, DXGI_FORMAT_R16G16B16A16_FLOAT, true)
        .expect("Failed to create light accumulation target");

    // 2. Create Depth Stencil Target
    mgr.create_depth(&ctx.device, "MainDepth", 512, 512)
        .expect("Failed to create depth target");

    // Verify properties
    let albedo = mgr.get_target("GBuffer_Albedo").expect("Albedo not found");
    assert_eq!(albedo.width, 512);
    assert_eq!(albedo.height, 512);

    let normal = mgr.get_target("GBuffer_Normal").expect("Normal not found");
    assert_eq!(normal.format, DXGI_FORMAT_R16G16B16A16_FLOAT);

    let light_acc = mgr.get_target("Light_Accumulation").expect("Light accumulation not found");
    assert!(light_acc.uav.is_some(), "Light accumulation target should have a valid UAV");

    let depth = mgr.get_depth("MainDepth").expect("Depth not found");
    assert_eq!(depth.width, 512);
    assert!(depth.srv.is_some(), "Depth target should support SRV shader reading");

    // 3. Bind 4 MRT targets + Depth Stencil
    let rtvs = [
        mgr.get_rtv("GBuffer_Albedo"),
        mgr.get_rtv("GBuffer_Normal"),
        mgr.get_rtv("GBuffer_Material"),
        mgr.get_rtv("Light_Accumulation"),
    ];
    let dsv = mgr.get_dsv("MainDepth");
    mgr.bind_mrt(&ctx.context, &rtvs, dsv.as_ref());

    // 4. Clear MRT targets
    mgr.clear_render_target(&ctx.context, 0, [0.1, 0.2, 0.3, 1.0]);
    mgr.clear_render_target(&ctx.context, 1, [0.0, 1.0, 0.0, 0.0]);
    mgr.clear_depth_stencil(&ctx.context, true, 1.0, true, 0);

    // 5. Create Structured Buffer with UAV
    mgr.create_structured_buffer(&ctx.device, "ParticleBuffer", 32, 1024, true)
        .expect("Failed to create structured buffer");
    let sbuf = mgr.get_buffer("ParticleBuffer").expect("ParticleBuffer not found");
    assert_eq!(sbuf.element_size, 32);
    assert_eq!(sbuf.element_count, 1024);
    assert!(sbuf.uav.is_some());
}
