#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Dx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"
#include "../../../../native/integration/game_relighting_dx12.h"

class FFXFeatureDx12 : public FFXFeature, public IFeature_Dx12
{
  private:
    ID3D12Resource* smallerColor[2]{};
    std::unique_ptr<tsr::integration::GameRelighting> tsrRelighting;
    bool tsrWasActive=false,tsrFailed=false;
    float tsrLastStrength=0;
    uint64_t tsrCalls=0;
    std::optional<tsr::integration::CameraSample> tsrLightAnchor;
    uint64_t tsrAnchorRevision=0;
    float tsrLastSmoothing=1;
    float tsrLastColorTransfer=0;
    bool tsrLastSceneryProtection=true;
    float tsrLastReach=80;

    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

  protected:
    bool InitFFX(const NVSDK_NGX_Parameter* InParameters);

  public:
    FFXFeatureDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool InitInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool EvaluateInternal(ID3D12GraphicsCommandList* InCommandList, NVSDK_NGX_Parameter* InParameters) override;
    bool QueryProviders(ID3D12Device* device);

    feature_version Version() override { return FFXFeature::Version(); }
    Upscaler GetUpscalerType() const final { return Upscaler::FFX; }
    API Api() const override { return IFeature_Dx12::Api(); }
    bool CallsUpscalerEndByItself() override { return IFeature_Dx12::CallsUpscalerEndByItself(); }

    bool IsWithDx12() final { return false; }

    ~FFXFeatureDx12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_context, NULL);

        SAFE_RELEASE(smallerColor[0]);
        SAFE_RELEASE(smallerColor[1]);
    }
};
