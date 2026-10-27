use hikari_core::d3d11::D3D11Context;
use hikari_core::shader::compile_hlsl;
use windows::Win32::Foundation::HWND;
use windows::Win32::Graphics::Direct3D11::*;

#[test]
fn test_compute_shader_uav_execution_and_readback() {
    let ctx = D3D11Context::new(HWND(std::ptr::null_mut()), 0, 0)
        .expect("Failed to create headless D3D11 context");

    let cs_source = r#"
RWStructuredBuffer<uint> OutputData : register(u0);

[numthreads(8, 1, 1)]
void CS_Compute(uint3 id : SV_DispatchThreadID) {
    OutputData[id.x] = id.x * id.x + 10;
}
"#;

    let cs_bytecode = compile_hlsl(cs_source, "CS_Compute", "cs_5_0", None, &[])
        .expect("Failed to compile Compute Shader");

    let mut cs = None;
    unsafe {
        ctx.device.CreateComputeShader(&cs_bytecode, None, Some(&mut cs))
            .expect("CreateComputeShader failed");
    }
    let cs = cs.unwrap();

    let element_count = 8u32;
    let byte_width = element_count * 4;

    // Create GPU RWStructuredBuffer
    let buf_desc = D3D11_BUFFER_DESC {
        ByteWidth: byte_width,
        Usage: D3D11_USAGE_DEFAULT,
        BindFlags: (D3D11_BIND_UNORDERED_ACCESS.0 | D3D11_BIND_SHADER_RESOURCE.0) as u32,
        CPUAccessFlags: 0,
        MiscFlags: D3D11_RESOURCE_MISC_BUFFER_STRUCTURED.0 as u32,
        StructureByteStride: 4,
    };

    let mut gpu_buffer = None;
    unsafe {
        ctx.device.CreateBuffer(&buf_desc, None, Some(&mut gpu_buffer))
            .expect("CreateBuffer for GPU UAV failed");
    }
    let gpu_buffer = gpu_buffer.unwrap();

    let uav_desc = D3D11_UNORDERED_ACCESS_VIEW_DESC {
        Format: windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_UNKNOWN,
        ViewDimension: D3D11_UAV_DIMENSION_BUFFER,
        Anonymous: unsafe {
            let mut a: D3D11_UNORDERED_ACCESS_VIEW_DESC_0 = std::mem::zeroed();
            a.Buffer.NumElements = element_count;
            a.Buffer.Flags = 0;
            a
        },
    };

    let mut uav = None;
    unsafe {
        ctx.device.CreateUnorderedAccessView(&gpu_buffer, Some(&uav_desc), Some(&mut uav))
            .expect("CreateUnorderedAccessView failed");
    }
    let uav = uav.unwrap();

    // Bind CS and UAV
    unsafe {
        ctx.context.CSSetShader(Some(&cs), None);
    }
    ctx.cs_set_uavs(0, &[Some(uav)]);

    // Dispatch 1 thread group (8 threads)
    ctx.dispatch_compute(1, 1, 1);

    // Unbind UAV
    ctx.cs_set_uavs(0, &[None]);

    // Create CPU staging buffer to read back GPU results
    let staging_desc = D3D11_BUFFER_DESC {
        ByteWidth: byte_width,
        Usage: D3D11_USAGE_STAGING,
        BindFlags: 0,
        CPUAccessFlags: D3D11_CPU_ACCESS_READ.0 as u32,
        MiscFlags: 0,
        StructureByteStride: 4,
    };

    let mut staging_buffer = None;
    unsafe {
        ctx.device.CreateBuffer(&staging_desc, None, Some(&mut staging_buffer))
            .expect("CreateBuffer for staging readback failed");
    }
    let staging_buffer = staging_buffer.unwrap();

    // Copy GPU buffer to staging buffer
    unsafe {
        ctx.context.CopyResource(&staging_buffer, &gpu_buffer);

        let mut mapped = D3D11_MAPPED_SUBRESOURCE::default();
        ctx.context.Map(&staging_buffer, 0, D3D11_MAP_READ, 0, Some(&mut mapped))
            .expect("Map staging buffer failed");

        let slice = std::slice::from_raw_parts(mapped.pData as *const u32, element_count as usize);

        for (i, &val) in slice.iter().enumerate() {
            let expected = (i as u32) * (i as u32) + 10;
            assert_eq!(val, expected, "GPU calculation mismatch at index {}", i);
        }

        ctx.context.Unmap(&staging_buffer, 0);
    }
}
