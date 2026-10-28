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
}

#[test]
fn test_verify_d3d9_vtable_layout() {
    use windows::Win32::Graphics::Direct3D9::IDirect3DDevice9_Vtbl;
    let size = std::mem::size_of::<IDirect3DDevice9_Vtbl>();
    let slot_count = size / std::mem::size_of::<usize>();
    assert_eq!(slot_count, 119);

    macro_rules! check_slot {
        ($field:ident, $expected:expr) => {
            let offset = std::mem::offset_of!(IDirect3DDevice9_Vtbl, $field) / std::mem::size_of::<usize>();
            assert_eq!(offset, $expected, "Field {} at slot {} expected {}", stringify!($field), offset, $expected);
        };
    }

    check_slot!(base__, 0);
    check_slot!(Present, 17);
    check_slot!(CreateTexture, 23);
    check_slot!(CreateVertexBuffer, 26);
    check_slot!(CreateIndexBuffer, 27);
    check_slot!(SetRenderTarget, 37);
    check_slot!(BeginScene, 41);
    check_slot!(EndScene, 42);
    check_slot!(Clear, 43);
    check_slot!(SetViewport, 47);
    check_slot!(SetRenderState, 57);
    check_slot!(GetRenderState, 58);
    check_slot!(GetTexture, 64);
    check_slot!(SetTexture, 65);
    check_slot!(GetTextureStageState, 66);
    check_slot!(SetTextureStageState, 67);
    check_slot!(GetSamplerState, 68);
    check_slot!(SetSamplerState, 69);
    check_slot!(DrawPrimitive, 81);
    check_slot!(DrawIndexedPrimitive, 82);
    check_slot!(DrawPrimitiveUP, 83);
    check_slot!(SetFVF, 89);
    check_slot!(GetFVF, 90);
    check_slot!(SetVertexShader, 92);
    check_slot!(GetVertexShader, 93);
    check_slot!(SetStreamSource, 100);
    check_slot!(SetIndices, 104);
    check_slot!(SetPixelShader, 107);
    check_slot!(GetPixelShader, 108);
}

