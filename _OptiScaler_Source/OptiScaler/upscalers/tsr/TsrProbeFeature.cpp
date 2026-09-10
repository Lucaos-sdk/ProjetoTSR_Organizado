#include "pch.h"
#include "TsrProbeFeature.h"
#include "TsrNgxAdapter.h"

void TsrProbeFeature::Capture(const NVSDK_NGX_Parameter* p)
{
    LOG_INFO("TSR_PROBE v1 evaluation={} renderer=FSR2.1.2 custom_temporal=false neural=false flags={} render={}x{} target={}x{} display={}x{}",
             evaluations,GetFeatureFlags(),RenderWidth(),RenderHeight(),TargetWidth(),TargetHeight(),DisplayWidth(),DisplayHeight());
    if(!p){LOG_WARN("TSR_PROBE missing parameters");return;}
    for(const char* name:{NVSDK_NGX_Parameter_Color,NVSDK_NGX_Parameter_Depth,NVSDK_NGX_Parameter_MotionVectors,NVSDK_NGX_Parameter_Output,NVSDK_NGX_Parameter_ExposureTexture}) {
        try {
            auto* resource=tsr::integration::NgxResource(*p,name);
            const auto d=resource->GetDesc();
            LOG_INFO("TSR_PROBE resource={} size={}x{} format={} dimension={} mips={} layers={} samples={} flags={}",
                name,d.Width,d.Height,int(d.Format),int(d.Dimension),d.MipLevels,d.DepthOrArraySize,d.SampleDesc.Count,unsigned(d.Flags));
        }catch(const std::invalid_argument&){LOG_INFO("TSR_PROBE resource={} missing",name);}
    }
    for(const char* name:{NVSDK_NGX_Parameter_Jitter_Offset_X,NVSDK_NGX_Parameter_Jitter_Offset_Y,
        NVSDK_NGX_Parameter_MV_Scale_X,NVSDK_NGX_Parameter_MV_Scale_Y,NVSDK_NGX_Parameter_DLSS_Pre_Exposure,
        "FSR.cameraNear","FSR.cameraFar"}) {
        float value=0;const bool available=p->Get(name,&value)==NVSDK_NGX_Result_Success;
        LOG_INFO("TSR_PROBE scalar={} available={} value={}",name,available,value);
    }
    for(const char* name:{NVSDK_NGX_Parameter_Reset,NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,
        NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,
        NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_X,
        NVSDK_NGX_Parameter_DLSS_Input_Depth_Subrect_Base_Y,NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_X,
        NVSDK_NGX_Parameter_DLSS_Input_MV_SubrectBase_Y,NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_X,
        NVSDK_NGX_Parameter_DLSS_Output_Subrect_Base_Y}) {
        unsigned value=0;const bool available=p->Get(name,&value)==NVSDK_NGX_Result_Success;
        LOG_INFO("TSR_PROBE integer={} available={} value={}",name,available,value);
    }
    auto& cfg=*Config::Instance();
    LOG_INFO("TSR_PROBE configured_barriers color={} depth={} motion={} (-1 means unknown, not measured GPU state)",
        cfg.ColorResourceBarrier.has_value()?int(cfg.ColorResourceBarrier.value()):-1,
        cfg.DepthResourceBarrier.has_value()?int(cfg.DepthResourceBarrier.value()):-1,
        cfg.MVResourceBarrier.has_value()?int(cfg.MVResourceBarrier.value()):-1);
}

bool TsrProbeFeature::EvaluateInternal(ID3D12GraphicsCommandList* list,NVSDK_NGX_Parameter* parameters)
{
    ++evaluations;
    const bool capture=snapshots<64 && (evaluations<=3 || evaluations==60 || evaluations==180 || evaluations%600==0);
    if(capture){++snapshots;Capture(parameters);} // Includes gameplay after loading; bounded output.
    const bool result=FSR2FeatureDx12_212::EvaluateInternal(list,parameters);
    if(capture || !result)LOG_INFO("TSR_PROBE evaluation={} fsr_evaluate_returned={}",evaluations,result);
    return result;
}
