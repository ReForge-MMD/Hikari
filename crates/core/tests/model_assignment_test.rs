use hikari_core::effect::ModelAssignmentRegistry;

#[test]
fn test_model_assignment_resolution_hierarchy() {
    let mut reg = ModelAssignmentRegistry::new();

    let dummy_default = 0x1000 as *mut hikari_core::effect::HikariEffect;
    let dummy_model_a = 0x2000 as *mut hikari_core::effect::HikariEffect;
    let dummy_subset_1 = 0x3000 as *mut hikari_core::effect::HikariEffect;

    // 1. Initially nothing is set
    assert_eq!(reg.resolve_effect(1, 0), None);

    // 2. Set default effect
    reg.default_effect = Some(dummy_default);
    reg.default_effect_path = "default.fx".to_string();

    // Any unknown model/order falls back to default
    assert_eq!(reg.resolve_effect(0, -1), Some(dummy_default));
    assert_eq!(reg.resolve_effect(5, 0), Some(dummy_default));

    // 3. Register model 1 (order 10)
    let model_id = 0xAAAA;
    reg.order_to_id.insert(10, model_id);
    let mut model = hikari_core::effect::ModelAssignment {
        object_id: model_id,
        filename: "test_model.pmx".to_string(),
        effect_path: "model.fx".to_string(),
        effect: Some(dummy_model_a),
        subsets: std::collections::HashMap::new(),
        enabled: true,
    };

    // Subset 1 has a specific override
    model.subsets.insert(1, hikari_core::effect::ModelSubsetAssignment {
        effect_path: "subset1.fx".to_string(),
        effect: Some(dummy_subset_1),
        enabled: true,
    });

    reg.models.insert(model_id, model);

    // Query order 10, subset 1 -> Should return subset override (0x3000)
    assert_eq!(reg.resolve_effect(10, 1), Some(dummy_subset_1));

    // Query order 10, subset 0 (no override) -> Should fall back to model effect (0x2000)
    assert_eq!(reg.resolve_effect(10, 0), Some(dummy_model_a));

    // Query order 10, subset -1 (whole model) -> Should return model effect (0x2000)
    assert_eq!(reg.resolve_effect(10, -1), Some(dummy_model_a));

    // In Camera Mode, object_order is 0, but model subset rendering continues.
    // Query order 0, subset 1 -> Should resolve against active model subset override (0x3000)
    assert_eq!(reg.resolve_effect(0, 1), Some(dummy_subset_1));

    // Query order 0, subset 0 -> Should resolve against active model effect (0x2000)
    assert_eq!(reg.resolve_effect(0, 0), Some(dummy_model_a));

    // Query order 99 (unknown order) -> Should fall back to default effect (0x1000)
    assert_eq!(reg.resolve_effect(99, 0), Some(dummy_default));

    // 4. Test expansion toggling
    assert!(!reg.is_subset_expanded(model_id));
    reg.toggle_subset_expand(model_id);
    assert!(reg.is_subset_expanded(model_id));
    reg.toggle_subset_expand(model_id);
    assert!(!reg.is_subset_expanded(model_id));

    // 5. Test is_effect_alive validation
    assert!(reg.is_effect_alive(dummy_default));
    assert!(reg.is_effect_alive(dummy_model_a));
    assert!(reg.is_effect_alive(dummy_subset_1));
    assert!(!reg.is_effect_alive(0x9999 as *mut hikari_core::effect::HikariEffect));
    assert!(!reg.is_effect_alive(std::ptr::null_mut()));

    // Prevent Drop on fake pointers
    reg.default_effect = None;
    if let Some(m) = reg.models.get_mut(&model_id) {
        m.effect = None;
        if let Some(s) = m.subsets.get_mut(&1) {
            s.effect = None;
        }
    }

    assert!(!reg.is_effect_alive(dummy_default));
    assert!(!reg.is_effect_alive(dummy_model_a));
    assert!(!reg.is_effect_alive(dummy_subset_1));
}
