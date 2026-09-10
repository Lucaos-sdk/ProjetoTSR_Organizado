#include "test_ngx_parameters.h"
#include <iostream>
using namespace tsr;
using namespace tsr::integration;
void Require(bool ok){if(!ok)throw std::runtime_error("NGX adapter assertion failed");}
template<class F> void Reject(F f){bool caught=false;try{f();}catch(const std::invalid_argument&){caught=true;}Require(caught);}
int main(){try{
    int identities[4];
    auto p=MakeNgxParameters({{1280,720},{1920,1080},{.25f,-.25f},2.f,3,false},
        reinterpret_cast<ID3D12Resource*>(&identities[0]),reinterpret_cast<ID3D12Resource*>(&identities[1]),
        reinterpret_cast<ID3D12Resource*>(&identities[2]),reinterpret_cast<ID3D12Resource*>(&identities[3]));
    NgxConventions c{{1280,720},{1920,1080}};c.frameIndex=3;c.linearColorConfirmed=c.linearDepthConfirmed=true;
    const unsigned writes=p.writes;
    auto mapped=ReadNgxFrame(p,c);
    Require(mapped.currentFrameOnly && mapped.frame.reset && mapped.conversion.fixedCamera==0);
    Require(mapped.frame.preExposure==2 && mapped.frame.jitterPixels[0]==.25f && p.writes==writes);
    c.fixedCameraConfirmed=true;mapped=ReadNgxFrame(p,c);Require(!mapped.currentFrameOnly && !mapped.frame.reset);
    c.frameIndex=0;Require(ReadNgxFrame(p,c).frame.reset);c.frameIndex=3;
    auto bad=p;bad.values.erase(NVSDK_NGX_Parameter_MV_Scale_X);Reject([&]{ReadNgxFrame(bad,c);});
    bad=p;bad.Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure,0.f);Reject([&]{ReadNgxFrame(bad,c);});
    bad=p;bad.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,std::numeric_limits<float>::quiet_NaN());Reject([&]{ReadNgxFrame(bad,c);});
    bad=p;bad.values.erase(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height);Reject([&]{ReadNgxFrame(bad,c);});
    bad=p;bad.values.erase(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height);bad.values.erase(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width);
    Require(ReadNgxFrame(bad,c).frame.renderSize.width==1280);
    bad=p;bad.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,0);Reject([&]{ReadNgxFrame(bad,c);});
    bad=p;bad.Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,int(NVSDK_NGX_DLSS_Feature_Flags_MVLowRes|NVSDK_NGX_DLSS_Feature_Flags_MVJittered));
    Reject([&]{ReadNgxFrame(bad,c);});c.jitterRemoval=Motion{.1f,-.2f};Require(ReadNgxFrame(bad,c).conversion.jitterRemoval[1]==-.2f);c.jitterRemoval.reset();
    bad=p;bad.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,1u);bad.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,2u);Reject([&]{ReadNgxFrame(bad,c);});
    bad=p;bad.Set(NVSDK_NGX_Parameter_Color,static_cast<void*>(&identities[0]));Require(ReadNgxFrame(bad,c).color==mapped.color);
    bad=p;bad.Set(NVSDK_NGX_Parameter_Output,mapped.color);Reject([&]{ReadNgxFrame(bad,c);});
    c.linearDepthConfirmed=false;Reject([&]{ReadNgxFrame(p,c);});
    c.projectedDepthAB=std::array<float,2>{0.f,1.f};mapped=ReadNgxFrame(p,c);
    Require(std::abs(ConvertGeometry(.25f,{0,0},mapped.conversion)[0]-4.f)<1e-6f);
    c.linearColorConfirmed=false;Reject([&]{ReadNgxFrame(p,c);});
    std::cout<<"PASS NGX virtual interface, conservative camera, exposure, dimensions, motion, depth, resource fallback and read-only mapping\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
