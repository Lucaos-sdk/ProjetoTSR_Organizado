#pragma once
#include "frame_contract.h"

namespace tsr {
struct CameraPosition { double x, y, z; };
// Analytic pinhole fixture, static plane at world Z=10; camera translates without
// rotation. Positions/rays are evaluated in double independently of the shader.
inline FrameInput CameraPlane(Size size, uint64_t index, CameraPosition camera, CameraPosition previous,
                              bool reset, Motion jitter = {0,0}) {
    auto frame = FrameFixture(size, {size.width*2,size.height*2});
    auto& p = frame.parameters;
    p.frameIndex = index;
    p.reset = reset;
    p.jitterPixels = jitter;
    p.depthReprojection = DepthReprojection::PredictedPreviousZ;
    frame.predictedPreviousDepth.resize(Count(size));
    const double fx = size.width * .5, fy = size.height * .5;
    const double cx = (size.width-1)*.5, cy = (size.height-1)*.5;
    const double z = 10-camera.z, previousZ = 10-previous.z;
    if (z <= 0) throw std::invalid_argument("Fixture plane behind current camera");
    for (uint32_t y=0;y<size.height;++y) for (uint32_t x=0;x<size.width;++x) {
        const size_t i=size_t(y)*size.width+x;
        const double worldX = camera.x + (x+jitter[0]-cx)*z/fx;
        const double worldY = camera.y + (y+jitter[1]-cy)*z/fy;
        frame.color[i] = {float(worldX+12),float(worldY+12),4,.5f};
        frame.depth[i] = float(z);
        frame.predictedPreviousDepth[i] = float(std::max(0.,previousZ));
        frame.motion[i] = previousZ > 0 ? Motion{
            float((worldX-previous.x)/previousZ*fx+cx-(x+jitter[0])),
            float((worldY-previous.y)/previousZ*fy+cy-(y+jitter[1]))} : Motion{0,0};
    }
    return frame;
}
}
