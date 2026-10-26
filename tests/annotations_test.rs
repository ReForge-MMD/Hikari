use std::ffi::{CStr, CString};
use std::ptr::null_mut;
use hikari::d3d11::D3D11Context;
use hikari::effect::HikariEffect;
use windows::Win32::Foundation::S_OK;

#[test]
fn test_mme_technique_pass_parameter_annotations() {
    let hlsl = r#"
texture2D DepthBuffer : RENDERDEPTHSTENCILTARGET <
    float2 ViewportRatio = {1.0, 1.0};
    string Format = "D24S8";
>;

texture2D SkyBoxTexture <
    string ResourceName = "skydome.hdr";
>;

struct VS_IN {
    float3 pos : POSITION;
};

struct PS_IN {
    float4 pos : SV_POSITION;
};

PS_IN VS_Main(VS_IN input) {
    PS_IN o;
    o.pos = float4(input.pos, 1.0);
    return o;
}

float4 PS_Main(PS_IN input) : SV_Target {
    return float4(1, 0, 0, 1);
}

technique MainTec <
    string MMDPass = "object";
    string InternalFormat = "A16B16G16R16F";
> {
    pass P0 <
        string Script = "RenderColorTarget0=DepthBuffer; Draw=Geometry;";
    > {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader = compile ps_5_0 PS_Main();
    }

    pass PostProcess <
        string Script = "Draw=Buffer;";
    > {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader = compile ps_5_0 PS_Main();
    }
}
"#;

    let d3d11 = D3D11Context::new(windows::Win32::Foundation::HWND(null_mut()), 0, 0)
        .expect("Failed to create D3D11 context");
    let effect_ptr = HikariEffect::new(hlsl, Some("test_mme.fx"), &d3d11).expect("Failed to create HikariEffect");
    assert!(!effect_ptr.is_null());

    unsafe {
        let eff = &*effect_ptr;
        let vtbl = &*eff.vtbl;

        // 1. Verify Technique Annotations
        let c_tech_name = CString::new("MainTec").unwrap();
        let h_tech = (vtbl.GetTechniqueByName)(effect_ptr as *mut _, c_tech_name.as_ptr());
        assert!(!h_tech.is_null(), "MainTec should be found");

        let c_anno_mmdpass = CString::new("MMDPass").unwrap();
        let h_anno_pass = (vtbl.GetAnnotationByName)(effect_ptr as *mut _, h_tech, c_anno_mmdpass.as_ptr());
        assert!(!h_anno_pass.is_null(), "MMDPass annotation should be found on MainTec");

        let mut str_ptr: *const i8 = null_mut();
        let hr = (vtbl.GetString)(effect_ptr as *mut _, h_anno_pass, &mut str_ptr);
        assert_eq!(hr, S_OK);
        assert_eq!(CStr::from_ptr(str_ptr).to_str().unwrap(), "object");

        // 2. Verify Pass Annotations
        let h_p0 = (vtbl.GetPass)(effect_ptr as *mut _, h_tech, 0);
        assert!(!h_p0.is_null(), "Pass P0 should be found");

        let c_anno_script = CString::new("Script").unwrap();
        let h_anno_script = (vtbl.GetAnnotationByName)(effect_ptr as *mut _, h_p0, c_anno_script.as_ptr());
        assert!(!h_anno_script.is_null(), "Script annotation on P0 should be found");

        let mut script_str: *const i8 = null_mut();
        let hr = (vtbl.GetString)(effect_ptr as *mut _, h_anno_script, &mut script_str);
        assert_eq!(hr, S_OK);
        assert_eq!(CStr::from_ptr(script_str).to_str().unwrap(), "RenderColorTarget0=DepthBuffer; Draw=Geometry;");

        // Verify Pass 1 (PostProcess) annotation
        let h_p1 = (vtbl.GetPass)(effect_ptr as *mut _, h_tech, 1);
        let h_anno_p1_script = (vtbl.GetAnnotationByName)(effect_ptr as *mut _, h_p1, c_anno_script.as_ptr());
        assert!(!h_anno_p1_script.is_null());
        let hr = (vtbl.GetString)(effect_ptr as *mut _, h_anno_p1_script, &mut script_str);
        assert_eq!(hr, S_OK);
        assert_eq!(CStr::from_ptr(script_str).to_str().unwrap(), "Draw=Buffer;");

        // 3. Verify Global Variable / Texture Annotations
        let c_depth_name = CString::new("DepthBuffer").unwrap();
        let h_depth_param = (vtbl.GetParameterByName)(effect_ptr as *mut _, null_mut(), c_depth_name.as_ptr());
        assert!(!h_depth_param.is_null(), "DepthBuffer parameter should be found");

        let c_anno_format = CString::new("Format").unwrap();
        let h_anno_format = (vtbl.GetAnnotationByName)(effect_ptr as *mut _, h_depth_param, c_anno_format.as_ptr());
        assert!(!h_anno_format.is_null(), "Format annotation on DepthBuffer should be found");

        let mut format_str: *const i8 = null_mut();
        let hr = (vtbl.GetString)(effect_ptr as *mut _, h_anno_format, &mut format_str);
        assert_eq!(hr, S_OK);
        assert_eq!(CStr::from_ptr(format_str).to_str().unwrap(), "D24S8");

        // Verify SkyBoxTexture ResourceName annotation
        let c_sky_name = CString::new("SkyBoxTexture").unwrap();
        let h_sky_param = (vtbl.GetParameterByName)(effect_ptr as *mut _, null_mut(), c_sky_name.as_ptr());
        assert!(!h_sky_param.is_null());

        let c_anno_res = CString::new("ResourceName").unwrap();
        let h_anno_res = (vtbl.GetAnnotationByName)(effect_ptr as *mut _, h_sky_param, c_anno_res.as_ptr());
        assert!(!h_anno_res.is_null(), "ResourceName annotation on SkyBoxTexture should be found");

        let mut res_str: *const i8 = null_mut();
        let hr = (vtbl.GetString)(effect_ptr as *mut _, h_anno_res, &mut res_str);
        assert_eq!(hr, S_OK);
        assert_eq!(CStr::from_ptr(res_str).to_str().unwrap(), "skydome.hdr");

        (vtbl.Release)(effect_ptr as *mut _);
    }
}
