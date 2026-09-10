#pragma once
#include "temporal_reference.h"
#include "camera_fixture.h"
#include <iostream>

namespace tsr {
inline void TemporalRequire(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
inline FrameInput ConstantFrame(Size size, uint64_t index, float value, bool reset = false) {
    auto f = FrameFixture(size, {size.width * 2, size.height * 2});
    f.parameters.frameIndex = index;
    f.parameters.reset = reset;
    for (auto& color : f.color) color = {value, value, value, .25f};
    return f;
}
// The callback selects one of two independent contexts. Known values below are
// additional oracles: GPU/CPU agreement alone is not sufficient.
template<class Run> void TemporalCases(Run run, bool full) {
    auto f = ConstantFrame({7,5}, 0, 10, true);
    auto r = run(0, f, "first/reset");
    TemporalRequire(r.color[0][0] == 10 && r.geometry[0][3] == 0, "First frame read history");
    run(1, ConstantFrame({7,5}, 0, 100, true), "second instance");
    f = ConstantFrame({7,5}, 1, 2);
    r = run(0, f, "static blend");
    TemporalRequire(r.color[0][0] == 8 && r.color[0][3] == .25f && r.geometry[0][3] == 1, "Static blend/alpha wrong");
    r = run(1, ConstantFrame({7,5}, 1, 20), "instance isolation");
    TemporalRequire(r.color[0][0] == 80, "History crossed instances");
    f = ConstantFrame({7,5}, 2, 16);
    f.parameters.preExposure = 2;
    r = run(0, f, "exposure change");
    TemporalRequire(r.color[0][0] == 16 && r.color[0][3] == .25f, "Exposure normalization wrong");
    f = ConstantFrame({7,5}, 3, 1, true);
    r = run(0, f, "explicit reset");
    TemporalRequire(r.color[0][0] == 1 && r.geometry[0][3] == 0, "Reset failed");
    f = ConstantFrame({7,5}, 5, 3);
    r = run(0, f, "index gap");
    TemporalRequire(r.color[0][0] == 3 && r.geometry[0][3] == 0, "Index gap failed");
    f = ConstantFrame({9,3}, 6, 4);
    r = run(0, f, "render resize");
    TemporalRequire(r.geometry[0][3] == 0, "Resize reused history");
    f.parameters.frameIndex = 7;
    f.parameters.outputSize = {17, 7};
    r = run(0, f, "output resize");
    TemporalRequire(r.geometry[0][3] == 0, "Output resize reused history");
    f = ConstantFrame({7,5}, 0, 0, true);
    for (uint32_t y=0; y<5; ++y) for (uint32_t x=0; x<7; ++x)
        f.color[size_t(y)*7+x] = {float(x + 10*y), -2, 64, .5f};
    run(0, f, "translation seed");
    f = ConstantFrame({7,5}, 1, 0);
    for (auto& mv : f.motion) mv = {-1, 1};
    r = run(0, f, "signed XY translation/offscreen");
    TemporalRequire(r.color[2][0] == 8.25f && r.geometry[0][3] == 0 && r.geometry[30][3] == 0,
                    "Motion sign or bounds wrong");
    f = ConstantFrame({7,5}, 0, 0, true);
    for (uint32_t x=0; x<7; ++x) for (uint32_t y=0; y<5; ++y) f.color[size_t(y)*7+x][0] = float(x);
    f.parameters.jitterPixels = {-.25f, 0};
    run(0, f, "jitter seed");
    f = ConstantFrame({7,5}, 1, 0);
    f.parameters.jitterPixels = {.25f, 0};
    r = run(0, f, "fractional jitter");
    TemporalRequire(r.color[2][0] == 1.875f && r.geometry[6][3] == 0, "Jitter sign/fraction wrong");
    f = ConstantFrame({7,5}, 0, 12, true);
    f.depth[3] = 1;
    run(0, f, "depth edge seed");
    f = ConstantFrame({7,5}, 1, 2);
    f.motion[2] = {.5f, 0};
    r = run(0, f, "disocclusion/all contributing taps");
    TemporalRequire(r.color[2][0] == 2 && r.geometry[2][3] == 0 && r.geometry[3][3] == 0 && r.geometry[1][3] == 1,
                    "Depth edge leaked history");
    // Repeated ping-pong reuse; non-power-of-two widths exercise row pitches.
    for (uint64_t i=2; i<10; ++i) {
        f = ConstantFrame({7,5}, i, float(i));
        run(0, f, "ping-pong sequence");
    }
    f.parameters.frameIndex = 10;
    f.motion[0][0] = std::numeric_limits<float>::quiet_NaN();
    bool rejected = false;
    try { run(0, f, "invalid input"); } catch (const std::invalid_argument&) { rejected = true; }
    TemporalRequire(rejected, "Invalid temporal frame accepted");
    f.motion[0][0] = 0;
    r = run(0, f, "after rejected input");
    TemporalRequire(r.geometry[0][3] == 1, "Rejected frame changed history state");
    f = ConstantFrame({1,1}, 0, 4, true);
    run(1, f, "single pixel seed");
    f = ConstantFrame({1,1}, 1, 0);
    r = run(1, f, "single pixel reuse");
    TemporalRequire(r.color[0][0] == 3 && r.geometry[0][3] == 1, "Single pixel reuse wrong");
    // Camera moves toward a static plane: current Z=8, previous Z=10.
    // Reusing current Z would reject every history sample incorrectly.
    f = CameraPlane({9,5},0,{0,0,0},{0,0,0},true);
    run(1,f,"camera plane seed");
    f = CameraPlane({9,5},1,{0,0,2},{0,0,0},false);
    r = run(1,f,"camera forward / predicted depth");
    TemporalRequire(r.geometry[22][3] == 1 && r.color[22][0] == 12 && r.geometry[22][0] == 8,
                    "Camera forward rejected valid previous depth");
    f = CameraPlane({9,5},2,{1,.5,1},{0,0,2},false,{.25f,-.25f});
    r = run(1,f,"camera XYZ translation and jitter");
    TemporalRequire(r.geometry[22][3] == 1, "Camera translation rejected valid center");
    // Occluder present in prior frame; a newly visible background must not reuse it.
    f = CameraPlane({9,5},0,{0,0,0},{0,0,0},true);
    f.depth[22] = 2;
    f.color[22] = {100,100,100,.5f};
    run(1,f,"camera occluder seed");
    f = CameraPlane({9,5},1,{0,0,2},{0,0,0},false);
    r = run(1,f,"camera disocclusion");
    TemporalRequire(r.geometry[22][3] == 0 && r.color[22][0] == 12 && r.geometry[0][3] == 1,
                    "Camera disocclusion leaked foreground");
    f = CameraPlane({9,5},2,{0,0,2},{0,0,2},false);
    f.predictedPreviousDepth[22] = 0;
    r = run(1,f,"invalid previous projection");
    TemporalRequire(r.geometry[22][3] == 0, "Invalid previous projection accepted");
    f = CameraPlane({9,5},3,{0,0,2},{0,0,2},false);
    f.predictedPreviousDepth.clear();
    rejected = false;
    try { run(1,f,"missing predicted depth"); } catch (const std::invalid_argument&) { rejected = true; }
    TemporalRequire(rejected, "Missing camera depth accepted");
    // Independent analytic oracle for the output grid, including sign and scale.
    for (const Motion jitter : {Motion{.5f,-.5f},Motion{-.5f,.5f},Motion{.25f,.25f},Motion{0,0}}) {
        f = ConstantFrame({9,5},0,0,true);
        f.parameters.outputSize = {17,11};
        f.parameters.jitterPixels = jitter;
        for (uint32_t y=0;y<5;++y) for (uint32_t x=0;x<9;++x) {
            const float value = 2*(float(x)+jitter[0]) + 3*(float(y)+jitter[1]) + 10;
            f.color[size_t(y)*9+x] = {value,value,value,1};
        }
        r = run(0,f,"stable output analytic ramp");
        for (uint32_t y=4;y<7;++y) for (uint32_t x=4;x<13;++x) {
            const double value = 2*((x+.5)*9/17-.5) + 3*((y+.5)*5/11-.5) + 10;
            TemporalRequire(std::abs(r.output[size_t(y)*17+x][0]-value)<.00002,"Stable output jitter sign/scale wrong");
        }
    }
    if (full) {
        for (uint64_t i=0; i<3; ++i) {
            f = ConstantFrame({1920,1080}, i, float(i+1), i==0);
            f.color = Fixture(f.parameters.renderSize);
            f.parameters.preExposure = i == 1 ? 2.f : 1.f;
            f.parameters.jitterPixels = i == 1 ? Motion{.25f,-.25f} : Motion{0,0};
            for (size_t pixel=0; pixel<f.color.size(); ++pixel) {
                for (size_t c=0; c<3; ++c) f.color[pixel][c] *= f.parameters.preExposure;
                f.motion[pixel] = {-1.f,.5f};
                // Moving depth edge forces both acceptance and disocclusion.
                f.depth[pixel] = pixel % 1920 < 900+i ? 1.f : 10.f;
            }
            run(0, f, "1080p temporal / 4K spatial");
        }
        run(1,CameraPlane({1920,1080},0,{0,0,0},{0,0,0},true),"1080p camera seed");
        run(1,CameraPlane({1920,1080},1,{.25,.125,2},{0,0,0},false),"1080p moving camera / 4K spatial");
    }
    std::cout << "PASS temporal scenarios and independent expected values\n";
}
}
