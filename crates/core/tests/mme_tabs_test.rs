use std::sync::atomic::Ordering;
use hikari_core::effect::{
    match_mme_pattern, ModelAssignmentRegistry, EFFECTS_ENABLED,
};

#[test]
fn test_mme_wildcard_pattern_matching() {
    assert!(match_mme_pattern("self", "self"));
    assert!(match_mme_pattern("*controller*.pmx", "facial_controller_v2.pmx"));
    assert!(!match_mme_pattern("*controller*.pmx", "miku_normal.pmx"));
    assert!(match_mme_pattern("sky*box*.*", "sky_box_clear.x"));
    assert!(match_mme_pattern("*.pmx", "any_model.pmx"));
    assert!(match_mme_pattern("*", "anything"));
    assert!(match_mme_pattern("model?.pmx", "model1.pmx"));
    assert!(!match_mme_pattern("model?.pmx", "model12.pmx"));
}

#[test]
fn test_mme_offscreen_tabs_and_deferred_material_map() {
    let mut reg = ModelAssignmentRegistry::new();

    // 1. Register offscreen tab for deferred rendering (MaterialMap)
    reg.add_tab(
        "MaterialMap",
        "Material buffer render target for deferred shading",
        "self=hide; *controller*.pmx=hide; sky*box*.*=./materials/material_skybox.fx; *.pmx=./materials/material_2.0.fx; *=hide;",
    );

    assert_eq!(reg.tabs.len(), 2);
    assert_eq!(reg.tabs[0].name, "Main");
    assert_eq!(reg.tabs[1].name, "MaterialMap");
    assert_eq!(reg.tabs[1].description, "Material buffer render target for deferred shading");

    // 2. Resolve effects on MaterialMap tab (tab_idx 1)
    // Model 1: "controller.pmx" -> should be hidden
    let (_, path_ctrl, shown_ctrl) = reg.resolve_tab_effect(1, 100, -1, "facial_controller.pmx");
    assert!(!shown_ctrl, "Controller should be hidden in MaterialMap tab");
    assert_eq!(path_ctrl, "(hide)");

    // Model 2: "sky_box_night.x" -> skybox material
    let (_, path_sky, shown_sky) = reg.resolve_tab_effect(1, 101, -1, "sky_box_night.x");
    assert!(shown_sky);
    assert_eq!(path_sky, "./materials/material_skybox.fx");

    // Model 3: "miku.pmx" -> standard deferred material 2.0
    let (_, path_miku, shown_miku) = reg.resolve_tab_effect(1, 102, -1, "miku_v4.pmx");
    assert!(shown_miku);
    assert_eq!(path_miku, "./materials/material_2.0.fx");

    // 3. User override: toggle visibility for miku
    reg.set_tab_item_shown(1, Some(102), -1, false);
    let (_, _, shown_miku_after) = reg.resolve_tab_effect(1, 102, -1, "miku_v4.pmx");
    assert!(!shown_miku_after, "Miku should now be hidden via user override");

    // 4. Test EMM serialization and deserialization
    let emm_tmp_path = std::env::temp_dir().join("test_hikari_mme.emm");
    let emm_str = emm_tmp_path.to_str().unwrap();

    let save_res = reg.save_emm(emm_str);
    assert!(save_res.is_ok(), "save_emm should succeed: {:?}", save_res.err());

    let saved_content = std::fs::read_to_string(emm_str).unwrap();
    assert!(saved_content.contains("[Object]"));
    assert!(saved_content.contains("[Effect]"));
    assert!(saved_content.contains("[Effect@MaterialMap]"));

    // Load back into a fresh registry
    let mut reg2 = ModelAssignmentRegistry::new();
    let load_res = reg2.load_emm(emm_str);
    assert!(load_res.is_ok(), "load_emm should succeed: {:?}", load_res.err());
    assert_eq!(reg2.tabs.len(), 2);
    assert_eq!(reg2.tabs[0].name, "Main");
    assert_eq!(reg2.tabs[1].name, "MaterialMap");

    let _ = std::fs::remove_file(emm_tmp_path);
}

#[test]
fn test_mme_shader_source_scanning_for_offscreen_tabs() {
    let mut reg = ModelAssignmentRegistry::new();

    let shader_source = r#"
texture2D DepthBuffer : OFFSCREENRENDERTARGET <
    string Description = "Z-Buffer and Depth Stencil render target";
    string DefaultEffect = "*.pmx=./depth.fx; *=hide;";
>;

texture2D NormalMap : OFFSCREENRENDERTARGET <
    string Description = "World Normal and Geometric Specular buffer";
    string DefaultEffect = "*.pmx=./normal.fx; *=hide;";
>;
"#;

    reg.scan_and_register_offscreen_tabs(shader_source);
    assert_eq!(reg.tabs.len(), 3);
    assert_eq!(reg.tabs[0].name, "Main");
    assert_eq!(reg.tabs[1].name, "DepthBuffer");
    assert_eq!(reg.tabs[1].description, "Z-Buffer and Depth Stencil render target");
    assert_eq!(reg.tabs[2].name, "NormalMap");
    assert_eq!(reg.tabs[2].description, "World Normal and Geometric Specular buffer");
}

#[test]
fn test_mme_enable_effects_toggle() {
    let reg = ModelAssignmentRegistry::new();
    EFFECTS_ENABLED.store(false, Ordering::Release);
    assert!(reg.resolve_effect(1, -1).is_none(), "When effects are disabled, resolve_effect must return None");

    EFFECTS_ENABLED.store(true, Ordering::Release);
}
