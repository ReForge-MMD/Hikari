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

        let d3d9 = Direct3DCreate9(D3D_SDK_VERSION).expect("Direct3DCreate9 failed");

        let mut mode = D3DDISPLAYMODE::default();
        d3d9.GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mut mode).expect("GetAdapterDisplayMode failed");

        let mut pp = D3DPRESENT_PARAMETERS {
            BackBufferWidth: 640,
            BackBufferHeight: 480,
            BackBufferFormat: mode.Format,
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

        let mut dev9: Option<IDirect3DDevice9> = None;
        let hr = d3d9.CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            hwnd,
            D3DCREATE_HARDWARE_VERTEXPROCESSING as u32,
            &mut pp,
            &mut dev9,
        );
        println!("Regular D3D9 CreateDevice hr: {:?}", hr);
        if let Some(dev) = dev9 {
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
                "Regular D3D9 CreateTexture(shared_handle): hr={:?}, handle={:?}",
                hr_tex, shared_handle.0
            );

            if hr_tex.is_ok() && !shared_handle.0.is_null() {
                let ctx11 = D3D11Context::new(hwnd, 128, 128).expect("D3D11 failed");
                let mut d3d11_res: Option<ID3D11Resource> = None;
                let hr_open = ctx11.device.OpenSharedResource(shared_handle, &mut d3d11_res);
                println!("D3D11 OpenSharedResource from regular D3D9: {:?}", hr_open);
                assert!(hr_open.is_ok());
            }
        }

        let _ = DestroyWindow(hwnd);
    }
}
