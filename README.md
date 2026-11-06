# Hikari (光)

> Direct3D 11 MMD/MME runtime and Qt 6 animation studio. C++20.

Hikari is a standalone Direct3D 11 runtime and MikuMikuEffect host for MikuMikuDance,
plus a desktop animation studio built with Qt Widgets. It implements Shader Model 5.0
rendering, runtime HLSL compilation, shader reflection, an MME-compatible synthetic
`ID3DXEffect` COM object, Jolt Physics rigid bodies, PMX/VMD/VPD assets and a Qt 6
editor.

## Layout

```
src/hikari/formats     PMX / VMD / VPD parsers and encoders
src/hikari/animation   skeleton, CCD IK, morphs, VMD timeline, jiggle
src/hikari/physics     Jolt MMD world, rigid bodies, 6DOF joints, physics rig
src/hikari/core        D3D11 device, reflection, effect engine, MMD host, Win32 UI
src/hikari/render      mesh, material, texture, scene renderer, parallel passes
src/hikari/studio      Qt 6 Widgets studio and native D3D11 viewport
tests                  Catch2 unit and integration tests
```

## Build

Requirements: Windows 10/11, Visual Studio 2022+ Build Tools (MSVC), CMake 3.29+,
Ninja, [vcpkg](https://github.com/microsoft/vcpkg), a Qt 6 MSVC 2022 x64 kit, and
ccache.

```powershell
$env:VCPKG_ROOT = "C:\vcpkg"
$env:QTDIR = "C:\Qt\6.12.0\msvc2022_64"

cmake --preset release
cmake --build --preset release
ctest --preset release
```

The vcpkg manifest supplies Jolt Physics, stb and Catch2. Qt is taken from the
installed kit through `QTDIR`. ccache is detected automatically and used as the
compiler launcher; `/Z7` is used so cached objects keep usable debug information.

Artifacts:

```
out/build/release/src/hikari/studio/hikari_studio.exe
out/build/release/src/hikari/core/hikari_core.dll
out/build/release/src/hikari/core/HikariEffects.dll
out/build/release/tests/hikari_tests.exe
```

Use `-DHIKARI_BUILD_TESTS=OFF` to skip tests and Catch2, or
`-DHIKARI_BUILD_STUDIO=OFF` for the core-only build.

## Studio

```powershell
out\build\release\src\hikari\studio\hikari_studio.exe model.pmx motion.vmd
```

The studio loads PMX models with materials, textures, morphs, skeleton, Jolt rigid
bodies and jiggle regions. VMD motions drive the timeline; VPD poses can be loaded and
saved; frame sequences are captured to BMP. VMD IK on/off frames drive the skeleton during
playback, and authored IK states are written back on export.

Viewport controls: right drag orbits, middle drag pans, wheel zooms, left click selects
a bone. The bottom-right gizmo rotates (first row) and moves (second row) the selected
bone or the camera; click its title to switch between local and global axes. `Space`
play/pause, `V` VSync, `P` physics, `J` jiggle, `R` reset physics.

Screen Size sets the offscreen viewport render resolution and aspect ratio; the
application window keeps its size and the image is letterboxed. Physics speed
(`Physical Operation > Simulation Speed`) scales the simulation rate without changing
gravity, so hair and cloth react faster without extra sag.

The editor provides model, bone, facial, camera, light, shadow, accessory, view,
playback and interpolation panels. Skeleton bones and Jolt rigid bodies/joints are
drawn as a D3D11 line overlay, so the viewport does not depend on Qt painting over the
swap chain.

## MMD installation

`hikari_core.dll` exports:

- `HikariInstallUi(HWND)` - injects the Hikari menu into the MMD main menu.
- `HikariOnDeviceCreated(HWND)` - creates the D3D11 device and effect manager.
- `HikariOnEndScene()` - advances the frame, syncs host objects and drains effects.
- `HikariGetVersion()` and `HikariGetRecentLogs()` - diagnostics.

The effect engine exposes the full `ID3DXEffect` vtable used by MME: technique/pass
selection, parameter setters and getters, annotations, `SetTexture`, `BeginPass`,
`CommitChanges` and `EndPass`. HLSL is compiled from source at runtime with
`d3dcompiler_47.dll`; no D3D9 proxy or D3D9 device is involved. Effects use a 64 KB
`b0` constant buffer resolved through `D3DReflect`.

`deploy.ps1` builds the current preset and copies `hikari_core.dll`,
`HikariEffects.dll` and the sample `.fx` files into an MMD directory.

## Notes

- The renderer records the whole scene on deferred contexts and executes the resulting
  command lists on the immediate context. Opaque material subsets are recorded in parallel;
  blended subsets are sorted back to front across all visible models and drawn in one pass
  with depth writes disabled.
- PMX rigid body and joint rotations use the intrinsic YXZ convention (`qy * qx * qz`),
  matching the MMD authoring convention rather than glam's static-frame helper.
- HMM/HMD mapping files are supported, including legacy EMM/EMD section syntax.

## License

MIT. See [LICENSE](LICENSE).
