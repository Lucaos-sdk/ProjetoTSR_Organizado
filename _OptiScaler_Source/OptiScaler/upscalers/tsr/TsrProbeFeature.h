#pragma once
#include "upscalers/fsr2_212/FSR2Feature_Dx12_212.h"

// First-game data capture. Rendering remains the existing FSR 2.1.2 path.
class TsrProbeFeature final : public FSR2FeatureDx12_212 {
    uint64_t evaluations=0;
    unsigned snapshots=0;
    void Capture(const NVSDK_NGX_Parameter* parameters);
public:
    TsrProbeFeature(unsigned handle,NVSDK_NGX_Parameter* parameters)
        :IFeature(handle,parameters),FSR2FeatureDx12_212(handle,parameters){}
    Upscaler GetUpscalerType() const override {return Upscaler::TSRProbe;}
    bool EvaluateInternal(ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* parameters) override;
};
