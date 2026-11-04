#include "hikari/core/com.hpp"

#include <windows.h>

namespace hikari::core {
namespace {

HRESULT STDMETHODCALLTYPE buffer_query_interface(void* self, const IID*, void** out) {
    if (out == nullptr) {
        return E_POINTER;
    }
    *out = self;
    static_cast<HikariBuffer*>(self)->ref_count.fetch_add(1, std::memory_order_relaxed);
    return S_OK;
}

ULONG STDMETHODCALLTYPE buffer_add_ref(void* self) {
    return static_cast<HikariBuffer*>(self)->ref_count.fetch_add(1, std::memory_order_relaxed) + 1;
}

ULONG STDMETHODCALLTYPE buffer_release(void* self) {
    auto* buffer = static_cast<HikariBuffer*>(self);
    const ULONG remaining = buffer->ref_count.fetch_sub(1, std::memory_order_acq_rel) - 1;
    if (remaining == 0) {
        delete buffer;
    }
    return remaining;
}

void* STDMETHODCALLTYPE buffer_get_pointer(void* self) {
    return static_cast<HikariBuffer*>(self)->data.data();
}

SIZE_T STDMETHODCALLTYPE buffer_get_size(void* self) {
    return static_cast<HikariBuffer*>(self)->data.size();
}

const ID3DXBufferVtbl g_buffer_vtbl = {
    buffer_query_interface,
    buffer_add_ref,
    buffer_release,
    buffer_get_pointer,
    buffer_get_size,
};

} // namespace

HikariBuffer* create_hikari_buffer(std::vector<std::uint8_t> data) {
    auto* buffer = new HikariBuffer();
    buffer->vtbl = &g_buffer_vtbl;
    buffer->data = std::move(data);
    return buffer;
}

} // namespace hikari::core
