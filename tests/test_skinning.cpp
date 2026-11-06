#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "hikari/core/d3d11.hpp"
#include "hikari/render/skinning.hpp"

namespace {

class DeviceFixture {
public:
    DeviceFixture() : context(nullptr, 0, 0) {}

    hikari::core::D3D11Context context;
};

} // namespace

TEST_CASE_METHOD(DeviceFixture, "Skinning buffer accepts palettes above 1024 bones",
                 "[d3d11][render]") {
    hikari::render::GpuSkinningBuffer skinning;
    std::vector<hikari::Mat4> palette(5000, hikari::Mat4::identity());

    REQUIRE_NOTHROW(skinning.update(context, palette));
    REQUIRE(skinning.buffer);
    REQUIRE(skinning.srv);

    D3D11_BUFFER_DESC buffer_desc{};
    skinning.buffer->GetDesc(&buffer_desc);
    REQUIRE(buffer_desc.ByteWidth == static_cast<UINT>(palette.size() * sizeof(hikari::Mat4)));
    REQUIRE(buffer_desc.StructureByteStride == sizeof(hikari::Mat4));
    REQUIRE((buffer_desc.MiscFlags & D3D11_RESOURCE_MISC_BUFFER_STRUCTURED) != 0);

    D3D11_SHADER_RESOURCE_VIEW_DESC view_desc{};
    skinning.srv->GetDesc(&view_desc);
    REQUIRE(view_desc.Buffer.NumElements == palette.size());

    palette.resize(64);
    REQUIRE_NOTHROW(skinning.update(context, palette));
    skinning.srv->GetDesc(&view_desc);
    REQUIRE(view_desc.Buffer.NumElements == palette.size());
}
