use hikari_core::d3d11::D3D11Context;
use windows::Win32::Foundation::HWND;
use windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_R8G8B8A8_UNORM;

#[test]
fn test_create_headless_d3d11_context() {
    // HWND(0) creates headless device without swap chain
    let ctx = D3D11Context::new(HWND(std::ptr::null_mut()), 0, 0);
    assert!(ctx.is_ok(), "Failed to create D3D11 context: {:?}", ctx.err());
    let ctx = ctx.unwrap();

    // Verify Constant Buffer creation (16-byte alignment)
    let cb = ctx.create_constant_buffer(64);
    assert!(cb.is_ok(), "Failed to create constant buffer");

    // Verify Structured Buffer creation for compute operations
    let sb = ctx.create_structured_buffer(16, 256, true);
    assert!(sb.is_ok(), "Failed to create structured buffer with UAV");
    let (_buffer, srv, uav) = sb.unwrap();
    assert!(srv.is_some(), "Structured buffer should have SRV");
    assert!(uav.is_some(), "Structured buffer should have UAV");

    // Verify 2D Texture creation with UAV
    let tex = ctx.create_texture2d(256, 256, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    assert!(tex.is_ok(), "Failed to create RWTexture2D");
    let (_texture, srv, uav) = tex.unwrap();
    assert!(srv.is_some(), "Texture should have SRV");
    assert!(uav.is_some(), "Texture should have UAV");

    // Verify Direct3D 11 threading capabilities query
    let caps = ctx.check_threading_support();
    // Hardware or WARP driver will report valid booleans
    println!("D3D11 Threading Caps: concurrent_creates={}, command_lists={}", caps.driver_concurrent_creates, caps.driver_command_lists);

    // Verify Deferred Context acquisition from pool and command list execution
    let deferred = ctx.acquire_deferred_context();
    assert!(deferred.is_ok(), "Failed to acquire deferred context from pool");
    let deferred = deferred.unwrap();

    // Record simple state change on deferred context
    unsafe {
        deferred.RSSetState(Some(&ctx.raster_solid_cull_none));
    }

    // Finish command list
    let cl = D3D11Context::finish_command_list(&deferred, false);
    assert!(cl.is_ok(), "Failed to finish command list on deferred context");
    let cl = cl.unwrap();

    // Recycle deferred context back to pool
    ctx.recycle_deferred_context(deferred);

    // Execute recorded command list on immediate context
    ctx.execute_command_list(&cl, false);

    // Acquire again from pool (should reuse recycled context)
    let deferred_reused = ctx.acquire_deferred_context().unwrap();
    ctx.recycle_deferred_context(deferred_reused);
}
