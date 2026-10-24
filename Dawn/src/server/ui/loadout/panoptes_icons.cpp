#include "panoptes_icons.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <limits>
#include <span>
#include <string_view>
#include <vector>
#include <wincodec.h>

#include "../../../client/hooks/graphics/renderer/state.h"
#include "../../../../resources/resource.h"

namespace dawn::server::ui::loadout::icons {
namespace {

constexpr std::array<std::byte, 8> kMagic{std::byte{'S'}, std::byte{'D'}, std::byte{'2'},
                                          std::byte{'I'}, std::byte{'C'}, std::byte{'O'},
                                          std::byte{'N'}, std::byte{'1'}};
constexpr std::array<std::byte, 8> kNameMagic{std::byte{'S'}, std::byte{'D'}, std::byte{'2'},
                                              std::byte{'N'}, std::byte{'A'}, std::byte{'M'},
                                              std::byte{'E'}, std::byte{'1'}};
constexpr std::size_t kEntrySize = 12;
constexpr std::size_t kCacheCapacity = 512;

struct CacheEntry {
    std::uint32_t hash{};
    ID3D11ShaderResourceView* view{};
};

std::array<CacheEntry, kCacheCapacity> g_cache{};
std::size_t g_replace{};
ID3D11Device* g_device{};
IWICImagingFactory* g_factory{};

[[nodiscard]] HMODULE module() noexcept {
    HMODULE result{};
    (void)GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&module),
                            &result);
    return result;
}

[[nodiscard]] std::span<const std::byte> resource(WORD id) noexcept {
    const HMODULE owner = module();
    const HRSRC found = owner != nullptr ? FindResourceW(owner, MAKEINTRESOURCEW(id), RT_RCDATA)
                                         : nullptr;
    const HGLOBAL loaded = found != nullptr ? LoadResource(owner, found) : nullptr;
    const DWORD size = found != nullptr ? SizeofResource(owner, found) : 0;
    const void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
    return data != nullptr && size != 0
               ? std::span<const std::byte>(static_cast<const std::byte*>(data), size)
               : std::span<const std::byte>{};
}

[[nodiscard]] std::uint32_t read_u32(std::span<const std::byte> bytes,
                                     std::size_t offset) noexcept {
    if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t)) {
        return 0;
    }
    const auto* value = reinterpret_cast<const std::uint8_t*>(bytes.data() + offset);
    return static_cast<std::uint32_t>(value[0])
           | (static_cast<std::uint32_t>(value[1]) << 8)
           | (static_cast<std::uint32_t>(value[2]) << 16)
           | (static_cast<std::uint32_t>(value[3]) << 24);
}

[[nodiscard]] std::span<const std::byte> packed(std::uint32_t hash) noexcept {
    const auto pack = resource(IDR_PANOPTES_ICONS);
    if (pack.size() < 12 || !std::equal(kMagic.begin(), kMagic.end(), pack.begin())) {
        return {};
    }
    const std::size_t count = read_u32(pack, 8);
    if (count > (pack.size() - 12) / kEntrySize) {
        return {};
    }
    std::size_t low = 0;
    std::size_t high = count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        const std::size_t at = 12 + middle * kEntrySize;
        const std::uint32_t candidate = read_u32(pack, at);
        if (candidate < hash) {
            low = middle + 1;
        } else if (candidate > hash) {
            high = middle;
        } else {
            const std::size_t offset = read_u32(pack, at + 4);
            const std::size_t length = read_u32(pack, at + 8);
            return offset <= pack.size() && length <= pack.size() - offset
                       ? pack.subspan(offset, length)
                       : std::span<const std::byte>{};
        }
    }
    return {};
}

[[nodiscard]] bool factory() noexcept {
    if (g_factory != nullptr) {
        return true;
    }
    // The render thread is owned by the game and may not have entered COM yet. An already chosen
    // apartment returns RPC_E_CHANGED_MODE, in which case COM is still ready for CoCreateInstance.
    (void)CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    return SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory,
                                      nullptr,
                                      CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&g_factory)));
}

template <typename Interface> void release(Interface*& value) noexcept {
    if (value != nullptr) {
        value->Release();
        value = nullptr;
    }
}

