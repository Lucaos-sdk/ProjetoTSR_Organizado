#pragma once
#include "camera_contract.h"

namespace tsr::integration {
struct LightingFrame {
    std::array<float,12> normalTransform{1,0,0,0, 0,1,0,0, 0,0,1,0};
    float smoothing=0;
    float colorTransfer=1; // 0: preserve chromaticity; 1: legacy RGB lighting.
    float sceneryProtection=0; // Legacy test/reference default; enabled by game controls.
    float fadeStart=20,fadeEnd=80; // Camera-space units, not inferred meters.
};
inline bool ValidLightingFrame(const LightingFrame& frame) {
    if(!std::isfinite(frame.smoothing)||frame.smoothing<0||frame.smoothing>1)return false;
    if(!std::isfinite(frame.colorTransfer)||frame.colorTransfer<0||frame.colorTransfer>1)return false;
    if(!std::isfinite(frame.sceneryProtection)||frame.sceneryProtection<0||frame.sceneryProtection>1||
       !std::isfinite(frame.fadeStart)||!std::isfinite(frame.fadeEnd)||frame.fadeStart<0||frame.fadeEnd<=frame.fadeStart)return false;
    for(float x:frame.normalTransform)if(!std::isfinite(x))return false;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){float dot=0;
        for(int k=0;k<3;++k)dot+=frame.normalTransform[i*4+k]*frame.normalTransform[j*4+k];
        if(std::abs(dot-(i==j?1.f:0.f))>1e-3f)return false;}
    return true;
}
inline std::optional<LightingFrame> MakeLightingFrame(const CameraSample& current,const CameraSample& anchor,float smoothing) {
    if(!ValidCameraBasis(current)||!ValidCameraBasis(anchor))return {};
    LightingFrame result;result.smoothing=smoothing;
    for(int i=0;i<3;++i){CameraVector unit{};unit[i]=1;
        auto transformed=CameraNormalInReference(unit,current,anchor);
        for(int row=0;row<3;++row)result.normalTransform[row*4+i]=transformed[row];}
    if(!ValidLightingFrame(result))return {};
    return result;
}
}
