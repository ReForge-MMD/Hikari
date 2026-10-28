use windows::Win32::Foundation::{HANDLE, HWND};
use windows::Win32::Graphics::Direct3D9::*;
use windows::Win32::Graphics::Direct3D11::*;
use windows::Win32::UI::WindowsAndMessaging::{CreateWindowExW, DestroyWindow, ShowWindow, SW_SHOW, WS_OVERLAPPEDWINDOW};
use hikari_core::d3d11::D3D11Context;

#[test]
fn test_real_mmd_style_d3d9_shared_handle() {
    unsafe {
        let hwnd = CreateWindowExW(
            Default::default(),
            windows::core::w!("STATIC"),
            windows::core::w!("MMDStyleTest"),
            WS_OVERLAPPEDWINDOW,
            0, 0, 640, 480,
            Some(HWND::default()),
            None,
            None,
            None,
        ).expect("CreateWindowExW failed");
        let _ = ShowWindow(hwnd, SW_SHOW);

        let d3d9ex = Direct3DCreate9Ex(D3D_SDK_VERSION).expect("Direct3DCreate9Ex failed");

        let mut pp = D3DPRESENT_PARAMETERS {
            BackBufferWidth: 640,
            BackBufferHeight: 480,
            BackBufferFormat: D3DFMT_X8R8G8B8,
            BackBufferCount: 1,
            MultiSampleType: D3DMULTISAMPLE_NONE,
            MultiSampleQuality: 0,
            SwapEffect: D3DSWAPEFFECT_DISCARD,
            hDeviceWindow: hwnd,
            Windowed: true.into(),
            EnableAutoDepthStencil: true.into(),
            AutoDepthStencilFormat: D3DFMT_D24S8,
            Flags: 0,
            FullScreen_RefreshRateInHz: 0,
            PresentationInterval: 0,
        };

        let mut dev9ex: Option<IDirect3DDevice9Ex> = None;
        let flags = (D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED) as u32;
        let hr_dev = d3d9ex.CreateDeviceEx(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            flags,
            &mut pp,
            std::ptr::null_mut(),
            &mut dev9ex,
        );
        println!("CreateDeviceEx hr: {:?}", hr_dev);
        assert!(hr_dev.is_ok(), "CreateDeviceEx should succeed");
        let dev = dev9ex.expect("dev9ex must not be none");

        let mut shared_handle = HANDLE::default();
        let mut tex: Option<IDirect3DTexture9> = None;
        let hr_tex = dev.CreateTexture(
            128,
            128,
            1,
            D3DUSAGE_RENDERTARGET as u32,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &mut tex,
            &mut shared_handle,
        );
        println!(
            "D3D9Ex CreateTexture(shared_handle): hr={:?}, handle={:?}",
            hr_tex, shared_handle.0
        );
        assert!(hr_tex.is_ok(), "D3D9Ex CreateTexture with shared_handle should succeed!");
        assert!(!shared_handle.0.is_null(), "shared_handle must not be NULL!");

        // Test D3DPOOL_MANAGED remapping pattern: (D3DPOOL_DEFAULT + D3DUSAGE_DYNAMIC)
        let mut dyn_tex: Option<IDirect3DTexture9> = None;
        let hr_dyn = dev.CreateTexture(
            64,
            64,
            1,
            D3DUSAGE_DYNAMIC as u32,
            D3DFMT_A8R8G8B8,
            D3DPOOL_DEFAULT,
            &mut dyn_tex,
            std::ptr::null_mut(),
        );
        println!("D3D9Ex CreateTexture(DYNAMIC, DEFAULT): hr={:?}", hr_dyn);
        assert!(hr_dyn.is_ok(), "Dynamic DEFAULT texture must succeed in D3D9Ex");

        let mut locked = D3DLOCKED_RECT::default();
        let hr_lock = dyn_tex.as_ref().unwrap().LockRect(0, &mut locked, std::ptr::null(), 0);
        println!("D3D9Ex LockRect on DYNAMIC DEFAULT: hr={:?}", hr_lock);
        assert!(hr_lock.is_ok(), "LockRect on dynamic DEFAULT texture must succeed in D3D9Ex!");
        let _ = dyn_tex.as_ref().unwrap().UnlockRect(0);

        // Test VertexBuffer D3DPOOL_MANAGED remapping
        let mut vb: Option<IDirect3DVertexBuffer9> = None;
        let hr_vb = dev.CreateVertexBuffer(
            1024,
            D3DUSAGE_DYNAMIC as u32,
            0,
            D3DPOOL_DEFAULT,
            &mut vb,
            std::ptr::null_mut(),
        );
        println!("D3D9Ex CreateVertexBuffer(DYNAMIC, DEFAULT): hr={:?}", hr_vb);
        assert!(hr_vb.is_ok());
        let mut vb_bits: *mut std::ffi::c_void = std::ptr::null_mut();
        let hr_vblock = vb.as_ref().unwrap().Lock(0, 0, &mut vb_bits, 0);
        println!("D3D9Ex Lock on VB: hr={:?}", hr_vblock);
        assert!(hr_vblock.is_ok());
        let _ = vb.as_ref().unwrap().Unlock();

        // Test IndexBuffer D3DPOOL_MANAGED remapping
        let mut ib: Option<IDirect3DIndexBuffer9> = None;
        let hr_ib = dev.CreateIndexBuffer(
            512,
            D3DUSAGE_DYNAMIC as u32,
            D3DFMT_INDEX16,
            D3DPOOL_DEFAULT,
            &mut ib,
            std::ptr::null_mut(),
        );
        println!("D3D9Ex CreateIndexBuffer(DYNAMIC, DEFAULT): hr={:?}", hr_ib);
        assert!(hr_ib.is_ok());
        let mut ib_bits: *mut std::ffi::c_void = std::ptr::null_mut();
        let hr_iblock = ib.as_ref().unwrap().Lock(0, 0, &mut ib_bits, 0);
        println!("D3D9Ex Lock on IB: hr={:?}", hr_iblock);
        assert!(hr_iblock.is_ok());
        let _ = ib.as_ref().unwrap().Unlock();

        let ctx11 = D3D11Context::new(hwnd, 128, 128).expect("D3D11 failed");
        let mut d3d11_res: Option<ID3D11Resource> = None;
        let hr_open = ctx11.device.OpenSharedResource(shared_handle, &mut d3d11_res);
        println!("D3D11 OpenSharedResource from D3D9Ex: {:?}", hr_open);
        assert!(hr_open.is_ok(), "D3D11 OpenSharedResource from D3D9Ex MUST succeed!");
        assert!(d3d11_res.is_some(), "D3D11 resource must exist!");

        let _ = DestroyWindow(hwnd);
    }
}
