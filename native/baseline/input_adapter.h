#pragma once
#include "frame_contract.h"
#include "texture_region.h"
namespace tsr {
// depth = A + B / positive-view-Z. Linear input bypasses A/B.
struct InputConversion {
    Size size;
    Motion motionScale{1,1};
    Motion jitterRemoval{0,0}; // subtract AFTER converting motion to render pixels
    float depthA=0,depthB=1;
    uint32_t linearDepth=1;
    uint32_t fixedCamera=0; // opt-in ONLY with known fixed camera
    float colorScale=1; // linear input only; alpha is preserved
    uint32_t padding=0;
    uint32_t originX=0,originY=0,pad2=0,pad3=0;
};
static_assert(sizeof(InputConversion)==64);
inline void ValidateConversion(const InputConversion& p) {
    Count(p.size);
    for (auto v:p.motionScale) if (!std::isfinite(v)) throw std::invalid_argument("Invalid motion scale");
    for (auto v:p.jitterRemoval) if (!std::isfinite(v)) throw std::invalid_argument("Invalid jitter removal");
    if (p.linearDepth>1 || p.fixedCamera>1 || !std::isfinite(p.depthA) || !std::isfinite(p.depthB) ||
        (!p.linearDepth && p.depthB==0) || !std::isfinite(p.colorScale) || p.colorScale<=0)
        throw std::invalid_argument("Invalid input conversion");
}
inline Pixel ConvertGeometry(float d,Motion mv,const InputConversion& p) {
    double z=p.linearDepth ? double(d) : double(p.depthB)/(double(d)-p.depthA);
    const double x=double(mv[0])*p.motionScale[0]-p.jitterRemoval[0];
    const double y=double(mv[1])*p.motionScale[1]-p.jitterRemoval[1];
    const bool valid=std::isfinite(d) && (p.linearDepth || (d>=0 && d<=1)) &&
        std::isfinite(float(z)) && z>0 && std::isfinite(float(x)) && std::isfinite(float(y));
    if (!valid) return {0,0,0,0};
    return {float(z),float(x),float(y),p.fixedCamera ? float(z) : 0.f};
}
}
