//! DirectX 11 global pipeline state and resource tracking.

use std::sync::atomic::AtomicBool;
use std::sync::Mutex;

use crate::d3d11::D3D11Context;

pub static GLOBAL_D3D11: Mutex<Option<D3D11Context>> = Mutex::new(None);
pub static DX11_FRAME_RENDERED: AtomicBool = AtomicBool::new(false);
pub static DRAW_INDEXED_TOTAL: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);
pub static DRAW_INDEXED_ACTIVE: std::sync::atomic::AtomicU64 = std::sync::atomic::AtomicU64::new(0);

pub static BOUND_VB: Mutex<Option<(usize, u32, u32)>> = Mutex::new(None);
pub static BOUND_IB: Mutex<Option<usize>> = Mutex::new(None);
pub static BOUND_TEXTURES: Mutex<[Option<usize>; 8]> = Mutex::new([None; 8]);

pub fn release_d3d11_resources() {
    crate::log_msg("[vtable] release_d3d11_resources: resetting bound state");
    *BOUND_VB.lock().unwrap() = None;
    *BOUND_IB.lock().unwrap() = None;
    *BOUND_TEXTURES.lock().unwrap() = [None; 8];
}
