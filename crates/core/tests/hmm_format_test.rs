use hikari_core::effect::ModelAssignmentRegistry;

#[test]
fn test_hmm_format_roundtrip_and_structure() {
    let mut reg = ModelAssignmentRegistry::new();

    // 1. Add offscreen tab with description and rules
    reg.add_tab(
        "MaterialMap",
        "Material buffer render target for deferred shading",
        "self=hide; *.pmx=./shaders/gbuffer.fx; *=hide;",
    );

    // 2. Set default effect for Main (virtual or empty path test) and models
    let _ = reg.assign_model_effect(1, "miku.pmx", "./shaders/miku.fx");
    let _ = reg.assign_subset_effect(1, "miku.pmx", 0, "./shaders/miku_face.fx");
    let _ = reg.assign_model_effect(2, "stage.x", "./shaders/stage.fx");

    // Model override in MaterialMap tab
    let _ = reg.assign_tab_effect(1, Some(1), "miku.pmx", -1, "./shaders/miku_gbuffer.fx");
    let _ = reg.assign_tab_effect(1, Some(1), "miku.pmx", 0, "./shaders/miku_face_gbuffer.fx");
    reg.set_tab_item_shown(1, Some(1), -1, true);
    reg.set_tab_item_shown(1, Some(2), -1, false);

    // 3. Save to .hmm
    let hmm_tmp = std::env::temp_dir().join("test_roundtrip.hmm");
    let hmm_path = hmm_tmp.to_str().unwrap();

    let save_res = reg.save_hmm(hmm_path);
    assert!(save_res.is_ok(), "save_hmm failed: {:?}", save_res.err());

    let content = std::fs::read_to_string(hmm_path).unwrap();
    assert!(content.contains("[HikariMapping]"));
    assert!(content.contains("Version = 1.0"));
    assert!(content.contains("[Object]"));
    assert!(content.contains("[Main]"));
    assert!(content.contains("[MaterialMap]"));
    assert!(content.contains("Description = Material buffer render target"));
    assert!(content.contains("DefaultRules = self=hide; *.pmx=./shaders/gbuffer.fx; *=hide;"));
    assert!(content.contains("Pmd0.Subset.0 = ./shaders/miku_face.fx"));

    // 4. Load into a fresh registry
    let mut reg2 = ModelAssignmentRegistry::new();
    let load_res = reg2.load_hmm(hmm_path);
    assert!(load_res.is_ok(), "load_hmm failed: {:?}", load_res.err());

    assert_eq!(reg2.tabs.len(), 2);
    assert_eq!(reg2.tabs[0].name, "Main");
    assert_eq!(reg2.tabs[1].name, "MaterialMap");
    assert_eq!(reg2.tabs[1].description, "Material buffer render target for deferred shading");
    assert_eq!(reg2.tabs[1].default_rules, "self=hide; *.pmx=./shaders/gbuffer.fx; *=hide;");

    let _ = std::fs::remove_file(hmm_tmp);
}

#[test]
fn test_hmd_model_data_roundtrip() {
    let mut reg = ModelAssignmentRegistry::new();
    reg.add_tab("MaterialMap", "Material Target", "*=hide;");

    let _ = reg.assign_model_effect(42, "character.pmx", "./shaders/char.fx");
    let _ = reg.assign_subset_effect(42, "character.pmx", 0, "./shaders/char_eye.fx");
    let _ = reg.assign_tab_effect(1, Some(42), "character.pmx", -1, "./shaders/char_gbuffer.fx");

    let hmd_tmp = std::env::temp_dir().join("test_char.hmd");
    let hmd_path = hmd_tmp.to_str().unwrap();

    let save_res = reg.save_hmd(42, hmd_path);
    assert!(save_res.is_ok(), "save_hmd failed: {:?}", save_res.err());

    let content = std::fs::read_to_string(hmd_path).unwrap();
    assert!(content.contains("[HikariModelData]"));
    assert!(content.contains("Version = 1.0"));
    assert!(content.contains("[Main]"));
    assert!(content.contains("[MaterialMap]"));

    let mut reg2 = ModelAssignmentRegistry::new();
    let _ = reg2.assign_model_effect(42, "character.pmx", "");
    let load_res = reg2.load_hmd(42, hmd_path);
    assert!(load_res.is_ok(), "load_hmd failed: {:?}", load_res.err());

    let _ = std::fs::remove_file(hmd_tmp);
}

#[test]
fn test_legacy_emm_fallback_in_load_hmm() {
    let legacy_emm = r#"[Object]
Pmd0 = test_model.pmx

[Effect]
Default = ./legacy/default.fx
Pmd0 = ./legacy/model.fx
Pmd0.show = true
Pmd0.0 = ./legacy/subset0.fx
Pmd0.0.show = true

[Effect@DepthMap]
Default = ./legacy/depth.fx
Pmd0 = ./legacy/model_depth.fx
Pmd0.show = false
"#;
    let emm_tmp = std::env::temp_dir().join("legacy_test.emm");
    std::fs::write(&emm_tmp, legacy_emm).unwrap();

    let mut reg = ModelAssignmentRegistry::new();
    // load_hmm should detect absence of [HikariMapping] and seamlessly fallback to load_emm
    let load_res = reg.load_hmm(emm_tmp.to_str().unwrap());
    assert!(load_res.is_ok(), "load_hmm legacy fallback failed: {:?}", load_res.err());

    assert_eq!(reg.tabs.len(), 2);
    assert_eq!(reg.tabs[0].name, "Main");
    assert_eq!(reg.tabs[1].name, "DepthMap");

    let _ = std::fs::remove_file(emm_tmp);
}

#[test]
fn test_tab_automatic_pruning() {
    let mut reg = ModelAssignmentRegistry::new();

    // 1. Add unused offscreen tab without overrides
    reg.add_tab("UnusedTab", "Unused Target", "");
    assert_eq!(reg.tabs.len(), 2);

    // Prune -> UnusedTab has no overrides and no active shader referencing it, so it should be pruned
    reg.prune_unused_tabs();
    assert_eq!(reg.tabs.len(), 1);
    assert_eq!(reg.tabs[0].name, "Main");

    // 2. Add offscreen tab WITH user override
    reg.add_tab("ConfiguredTab", "Configured Target", "");
    reg.set_tab_item_shown(1, Some(10), -1, false);
    assert_eq!(reg.tabs.len(), 2);

    // Prune -> ConfiguredTab has user override, so it must NOT be pruned
    reg.prune_unused_tabs();
    assert_eq!(reg.tabs.len(), 2);
    assert_eq!(reg.tabs[1].name, "ConfiguredTab");
}
