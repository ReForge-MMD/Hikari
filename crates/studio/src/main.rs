//! Hikari Studio - Modern standalone 3D animation runtime and studio on pure DirectX 11.

use hikari_animation::math::Vec3;
use hikari_animation::physics::{PhysicsRig, PhysicsSettings};
use hikari_animation::{Jiggle, JiggleSettings, MorphEvaluator, MotionPlayer, Skeleton};
use hikari_core::d3d11::D3D11Context;
use hikari_formats::{PmxModel, VmdMotion};
use hikari_render::{Camera, GpuMaterial, GpuMesh, GpuTexture, SceneRenderer};
use std::os::windows::ffi::OsStrExt;
use std::path::Path;
use std::time::Instant;
use windows::core::{w, PCWSTR};
use windows::Win32::Foundation::{COLORREF, HINSTANCE, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Gdi::{CreateSolidBrush, HBRUSH};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::Input::KeyboardAndMouse::{ReleaseCapture, SetCapture};
use windows::Win32::UI::WindowsAndMessaging::*;

struct StudioApp {
    ctx: D3D11Context,
    renderer: SceneRenderer,
    camera: Camera,
    mesh: Option<GpuMesh>,
    materials: Vec<GpuMaterial>,
    textures: Vec<Option<GpuTexture>>,
    skeleton: Option<Skeleton>,
    physics_rig: Option<PhysicsRig>,
    physics_enabled: bool,
    jiggle: Option<Jiggle>,
    jiggle_settings: JiggleSettings,
    jiggle_enabled: bool,
    last_frame_instant: Instant,
    last_frame_number: f32,
    morphs: MorphEvaluator,
    motion_player: Option<MotionPlayer>,
    model_name: String,
    vertex_count: usize,
    index_count: usize,
    bone_count: usize,
    start_time: Instant,
    camera_distance: f32,
    camera_yaw: f32,
    camera_pitch: f32,
    camera_pan: Vec3,
    last_mouse_pos: (i32, i32),
    mouse_l_down: bool,
    mouse_r_down: bool,
    fps_counter: u32,
    fps_timer: Instant,
    current_fps: f32,
    current_frame_time_ms: f32,
    last_title_update: Instant,
    vsync: bool,
}

static mut APP_PTR: *mut StudioApp = std::ptr::null_mut();

unsafe extern "system" fn window_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    let app = if !APP_PTR.is_null() { Some(&mut *APP_PTR) } else { None };

    match msg {
        WM_SIZE => {
            let width = (lparam.0 & 0xFFFF) as u32;
            let height = ((lparam.0 >> 16) & 0xFFFF) as u32;
            if width > 0 && height > 0 {
                if let Some(app) = app {
                    let _ = app.ctx.resize(width, height);
                    app.camera.aspect = width as f32 / height as f32;
                }
            }
            LRESULT(0)
        }
        WM_LBUTTONDOWN => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            if let Some(app) = app {
                app.mouse_l_down = true;
                app.last_mouse_pos = (x, y);
            }
            SetCapture(hwnd);
            LRESULT(0)
        }
        WM_LBUTTONUP => {
            if let Some(app) = app {
                app.mouse_l_down = false;
            }
            let _ = ReleaseCapture();
            LRESULT(0)
        }
        WM_RBUTTONDOWN => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            if let Some(app) = app {
                app.mouse_r_down = true;
                app.last_mouse_pos = (x, y);
            }
            SetCapture(hwnd);
            LRESULT(0)
        }
        WM_RBUTTONUP => {
            if let Some(app) = app {
                app.mouse_r_down = false;
            }
            let _ = ReleaseCapture();
            LRESULT(0)
        }
        WM_MOUSEMOVE => {
            let x = (lparam.0 & 0xFFFF) as i16 as i32;
            let y = ((lparam.0 >> 16) & 0xFFFF) as i16 as i32;
            if let Some(app) = app {
                let dx = x - app.last_mouse_pos.0;
                let dy = y - app.last_mouse_pos.1;
                app.last_mouse_pos = (x, y);

                if app.mouse_l_down {
                    // Orbit
                    app.camera_yaw += dx as f32 * 0.005;
                    app.camera_pitch = (app.camera_pitch + dy as f32 * 0.005).clamp(-1.5, 1.5);
                } else if app.mouse_r_down {
                    // Pan
                    let pan_speed = app.camera_distance * 0.001;
                    app.camera_pan.x -= dx as f32 * pan_speed;
                    app.camera_pan.y += dy as f32 * pan_speed;
                }
            }
            LRESULT(0)
        }
        WM_MOUSEWHEEL => {
            let delta = ((wparam.0 >> 16) & 0xFFFF) as i16 as f32;
            if let Some(app) = app {
                app.camera_distance = (app.camera_distance - delta * 0.02).clamp(1.0, 500.0);
            }
            LRESULT(0)
        }
        WM_KEYDOWN => {
            if let Some(app) = app {
                // 'V' key (0x56) toggles VSync
                if wparam.0 == 0x56 {
                    app.vsync = !app.vsync;
                    println!("[Studio] VSync toggled: {}", if app.vsync { "ON" } else { "OFF" });
                }
                // 'P' key (0x50) toggles Physics simulation
                if wparam.0 == 0x50 {
                    app.physics_enabled = !app.physics_enabled;
                    println!("[Studio] Physics simulation toggled: {}", if app.physics_enabled { "ON" } else { "OFF" });
                }
                // 'J' key (0x4A) toggles Jiggle secondary dynamics
                if wparam.0 == 0x4A {
                    app.jiggle_enabled = !app.jiggle_enabled;
                    println!("[Studio] Jiggle secondary dynamics toggled: {}", if app.jiggle_enabled { "ON" } else { "OFF" });
                }
                // 'R' key (0x52) resets physics simulation
                if wparam.0 == 0x52 {
                    if let Some(ref mut rig) = app.physics_rig {
                        rig.request_reset();
                        println!("[Studio] Physics simulation reset requested");
                    }
                }
            }
            LRESULT(0)
        }
        WM_DESTROY => {
            PostQuitMessage(0);
            LRESULT(0)
        }
        _ => DefWindowProcW(hwnd, msg, wparam, lparam),
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let model_path = args.get(1);
    let motion_path = args.get(2);

    println!("===========================================================");
    println!("  Hikari Studio - Standalone 3D DirectX 11 Animation Engine");
    println!("===========================================================");

    unsafe {
        let instance: HINSTANCE = GetModuleHandleW(PCWSTR::null()).unwrap().into();
        let class_name = w!("HikariStudioWindowClass");

        let wc = WNDCLASSEXW {
            cbSize: std::mem::size_of::<WNDCLASSEXW>() as u32,
            style: CS_HREDRAW | CS_VREDRAW,
            lpfnWndProc: Some(window_proc),
            hInstance: instance,
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap(),
            hbrBackground: HBRUSH(CreateSolidBrush(COLORREF(0x1e1e1e)).0),
            lpszClassName: class_name,
            ..Default::default()
        };
        RegisterClassExW(&wc);

        let width = 1280;
        let height = 720;
        let mut rect = RECT {
            left: 100,
            top: 100,
            right: 100 + width,
            bottom: 100 + height,
        };
        AdjustWindowRect(&mut rect, WS_OVERLAPPEDWINDOW, false).unwrap();

        let hwnd = CreateWindowExW(
            WINDOW_EX_STYLE::default(),
            class_name,
            w!("Hikari Studio [DirectX 11 Standalone Engine]"),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            None,
            None,
            Some(instance),
            None,
        ).unwrap();

        let ctx = D3D11Context::new(hwnd, width as u32, height as u32)
            .expect("Failed to initialize DirectX 11 device and swapchain");

        let renderer = SceneRenderer::new(&ctx)
            .expect("Failed to initialize SceneRenderer");

        let mut app = StudioApp {
            ctx,
            renderer,
            camera: Camera::default(),
            mesh: None,
            materials: Vec::new(),
            textures: Vec::new(),
            skeleton: None,
            physics_rig: None,
            physics_enabled: true,
            jiggle: None,
            jiggle_settings: JiggleSettings::default(),
            jiggle_enabled: true,
            last_frame_instant: Instant::now(),
            last_frame_number: 0.0,
            morphs: MorphEvaluator::new(),
            motion_player: None,
            model_name: String::new(),
            vertex_count: 0,
            index_count: 0,
            bone_count: 0,
            start_time: Instant::now(),
            camera_distance: 30.0,
            camera_yaw: 0.0,
            camera_pitch: 0.1,
            camera_pan: Vec3::new(0.0, 10.0, 0.0),
            last_mouse_pos: (0, 0),
            mouse_l_down: false,
            mouse_r_down: false,
            fps_counter: 0,
            fps_timer: Instant::now(),
            current_fps: 0.0,
            current_frame_time_ms: 0.0,
            last_title_update: Instant::now(),
            vsync: false,
        };

        // Load PMX model if specified
        if let Some(path) = model_path {
            match std::fs::read(path) {
                Ok(bytes) => match PmxModel::parse(&bytes) {
                    Ok(pmx) => {
                        println!("[Studio] Loaded PMX Model: '{}' ({} vertices, {} bones, {} textures)", pmx.info.name_local, pmx.vertices.len(), pmx.bones.len(), pmx.textures.len());
                        app.model_name = pmx.info.name_local.clone();
                        app.vertex_count = pmx.vertices.len();
                        app.index_count = pmx.indices.len();
                        app.bone_count = pmx.bones.len();

                        let pmx_dir = Path::new(path).parent().unwrap_or(Path::new(""));
                        let mut textures = Vec::with_capacity(pmx.textures.len());
                        for tex_name in &pmx.textures {
                            let norm = tex_name.replace('\\', "/");
                            let c1 = pmx_dir.join(tex_name);
                            let c2 = pmx_dir.join(&norm);
                            let target = if c1.exists() {
                                Some(c1)
                            } else if c2.exists() {
                                Some(c2)
                            } else {
                                Path::new(&norm).file_name().map(|f| pmx_dir.join(f)).filter(|p| p.exists())
                            };

                            if let Some(target_path) = target {
                                match GpuTexture::from_file(&app.ctx, &target_path) {
                                    Ok(tex) => {
                                        textures.push(Some(tex));
                                    }
                                    Err(e) => {
                                        eprintln!("[Studio] Failed to decode texture '{}': {e}", target_path.display());
                                        textures.push(None);
                                    }
                                }
                            } else {
                                eprintln!("[Studio] Texture not found on disk: '{}'", tex_name);
                                textures.push(None);
                            }
                        }

                        let mesh = GpuMesh::from_pmx(&app.ctx, &pmx).expect("GPU Mesh build failed");
                        let mut mats = Vec::new();
                        for m in &pmx.materials {
                            if let Ok(gpu_mat) = GpuMaterial::from_pmx_material(&app.ctx, m) {
                                mats.push(gpu_mat);
                            }
                        }
                        let skel = Skeleton::from_pmx(&pmx);
                        let bone_rests: Vec<[f32; 3]> = pmx.bones.iter().map(|b| b.position).collect();
                        let phys_settings = PhysicsSettings::default();
                        app.physics_rig = PhysicsRig::new(&pmx, &bone_rests, &phys_settings);
                        if let Some(ref rig) = app.physics_rig {
                            println!("[Studio] Initialized Jolt Physics: {} bodies ({} dynamic)", rig.body_count(), rig.dynamic_count());
                        }

                        let jiggle = Jiggle::new(&pmx);
                        if jiggle.body_count() > 0 {
                            println!("[Studio] Initialized Jiggle dynamics: {} soft-tissue targets", jiggle.body_count());
                            app.jiggle = Some(jiggle);
                        }

                        app.mesh = Some(mesh);
                        app.materials = mats;
                        app.textures = textures;
                        app.skeleton = Some(skel);
                        app.morphs = MorphEvaluator::from_pmx(&pmx);
                    }
                    Err(e) => eprintln!("[Studio] Failed to parse PMX: {e}"),
                },
                Err(e) => eprintln!("[Studio] Failed to read PMX file '{path}': {e}"),
            }
        }

        // Load VMD motion if specified
        if let Some(path) = motion_path {
            match std::fs::read(path) {
                Ok(bytes) => match VmdMotion::parse(&bytes) {
                    Ok(vmd) => {
                        println!("[Studio] Loaded VMD Motion: '{}' ({} bone frames, {} morph frames, max frame: {})",
                            vmd.model_name, vmd.bone_frames.len(), vmd.morph_frames.len(),
                            vmd.bone_frames.iter().map(|f| f.frame).max().unwrap_or(0));
                        app.motion_player = Some(MotionPlayer::new(&vmd));
                    }
                    Err(e) => eprintln!("[Studio] Failed to parse VMD: {e}"),
                },
                Err(e) => eprintln!("[Studio] Failed to read VMD file '{path}': {e}"),
            }
        }

        APP_PTR = &mut app as *mut StudioApp;

        let mut msg = MSG::default();
        'main_loop: loop {
            while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
                if msg.message == WM_QUIT {
                    break 'main_loop;
                }
                let _ = TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            // Update camera from orbital parameters
            let cy = app.camera_yaw.cos();
            let sy = app.camera_yaw.sin();
            let cp = app.camera_pitch.cos();
            let sp = app.camera_pitch.sin();

            let eye_offset = Vec3::new(
                app.camera_distance * cp * sy,
                app.camera_distance * sp,
                -app.camera_distance * cp * cy,
            );
            app.camera.position = app.camera_pan + eye_offset;
            app.camera.target = app.camera_pan;

            // Clear screen to deep studio dark gray
            app.ctx.bind_default_targets();
            app.ctx.clear([0.14, 0.14, 0.16, 1.0], 1.0, 0);

            // Render 3D studio floor grid
            let _ = app.renderer.render_grid(&app.ctx, &app.camera);

            // Animate and render
            let now = Instant::now();
            let dt = now.duration_since(app.last_frame_instant).as_secs_f32().clamp(0.0001, 0.1);
            app.last_frame_instant = now;

            let elapsed_secs = app.start_time.elapsed().as_secs_f32();
            let current_frame = elapsed_secs * 30.0; // MMD standard 30 FPS timeline

            if let (Some(ref mut skel), Some(ref player)) = (&mut app.skeleton, &app.motion_player) {
                let looped_frame = if player.max_frame > 0 {
                    current_frame % (player.max_frame as f32 + 1.0)
                } else {
                    current_frame
                };

                if looped_frame < app.last_frame_number {
                    if let Some(ref mut rig) = app.physics_rig {
                        rig.request_reset();
                    }
                }
                app.last_frame_number = looped_frame;

                player.evaluate_pose(looped_frame, skel, &mut app.morphs);
            }

            // Step Jolt physics and apply bone overrides
            if app.physics_enabled {
                if let (Some(ref mut skel), Some(ref mut rig)) = (&mut app.skeleton, &mut app.physics_rig) {
                    skel.apply_physics(rig, dt);
                }
            }

            // Apply soft-tissue secondary dynamics (Jiggle)
            if app.jiggle_enabled {
                if let (Some(ref mut skel), Some(ref mut jiggle)) = (&mut app.skeleton, &mut app.jiggle) {
                    skel.apply_jiggle(jiggle, dt, &app.jiggle_settings);
                }
            }

            // Apply dynamic facial and vertex morphs to mesh
            if let Some(ref mut mesh) = app.mesh {
                let (pos_offsets, uv_offsets) = app.morphs.compute_offsets();
                let _ = mesh.apply_morph_offsets(&app.ctx, &pos_offsets, &uv_offsets);
            }

            if let (Some(ref mesh), Some(ref skel)) = (&app.mesh, &app.skeleton) {
                let _ = app.renderer.render_mesh(
                    &app.ctx,
                    mesh,
                    &app.materials,
                    &app.textures,
                    &skel.skinning_palette,
                    &app.camera,
                );
            }

            // Present to screen (sync_interval = 0 for uncapped, 1 for VSync)
            let sync_interval = if app.vsync { 1 } else { 0 };
            let _ = app.ctx.present(sync_interval);

            // Compute FPS and frame time metrics
            app.fps_counter += 1;
            let fps_elapsed = app.fps_timer.elapsed().as_secs_f32();
            if fps_elapsed >= 0.5 {
                app.current_fps = app.fps_counter as f32 / fps_elapsed;
                app.current_frame_time_ms = (fps_elapsed / app.fps_counter as f32) * 1000.0;
                app.fps_counter = 0;
                app.fps_timer = Instant::now();
            }

            // Update window title metrics periodically (every 100ms)
            if app.last_title_update.elapsed().as_millis() >= 100 {
                app.last_title_update = Instant::now();
                let cur_frame_str = if let Some(ref player) = app.motion_player {
                    let looped = if player.max_frame > 0 {
                        current_frame % (player.max_frame as f32 + 1.0)
                    } else {
                        current_frame
                    };
                    format!("Frame: {:05.1} / {:05}", looped, player.max_frame)
                } else {
                    "Static Pose".to_string()
                };

                let vsync_str = if app.vsync { "VSync: ON" } else { "VSync: OFF" };
                let phys_str = if app.physics_enabled && app.physics_rig.is_some() {
                    "Jolt Phys: ON"
                } else if app.physics_rig.is_some() {
                    "Jolt Phys: OFF"
                } else {
                    "No Phys"
                };
                let jiggle_str = if app.jiggle_enabled && app.jiggle.is_some() {
                    "Jiggle: ON"
                } else if app.jiggle.is_some() {
                    "Jiggle: OFF"
                } else {
                    "No Jiggle"
                };
                let title = format!(
                    "Hikari Studio [DX11] | {:.1} FPS ({:.2} ms) [{}] [{}] [{}] | '{}' ({} vtx, {} tris, {} bones) | {} | Cam: {:.1}m\0",
                    app.current_fps,
                    app.current_frame_time_ms,
                    vsync_str,
                    phys_str,
                    jiggle_str,
                    if app.model_name.is_empty() { "Empty" } else { &app.model_name },
                    app.vertex_count,
                    app.index_count / 3,
                    app.bone_count,
                    cur_frame_str,
                    app.camera_distance,
                );
                let wide: Vec<u16> = std::ffi::OsStr::new(&title).encode_wide().collect();
                let _ = SetWindowTextW(hwnd, PCWSTR(wide.as_ptr()));
            }
        }

        APP_PTR = std::ptr::null_mut();
    }
}
