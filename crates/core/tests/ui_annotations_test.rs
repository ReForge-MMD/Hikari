use std::ptr::null_mut;
use hikari_core::d3d11::D3D11Context;
use hikari_core::effect::{HikariEffect, UIWidgetKind};
use windows::Win32::Foundation::HWND;

#[test]
fn test_shader_ui_annotations_and_defaults() {
    let hlsl = r#"
float Brightness <
    string UIName = "Яркость эффекта";
    string UIWidget = "Slider";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.05;
    string UIGroup = "Освещение";
    string UIHelp = "Регулирует общую яркость модели";
> = 1.25;

float3 DiffuseColor <
    string UIName = "Цвет рассеивания";
    string UIWidget = "Color";
> = float3(1.0, 0.5, 0.25);

bool EnableEdge <
    string UIName = "Контур";
    string UIWidget = "Checkbox";
> = true;

int QualityLevel <
    string UIName = "Уровень качества";
    string UIWidget = "Combo";
    string UIItems = "Низкое,Среднее,Высокое,Ультра";
> = 2;

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
    float3 col = DiffuseColor * Brightness;
    if (!EnableEdge) {
        col *= 0.5;
    }
    return float4(col, 1.0);
}

technique MainTec {
    pass P0 {
        VertexShader = compile vs_5_0 VS_Main();
        PixelShader = compile ps_5_0 PS_Main();
    }
}
"#;

    let d3d11 = D3D11Context::new(HWND(null_mut()), 0, 0)
        .expect("Failed to create D3D11 context");
    let effect_ptr = HikariEffect::new(hlsl, Some("test_ui.fx"), &d3d11)
        .expect("Failed to create HikariEffect with UI annotations");
    assert!(!effect_ptr.is_null());

    let eff = unsafe { &mut *effect_ptr };

    // 1. Verify Brightness parameter UI description & default value
    let b_idx = *eff.param_by_name.get("brightness").expect("Brightness param not found");
    let b_param = &eff.parameters[b_idx];
    let b_ui = b_param.ui.as_ref().expect("Brightness should have UI metadata");
    assert_eq!(b_ui.label, "Яркость эффекта");
    assert_eq!(b_ui.widget, UIWidgetKind::Slider);
    assert_eq!(b_ui.min, 0.0);
    assert_eq!(b_ui.max, 2.0);
    assert_eq!(b_ui.step, 0.05);
    assert_eq!(b_ui.group, "Освещение");
    assert_eq!(b_ui.help, "Регулирует общую яркость модели");
    assert_eq!(b_ui.default_val, "1.25");

    let b_val = eff.get_param_value_f32(b_idx).expect("Read Brightness f32");
    assert!((b_val - 1.25).abs() < 1e-4, "Initial brightness must match default (1.25), got {b_val}");

    // Test live tweaking of Brightness
    eff.set_param_value_f32(b_idx, 0.75);
    let b_val_tweaked = eff.get_param_value_f32(b_idx).unwrap();
    assert!((b_val_tweaked - 0.75).abs() < 1e-4);

    // 2. Verify DiffuseColor parameter UI description & default value
    let c_idx = *eff.param_by_name.get("diffusecolor").expect("DiffuseColor param not found");
    let c_param = &eff.parameters[c_idx];
    let c_ui = c_param.ui.as_ref().expect("DiffuseColor should have UI metadata");
    assert_eq!(c_ui.label, "Цвет рассеивания");
    assert_eq!(c_ui.widget, UIWidgetKind::Color);

    let c_val = eff.get_param_value_float3(c_idx).expect("Read DiffuseColor float3");
    assert!((c_val[0] - 1.0).abs() < 1e-4);
    assert!((c_val[1] - 0.5).abs() < 1e-4);
    assert!((c_val[2] - 0.25).abs() < 1e-4);

    // Test live tweaking of Color
    eff.set_param_value_float3(c_idx, [0.1, 0.2, 0.3]);
    let c_val_tweaked = eff.get_param_value_float3(c_idx).unwrap();
    assert!((c_val_tweaked[0] - 0.1).abs() < 1e-4);
    assert!((c_val_tweaked[1] - 0.2).abs() < 1e-4);
    assert!((c_val_tweaked[2] - 0.3).abs() < 1e-4);

    // 3. Verify EnableEdge parameter UI description & default value
    let e_idx = *eff.param_by_name.get("enableedge").expect("EnableEdge param not found");
    let e_param = &eff.parameters[e_idx];
    let e_ui = e_param.ui.as_ref().expect("EnableEdge should have UI metadata");
    assert_eq!(e_ui.label, "Контур");
    assert_eq!(e_ui.widget, UIWidgetKind::Checkbox);

    let is_edge = eff.get_param_value_bool(e_idx).expect("Read EnableEdge value");
    assert!(is_edge, "Default bool should be true");

    eff.set_param_value_bool(e_idx, false);
    assert!(!eff.get_param_value_bool(e_idx).unwrap(), "Tweaked bool should be false");

    // 4. Verify QualityLevel parameter UI description & default value
    let q_idx = *eff.param_by_name.get("qualitylevel").expect("QualityLevel param not found");
    let q_param = &eff.parameters[q_idx];
    let q_ui = q_param.ui.as_ref().expect("QualityLevel should have UI metadata");
    assert_eq!(q_ui.label, "Уровень качества");
    assert_eq!(q_ui.widget, UIWidgetKind::Combo);
    assert_eq!(q_ui.items, vec!["Низкое", "Среднее", "Высокое", "Ультра"]);

    let q_val = eff.get_param_value_i32(q_idx).expect("Read QualityLevel value");
    assert_eq!(q_val, 2, "Default combo index should be 2, got {q_val}");

    // 5. Verify pass info generation for direct execution
    let pass_info = eff.get_first_pass_info().expect("Must return compiled active pass info");
    assert!(pass_info.vs.is_some());
    assert!(pass_info.ps.is_some());
    assert!(pass_info.constant_buffer.is_some());

    unsafe {
        drop(Box::from_raw(effect_ptr));
    }
}
