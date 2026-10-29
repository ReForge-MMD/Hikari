use hikari_core::effect::ASSIGNMENT_REGISTRY;
use hikari_core::mmd_host::get_host_objects_fingerprint;

#[test]
fn test_host_objects_fingerprint_stability() {
    let fp1 = get_host_objects_fingerprint();
    let fp2 = get_host_objects_fingerprint();
    assert_eq!(fp1, fp2, "Fingerprint must be stable when scene state does not change");
}

#[test]
fn test_sync_host_objects_lifecycle() {
    let mut reg = ASSIGNMENT_REGISTRY.lock().unwrap();
    reg.sync_host_objects();

    // Verify registry can safely synchronize without host exports (headless test mode)
    let _eff = reg.resolve_effect(0, -1);
}
