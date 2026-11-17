#pragma once

#include "lab.hpp"

#include <d3d11_3.h>
#include <dxgi1_6.h>

// Lab-only Direct3D 11 / DXGI helpers. Every route that uses them needs a
// GPU API symbol to obtain its buffer; that is what the study records.
namespace lab {

struct Gpu final {
    ID3D11Device* device{};
    ID3D11DeviceContext* context{};
    IDXGIDevice* dxgi_device{};
    IDXGIFactory2* factory{};
    D3D_DRIVER_TYPE driver{};
};

// Creates a BGRA-capable D3D11 device: hardware first, then WARP when
// `allow_warp`. With `details`, reports the adapter, feature level,
// unified memory, MapOnDefaultTextures and tearing support under `route`.
bool create_gpu(const char* route, Gpu& gpu, bool allow_warp, bool details);
void release_gpu(Gpu& gpu);

// A BGRA texture of window_width x window_height with the given usage,
// CPU access, bind and misc flags.
HRESULT create_texture(const Gpu& gpu, D3D11_USAGE usage, UINT cpu_access, UINT bind, UINT misc,
                       ID3D11Texture2D** texture);
// Writes the pattern into a texture through a CPU-writable dynamic texture
// and CopySubresourceRegion (a GPU copy issued by the caller) over `box`
// (whole texture when null).
HRESULT upload_pattern(const Gpu& gpu, ID3D11Texture2D* target, const D3D11_BOX* box, int phase, bool with_alpha);
// A default-usage texture the CPU writes without a GPU copy (needs
// MapOnDefaultTextures): created through ID3D11Device3::CreateTexture2D1
// with `layout` (undefined or row major), CPU write access, shader-resource
// binding and `misc`.
HRESULT create_cpu_texture(const Gpu& gpu, D3D11_TEXTURE_LAYOUT layout, UINT misc, ID3D11Texture2D** texture);
// Writes the pattern into such a texture: Map(D3D11_MAP_WRITE) and the
// returned pointer for row-major layout, else Map with no pointer and
// ID3D11Device3::WriteToSubresource from CPU memory.
HRESULT cpu_write_texture(const Gpu& gpu, ID3D11Texture2D* texture, D3D11_TEXTURE_LAYOUT layout, int phase,
                          bool with_alpha);
// Reads one pixel of `source` back through a staging copy (probe only).
HRESULT read_pixel(const Gpu& gpu, ID3D11Texture2D* source, int x, int y, std::uint32_t& pixel);

} // namespace lab
