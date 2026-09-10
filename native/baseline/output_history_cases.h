#pragma once
#include "temporal_cases.h"
#include "output_history_reference.h"
namespace tsr {
template<class Run> void OutputHistoryCases(Run run,bool full) {
    auto f=ConstantFrame({7,5},0,10,true);
    auto r=run(0,f,"output history first frame");
    TemporalRequire(std::abs(r.output[45][0]-10)<1e-5 && r.geometry[45][3]==0,"Output history initialization");
    run(1,ConstantFrame({7,5},0,100,true),"output history other instance");
    r=run(0,ConstantFrame({7,5},1,2),"output weighted blend");
    TemporalRequire(std::abs(r.output[45][0]-float(2.75/.475))<1e-5,"Output history weighted blend");
    r=run(1,ConstantFrame({7,5},1,20),"output instance isolation");
    TemporalRequire(std::abs(r.output[45][0]-float(27.5/.475))<1e-4,"Output history instance isolation");
    const Motion phases[]={{-.25f,-.25f},{.25f,.25f},{.25f,-.25f},{-.25f,.25f}};
    for (unsigned t=0;t<4;++t) {
        f=ConstantFrame({7,5},t,0,t==0);
        f.parameters.jitterPixels=phases[t];
        for (uint32_t y=0;y<5;++y) for (uint32_t x=0;x<7;++x) {
            const float value=std::abs(float(x)+phases[t][0]-1.25f)<.1f ? 1.f : 0.f;
            f.color[size_t(y)*7+x]={value,value,value,1};
        }
        r=run(0,f,"four-phase thin-line samples");
    }
    TemporalRequire(std::abs(r.output[45][0]-1)<1e-6 && std::abs(r.output[46][0])<1e-6,"Thin detail was lost or spread between samples");
    f=ConstantFrame({7,5},4,3);
    std::fill(f.depth.begin(),f.depth.end(),2.f);
    r=run(0,f,"output disocclusion");
    TemporalRequire(std::abs(r.output[45][0]-3)<1e-5 && r.geometry[45][3]==0,"Output history disocclusion");
    f=ConstantFrame({7,5},5,4,true);
    run(0,f,"output reset");
    f=ConstantFrame({7,5},6,8);
    f.parameters.preExposure=2;
    r=run(0,f,"output exposure");
    TemporalRequire(std::abs(r.output[45][0]-8)<1e-5,"Output history exposure");
    f=ConstantFrame({7,5},8,5);
    r=run(0,f,"output index gap");
    TemporalRequire(r.geometry[45][3]==0,"Output index gap");
    f=ConstantFrame({9,3},9,6);
    r=run(0,f,"output resize");
    TemporalRequire(r.geometry[20][3]==0,"Output resize reused history");
    f=ConstantFrame({9,3},10,6);
    for (auto& mv:f.motion) mv={1000,0};
    r=run(0,f,"output offscreen motion");
    TemporalRequire(r.geometry[20][3]==0,"Offscreen history accepted");
    f=ConstantFrame({9,3},11,6);
    f.parameters.outputSize.width=8;
    bool rejected=false;
    try { run(0,f,"unsupported scale"); } catch (const std::invalid_argument&) { rejected=true; }
    TemporalRequire(rejected,"Unsupported output-history scale accepted");
    f=ConstantFrame({9,3},11,6);
    r=run(0,f,"after rejected scale");
    TemporalRequire(r.geometry[20][3]==1,"Rejected scale advanced history");
    for (const Size output : {Size{9,3},Size{13,5},Size{17,7},Size{21,9}}) {
        for (unsigned t=0;t<3;++t) {
            f=FrameFixture({9,3},output);
            f.parameters.frameIndex=t; f.parameters.reset=t==0;
            f.parameters.jitterPixels=phases[t];
            for (auto& motion:f.motion) motion={-.25f,.125f};
            run(0,f,"native and fractional scales");
        }
    }
    // Dynamic render resolution with a fixed output must reset history, while
    // repeated dimensions and explicit resets must reuse the resource storage.
    unsigned dynamicFrame=0;
    for (const Size render : {Size{9,3},Size{7,2},Size{7,2},Size{9,3}}) {
        f=FrameFixture(render,{21,9});
        f.parameters.frameIndex=dynamicFrame; f.parameters.reset=dynamicFrame==0;
        r=run(0,f,"dynamic render with fixed output");
        if (dynamicFrame!=2)
            for (const auto& g:r.geometry) TemporalRequire(g[3]==0,"Dynamic resolution reused stale history");
        ++dynamicFrame;
    }
    run(1,CameraPlane({9,5},0,{0,0,0},{0,0,0},true),"output camera seed");
    run(1,CameraPlane({9,5},1,{.25,.125,2},{0,0,0},false),"output camera reprojection");
    if (full) for (unsigned t=0;t<4;++t) {
        f=FrameFixture({1920,1080},{3840,2160});
        f.parameters.frameIndex=t; f.parameters.reset=t==0; f.parameters.jitterPixels=phases[t];
        run(0,f,"4K output history");
    }
    std::cout << "PASS output-history scenarios and independent thin-line oracle\n";
}
}
