#pragma once
#include "../integration/ngx_frame_adapter.h"
#include <unordered_map>
#include <variant>
// Test double of the actual vendored NGX virtual interface, with exact types.
struct TestNgxParameters final : NVSDK_NGX_Parameter {
    using Value=std::variant<unsigned long long,float,double,unsigned,int,ID3D11Resource*,ID3D12Resource*,void*>;
    std::unordered_map<std::string,Value> values;
    mutable unsigned writes=0;
    template<class T> NVSDK_NGX_Result Read(const char* key,T* out) const {
        const auto it=values.find(key);
        if(it==values.end())return NVSDK_NGX_Result_FAIL_InvalidParameter;
        const auto* value=std::get_if<T>(&it->second);
        if(!value)return NVSDK_NGX_Result_FAIL_InvalidParameter;
        *out=*value;return NVSDK_NGX_Result_Success;
    }
#define TSR_TEST_NGX_TYPE(T) void Set(const char* k,T v) override {++writes;values[k]=v;} \
    NVSDK_NGX_Result Get(const char* k,T* v) const override {return Read(k,v);}
    TSR_TEST_NGX_TYPE(unsigned long long)
    TSR_TEST_NGX_TYPE(float)
    TSR_TEST_NGX_TYPE(double)
    TSR_TEST_NGX_TYPE(unsigned)
    TSR_TEST_NGX_TYPE(int)
    TSR_TEST_NGX_TYPE(ID3D11Resource*)
    TSR_TEST_NGX_TYPE(ID3D12Resource*)
    TSR_TEST_NGX_TYPE(void*)
#undef TSR_TEST_NGX_TYPE
    void Reset() override {++writes;values.clear();}
};
inline TestNgxParameters MakeNgxParameters(const tsr::FrameParameters& frame,ID3D12Resource* color,
    ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* output) {
    TestNgxParameters p;
    p.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes));
    p.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width,frame.renderSize.width);
    p.Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height,frame.renderSize.height);
    p.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,frame.jitterPixels[0]);
    p.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,frame.jitterPixels[1]);
    p.Set(NVSDK_NGX_Parameter_MV_Scale_X,1.f);p.Set(NVSDK_NGX_Parameter_MV_Scale_Y,1.f);
    p.Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,frame.preExposure);
    p.Set(NVSDK_NGX_Parameter_Reset,unsigned(frame.reset));
    p.Set(NVSDK_NGX_Parameter_Color,color);p.Set(NVSDK_NGX_Parameter_Depth,depth);
    p.Set(NVSDK_NGX_Parameter_MotionVectors,motion);p.Set(NVSDK_NGX_Parameter_Output,output);
    return p;
}
