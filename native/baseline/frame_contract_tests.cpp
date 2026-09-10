#include "frame_contract.h"
#include <iostream>

template<class Edit> void rejects(Edit edit) {
    auto frame = tsr::FrameFixture({3, 2}, {7, 5});
    edit(frame);
    try { tsr::ValidateFrame(frame); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid frame accepted");
}
int main() {
    try {
        auto frame = tsr::FrameFixture({3, 2}, {7, 5});
        tsr::ValidateFrame(frame);
        frame.parameters = {{3, 2}, {7, 5}, {.5f, -.5f}, 2.f, 1, false};
        frame.motion[0] = {-3.f, 2.f};
        frame.color[0] = {-4.f, 8000.f, 0.f, .25f};
        tsr::ValidateFrame(frame);
        rejects([](auto& f) { f.depth.pop_back(); });
        rejects([](auto& f) { f.motion.clear(); });
        rejects([](auto& f) { f.color.clear(); });
        rejects([](auto& f) { f.parameters.renderSize.width = 0; });
        rejects([](auto& f) { f.parameters.outputSize.height = 16385; });
        rejects([](auto& f) { f.parameters.preExposure = 0; });
        rejects([](auto& f) { f.parameters.preExposure = std::numeric_limits<float>::infinity(); });
        rejects([](auto& f) { f.parameters.jitterPixels[0] = .51f; });
        rejects([](auto& f) { f.parameters.reset = false; });
        rejects([](auto& f) { f.depth[0] = -1; });
        rejects([](auto& f) { f.motion[0][1] = std::numeric_limits<float>::quiet_NaN(); });
        rejects([](auto& f) { f.color[0][2] = std::numeric_limits<float>::infinity(); });
        rejects([](auto& f) { f.parameters.depthReprojection = tsr::DepthReprojection::PredictedPreviousZ; });
        rejects([](auto& f) { f.predictedPreviousDepth.resize(f.depth.size(), 10); });
        rejects([](auto& f) {
            f.parameters.depthReprojection = tsr::DepthReprojection::PredictedPreviousZ;
            f.predictedPreviousDepth.resize(f.depth.size(), -1);
        });
        rejects([](auto& f) {
            f.parameters.depthReprojection = tsr::DepthReprojection::PredictedPreviousZ;
            f.predictedPreviousDepth.resize(f.depth.size(), std::numeric_limits<float>::quiet_NaN());
        });
        frame.parameters.depthReprojection = tsr::DepthReprojection::PredictedPreviousZ;
        frame.predictedPreviousDepth.resize(frame.depth.size(), 0);
        tsr::ValidateFrame(frame); // Zero explicitly rejects history, not the frame.
        std::cout << "PASS frame contract: HDR, motion, jitter, exposure, dimensions, reset and invalid input\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
