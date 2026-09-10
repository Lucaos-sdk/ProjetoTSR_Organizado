#pragma once
#include "../baseline/input_adapter.h"
#include <nvsdk_ngx_params.h>
#include <optional>
#include <string>

namespace tsr::integration {
// Evidence supplied by the integration, never inferred from a game title.
struct NgxConventions {
    Size creationRender, output;
    uint64_t frameIndex=0; // advance on first successful submission, not Evaluate/replay
    bool linearColorConfirmed=false;
    bool linearDepthConfirmed=false;
    std::optional<std::array<float,2>> projectedDepthAB;
    std::optional<Motion> jitterRemoval;
    bool fixedCameraConfirmed=false;
};
struct NgxFrame {
    FrameParameters frame;
    InputConversion conversion;
    OutputRegion region;
    ID3D12Resource *color=nullptr,*depth=nullptr,*motion=nullptr,*output=nullptr;
    bool currentFrameOnly=true;
};
template<class T> inline T NgxRequired(const NVSDK_NGX_Parameter& p,const char* name) {
    T value{};
    if(p.Get(name,&value)!=NVSDK_NGX_Result_Success)throw std::invalid_argument(std::string("Missing NGX parameter: ")+name);
    return value;
}
inline Size NgxPair(const NVSDK_NGX_Parameter& p,const char* x,const char* y,Size fallback) {
    unsigned a=0,b=0;
    const bool ax=p.Get(x,&a)==NVSDK_NGX_Result_Success,by=p.Get(y,&b)==NVSDK_NGX_Result_Success;
    if(ax!=by)throw std::invalid_argument("Incomplete NGX dimension/origin pair");
    return ax?Size{a,b}:fallback;
}
inline ID3D12Resource* NgxResource(const NVSDK_NGX_Parameter& p,const char* name) {
    ID3D12Resource* resource=nullptr;
    if(p.Get(name,&resource)!=NVSDK_NGX_Result_Success) {
        void* raw=nullptr;
        if(p.Get(name,&raw)!=NVSDK_NGX_Result_Success)throw std::invalid_argument(std::string("Missing NGX texture: ")+name);
        resource=static_cast<ID3D12Resource*>(raw);
    }
    if(!resource)throw std::invalid_argument(std::string("Null NGX texture: ")+name);
    return resource;
}
// Read-only: no NGX Set, barriers, descriptors, command recording or history mutation.
// Resource pointers are borrowed; GpuFrameContext validates/retains real textures.
inline NgxFrame ReadNgxFrame(const NVSDK_NGX_Parameter& p,const NgxConventions& c) {
    if(!c.linearColorConfirmed)throw std::invalid_argument("Linear color convention not confirmed");
    Count(c.creationRender);Count(c.output);
    if(c.linearDepthConfirmed && c.projectedDepthAB)throw std::invalid_argument("Ambiguous depth convention");
    if(!c.linearDepthConfirmed && !c.projectedDepthAB)throw std::invalid_argument("Depth projection is unknown");
    const int flags=NgxRequired<int>(p,NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags);
    if(flags<0)throw std::invalid_argument("Invalid NGX feature flags");
    NgxFrame result{};
    result.frame.renderSize=NgxPair(p,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
        NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,c.creationRender);
    result.frame.outputSize=c.output;
    Count(result.frame.renderSize);
    if(result.frame.renderSize.width>c.output.width || result.frame.renderSize.height>c.output.height)
        throw std::invalid_argument("Downsampling unsupported");
    if(!(flags&NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) && !SameSize(result.frame.renderSize,c.output))
        throw std::invalid_argument("Output-resolution motion vectors unsupported");
    result.frame.jitterPixels={NgxRequired<float>(p,NVSDK_NGX_Parameter_Jitter_Offset_X),NgxRequired<float>(p,NVSDK_NGX_Parameter_Jitter_Offset_Y)};
    for(float j:result.frame.jitterPixels)if(!std::isfinite(j) || std::abs(j)>.5f)throw std::invalid_argument("Unsupported NGX jitter");
    result.frame.preExposure=NgxRequired<float>(p,NVSDK_NGX_Parameter_DLSS_Pre_Exposure);
    if(!std::isfinite(result.frame.preExposure) || result.frame.preExposure<=0)throw std::invalid_argument("Invalid NGX pre-exposure");
    const unsigned reset=NgxRequired<unsigned>(p,NVSDK_NGX_Parameter_Reset);
    if(reset>1)throw std::invalid_argument("Invalid NGX reset");
    result.frame.frameIndex=c.frameIndex;
    result.currentFrameOnly=!c.fixedCameraConfirmed;
    result.frame.reset=reset!=0 || c.frameIndex==0 || result.currentFrameOnly;
    result.frame.depthReprojection=c.fixedCameraConfirmed?DepthReprojection::FixedCamera:DepthReprojection::PredictedPreviousZ;
    result.conversion.size=result.frame.renderSize;
    result.conversion.fixedCamera=c.fixedCameraConfirmed?1:0;
    result.conversion.linearDepth=c.linearDepthConfirmed?1:0;
    if(c.projectedDepthAB){result.conversion.depthA=(*c.projectedDepthAB)[0];result.conversion.depthB=(*c.projectedDepthAB)[1];}
    result.conversion.motionScale={NgxRequired<float>(p,NVSDK_NGX_Parameter_MV_Scale_X),NgxRequired<float>(p,NVSDK_NGX_Parameter_MV_Scale_Y)};
    if(flags&NVSDK_NGX_DLSS_Feature_Flags_MVJittered) {
        if(!c.jitterRemoval)throw std::invalid_argument("Jittered motion requires explicit correction");
        result.conversion.jitterRemoval=*c.jitterRemoval;
    } else if(c.jitterRemoval)throw std::invalid_argument("Jitter correction supplied for unjittered motion");
    const auto colorOrigin=NgxPair(p,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,{0,0});
    const auto depthOrigin=NgxPair(p,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,{0,0});
    const auto motionOrigin=NgxPair(p,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y,{0,0});
    if(!SameSize(colorOrigin,depthOrigin) || !SameSize(colorOrigin,motionOrigin))throw std::invalid_argument("Different input texture origins unsupported");
    result.conversion.originX=colorOrigin.width;result.conversion.originY=colorOrigin.height;
    const auto outputOrigin=NgxPair(p,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y,{0,0});
    result.region={c.output,0,0,outputOrigin.width,outputOrigin.height};
    ValidateConversion(result.conversion);
    result.color=NgxResource(p,NVSDK_NGX_Parameter_Color);
    result.depth=NgxResource(p,NVSDK_NGX_Parameter_Depth);
    result.motion=NgxResource(p,NVSDK_NGX_Parameter_MotionVectors);
    result.output=NgxResource(p,NVSDK_NGX_Parameter_Output);
    if(result.output==result.color || result.output==result.depth || result.output==result.motion)
        throw std::invalid_argument("NGX input/output alias unsupported");
    return result;
}
}