[[nodiscard]] ID3D11ShaderResourceView* decode(std::span<const std::byte> bytes) noexcept {
    if (bytes.empty() || bytes.size() > static_cast<std::size_t>((std::numeric_limits<DWORD>::max)())
        || g_device == nullptr || !factory()) {
        return nullptr;
    }
    IWICStream* stream{};
    IWICBitmapDecoder* decoder{};
    IWICBitmapFrameDecode* frame{};
    IWICFormatConverter* converter{};
    ID3D11Texture2D* texture{};
    ID3D11ShaderResourceView* view{};
    bool okay = SUCCEEDED(g_factory->CreateStream(&stream))
                && SUCCEEDED(stream->InitializeFromMemory(
                    reinterpret_cast<BYTE*>(const_cast<std::byte*>(bytes.data())),
                    static_cast<DWORD>(bytes.size())))
                && SUCCEEDED(g_factory->CreateDecoderFromStream(
                    stream, nullptr, WICDecodeMetadataCacheOnDemand, &decoder))
                && SUCCEEDED(decoder->GetFrame(0, &frame))
                && SUCCEEDED(g_factory->CreateFormatConverter(&converter))
                && SUCCEEDED(converter->Initialize(frame,
                                                   GUID_WICPixelFormat32bppRGBA,
                                                   WICBitmapDitherTypeNone,
                                                   nullptr,
                                                   0.0,
                                                   WICBitmapPaletteTypeCustom));
    UINT width{};
    UINT height{};
    okay = okay && SUCCEEDED(converter->GetSize(&width, &height)) && width != 0 && height != 0;
    std::vector<std::uint8_t> pixels;
    if (okay) {
        pixels.resize(static_cast<std::size_t>(width) * height * 4);
        okay = SUCCEEDED(converter->CopyPixels(
            nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    }
    if (okay) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_IMMUTABLE;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        const D3D11_SUBRESOURCE_DATA data{pixels.data(), width * 4, 0};
        okay = SUCCEEDED(g_device->CreateTexture2D(&description, &data, &texture))
               && SUCCEEDED(g_device->CreateShaderResourceView(texture, nullptr, &view));
    }
    release(texture);
    release(converter);
    release(frame);
    release(decoder);
    release(stream);
    return okay ? view : nullptr;
}

void reset_for_device(ID3D11Device* device) noexcept {
    if (g_device == device) {
        return;
    }
    for (CacheEntry& entry : g_cache) {
        release(entry.view);
        entry = {};
    }
    g_replace = 0;
    g_device = device;
}

} // namespace

std::string_view name(std::uint32_t hash) noexcept {
    const auto pack = resource(IDR_PANOPTES_NAMES);
    if (pack.size() < 12 || !std::equal(kNameMagic.begin(), kNameMagic.end(), pack.begin())) {
        return {};
    }
    const std::size_t count = read_u32(pack, 8);
    if (count > (pack.size() - 12) / kEntrySize) {
        return {};
    }
    std::size_t low = 0;
    std::size_t high = count;
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2;
        const std::size_t at = 12 + middle * kEntrySize;
        const std::uint32_t candidate = read_u32(pack, at);
        if (candidate < hash) {
            low = middle + 1;
        } else if (candidate > hash) {
            high = middle;
        } else {
            const std::size_t offset = read_u32(pack, at + 4);
            const std::size_t length = read_u32(pack, at + 8);
            if (offset > pack.size() || length > pack.size() - offset) {
                return {};
            }
            return {reinterpret_cast<const char*>(pack.data() + offset), length};
        }
    }
    return {};
}

ImTextureID texture(std::uint32_t hash) noexcept {
    reset_for_device(client::hooks::graphics::renderer::g_resources.device);
    if (g_device == nullptr) {
        return {};
    }
    for (const CacheEntry& entry : g_cache) {
        if (entry.view != nullptr && entry.hash == hash) {
            return reinterpret_cast<ImTextureID>(entry.view);
        }
    }
    auto bytes = packed(hash);
    if (bytes.empty()) {
        bytes = resource(IDR_PANOPTES_UNKNOWN);
    }
    ID3D11ShaderResourceView* view = decode(bytes);
    if (view == nullptr) {
        return {};
    }
    CacheEntry& entry = g_cache[g_replace++ % g_cache.size()];
    release(entry.view);
    entry = {hash, view};
    return reinterpret_cast<ImTextureID>(view);
}

void shutdown() noexcept {
    reset_for_device(nullptr);
    release(g_factory);
}

} // namespace dawn::server::ui::loadout::icons
