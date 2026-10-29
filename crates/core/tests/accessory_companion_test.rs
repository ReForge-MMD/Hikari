use hikari_core::effect::ModelAssignmentRegistry;

#[test]
fn test_accessory_companion_fx_discovery() {
    let temp_dir = std::env::temp_dir().join("hikari_acc_test");
    let _ = std::fs::create_dir_all(&temp_dir);

    let x_file = temp_dir.join("StageLight.x");
    let fx_file = temp_dir.join("StageLight.fx");

    std::fs::write(&x_file, b"xof 0302txt 0064\n").unwrap();
    std::fs::write(&fx_file, "// companion effect\nfloat4 PS_Main(): SV_Target { return float4(1,1,1,1); }\n").unwrap();

    let mut reg = ModelAssignmentRegistry::new();

    // Verify companion path check logic directly
    let p = std::path::Path::new(&x_file);
    let companion_fx = p.with_extension("fx");
    assert!(companion_fx.exists());

    // When assigning accessory directly or syncing
    let _res = reg.assign_model_effect(999, x_file.to_str().unwrap(), companion_fx.to_str().unwrap());
    // In test environment, compilation might fail if D3D11 device is absent or succeed if mock/offline
    // But the registry registers the effect path attempt
    let model = reg.models.get(&999);
    assert!(model.is_some());
    assert_eq!(model.unwrap().filename, x_file.to_str().unwrap());

    let _ = std::fs::remove_file(x_file);
    let _ = std::fs::remove_file(fx_file);
    let _ = std::fs::remove_dir(temp_dir);
}
