use hikari_core::d3d11::D3D11Context;
use hikari_core::states::{BlendPreset, DepthPreset, RasterizerPreset, SamplerPreset, StateManager};
use windows::Win32::Foundation::HWND;

#[test]
fn test_pipeline_states_creation_and_caching() {
    let ctx = D3D11Context::new(HWND(std::ptr::null_mut()), 0, 0)
        .expect("Failed to create headless D3D11 context");

    let mgr = StateManager::new(&ctx.device).expect("Failed to create StateManager");

    // Test blend states
    assert!(mgr.get_blend(BlendPreset::Opaque).is_some());
    assert!(mgr.get_blend(BlendPreset::AlphaBlend).is_some());
    assert!(mgr.get_blend(BlendPreset::Additive).is_some());
    assert!(mgr.get_blend(BlendPreset::Subtractive).is_some());
    assert!(mgr.get_blend(BlendPreset::Multiply).is_some());

    // Test depth-stencil states
    assert!(mgr.get_depth(DepthPreset::ReadWriteLessEqual).is_some());
    assert!(mgr.get_depth(DepthPreset::ReadOnlyLessEqual).is_some());
    assert!(mgr.get_depth(DepthPreset::Disabled).is_some());
    assert!(mgr.get_depth(DepthPreset::ShadowComparison).is_some());

    // Test rasterizer states
    assert!(mgr.get_rasterizer(RasterizerPreset::CullNoneSolid).is_some());
    assert!(mgr.get_rasterizer(RasterizerPreset::CullBackSolid).is_some());
    assert!(mgr.get_rasterizer(RasterizerPreset::CullFrontSolid).is_some());
    assert!(mgr.get_rasterizer(RasterizerPreset::Wireframe).is_some());

    // Test sampler states
    assert!(mgr.get_sampler(SamplerPreset::LinearWrap).is_some());
    assert!(mgr.get_sampler(SamplerPreset::LinearClamp).is_some());
    assert!(mgr.get_sampler(SamplerPreset::PointClamp).is_some());
    assert!(mgr.get_sampler(SamplerPreset::PointWrap).is_some());
    assert!(mgr.get_sampler(SamplerPreset::Anisotropic16Wrap).is_some());
    assert!(mgr.get_sampler(SamplerPreset::ComparisonLinearClamp).is_some());
}
