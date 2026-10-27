use std::ffi::CString;
use std::ptr::null_mut;
use hikari_core::d3d11::D3D11Context;
use hikari_core::effect::HikariEffect;
use windows::Win32::Foundation::S_OK;

#[test]
fn test_cbuffer_parameter_reflection_and_updates() {
    let hlsl = r#"
cbuffer ParamBuffer : register(b0) {
    row_major float4x4 WorldViewProj : WORLDVIEWPROJECTION;
    float Exposure;
    float BloomThreshold;
    float3 LightDir : DIRECTION;
    float4 FogColor;
};

struct VS_IN {
    float3 pos : POSITION;
};

struct PS_IN {
    float4 pos : SV_POSITION;
};

PS_IN VS_Main(VS_IN input) {
    PS_IN o;
    o.pos = mul(float4(input.pos, 1.0), WorldViewProj);
    return o;
}

float4 PS_Main(PS_IN input) : SV_Target {
    return FogColor * Exposure + float4(LightDir, BloomThreshold);
}

technique MainTec {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader = compile ps_5_0 PS_Main();
    }
}
"#;

    let d3d11 = D3D11Context::new(windows::Win32::Foundation::HWND(null_mut()), 0, 0)
        .expect("Failed to create D3D11 context");
    let effect_ptr = HikariEffect::new(hlsl, Some("test_params.fx"), &d3d11)
        .expect("Failed to create HikariEffect with parameter reflection");
    assert!(!effect_ptr.is_null());

    unsafe {
        let eff = &*effect_ptr;
        let vtbl = &*eff.vtbl;

        // 1. Verify Parameter Handles by Name
        let c_exposure = CString::new("Exposure").unwrap();
        let h_exposure = (vtbl.GetParameterByName)(effect_ptr as *mut _, null_mut(), c_exposure.as_ptr());
        assert!(!h_exposure.is_null(), "Parameter 'Exposure' should be found");

        let c_bloom = CString::new("BloomThreshold").unwrap();
        let h_bloom = (vtbl.GetParameterByName)(effect_ptr as *mut _, null_mut(), c_bloom.as_ptr());
        assert!(!h_bloom.is_null(), "Parameter 'BloomThreshold' should be found");

        let c_fog = CString::new("FogColor").unwrap();
        let h_fog = (vtbl.GetParameterByName)(effect_ptr as *mut _, null_mut(), c_fog.as_ptr());
        assert!(!h_fog.is_null(), "Parameter 'FogColor' should be found");

        let c_wvp = CString::new("WorldViewProj").unwrap();
        let h_wvp = (vtbl.GetParameterByName)(effect_ptr as *mut _, null_mut(), c_wvp.as_ptr());
        assert!(!h_wvp.is_null(), "Parameter 'WorldViewProj' should be found");

        // 2. Verify Parameter Handle by Semantic
        let c_sem_wvp = CString::new("WORLDVIEWPROJECTION").unwrap();
        let h_sem_wvp = (vtbl.GetParameterBySemantic)(effect_ptr as *mut _, null_mut(), c_sem_wvp.as_ptr());
        assert_eq!(h_wvp, h_sem_wvp, "GetParameterBySemantic should find WorldViewProj");

        // 3. Test SetFloat & GetFloat
        let hr = (vtbl.SetFloat)(effect_ptr as *mut _, h_exposure, 3.75);
        assert_eq!(hr, S_OK);

        let mut read_float: f32 = 0.0;
        let hr = (vtbl.GetFloat)(effect_ptr as *mut _, h_exposure, &mut read_float);
        assert_eq!(hr, S_OK);
        assert!((read_float - 3.75).abs() < 1e-6, "Exposure should read back 3.75");

        let hr = (vtbl.SetFloat)(effect_ptr as *mut _, h_bloom, 0.85);
        assert_eq!(hr, S_OK);

        let mut read_bloom: f32 = 0.0;
        let hr = (vtbl.GetFloat)(effect_ptr as *mut _, h_bloom, &mut read_bloom);
        assert_eq!(hr, S_OK);
        assert!((read_bloom - 0.85).abs() < 1e-6, "BloomThreshold should read back 0.85");

        // 4. Test SetVector & GetVector
        let in_vector: [f32; 4] = [0.2, 0.4, 0.6, 1.0];
        let hr = (vtbl.SetVector)(effect_ptr as *mut _, h_fog, in_vector.as_ptr() as *const _);
        assert_eq!(hr, S_OK);

        let mut out_vector: [f32; 4] = [0.0; 4];
        let hr = (vtbl.GetVector)(effect_ptr as *mut _, h_fog, out_vector.as_mut_ptr() as *mut _);
        assert_eq!(hr, S_OK);
        assert_eq!(in_vector, out_vector, "FogColor vector should match exactly");

        // 5. Test SetMatrix & GetMatrix
        #[rustfmt::skip]
        let in_matrix: [f32; 16] = [
            1.0, 0.0, 0.0, 0.0,
            0.0, 2.0, 0.0, 0.0,
            0.0, 0.0, 3.0, 0.0,
            4.0, 5.0, 6.0, 1.0,
        ];
        let hr = (vtbl.SetMatrix)(effect_ptr as *mut _, h_wvp, in_matrix.as_ptr() as *const _);
        assert_eq!(hr, S_OK);

        let mut out_matrix: [f32; 16] = [0.0; 16];
        let hr = (vtbl.GetMatrix)(effect_ptr as *mut _, h_wvp, out_matrix.as_mut_ptr() as *mut _);
        assert_eq!(hr, S_OK);
        assert_eq!(in_matrix, out_matrix, "Matrix should match exactly");

        // 6. Test Begin and BeginPass to exercise constant buffer upload
        let mut pass_count: u32 = 0;
        let hr = (vtbl.Begin)(effect_ptr as *mut _, &mut pass_count, 0);
        assert_eq!(hr, S_OK);
        assert_eq!(pass_count, 1);

        let hr = (vtbl.BeginPass)(effect_ptr as *mut _, 0);
        assert_eq!(hr, S_OK);

        let hr = (vtbl.EndPass)(effect_ptr as *mut _);
        assert_eq!(hr, S_OK);

        let hr = (vtbl.End)(effect_ptr as *mut _);
        assert_eq!(hr, S_OK);

        (vtbl.Release)(effect_ptr as *mut _);
    }
}
