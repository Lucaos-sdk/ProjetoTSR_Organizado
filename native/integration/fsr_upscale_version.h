#pragma once
// Compatibility extension for the legacy FFX API headers used by OptiScaler.
// ABI from FidelityFX SDK 2.1, Kits/FidelityFX/upscalers/include/ffx_upscale.h.
// Include ffx_api.h before this file. No frame-generation API is changed.
namespace tsr::integration {
struct UpscaleVersion {
    ffxCreateContextDescHeader header{};
    uint32_t version=(4u<<22)|(1u<<12)|1u;
    explicit UpscaleVersion(ffxCreateContextDescHeader* next=nullptr) {
        header.type=0x0001000bu;
        header.pNext=next;
    }
};
}
