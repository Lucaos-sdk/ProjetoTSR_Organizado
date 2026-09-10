#pragma once
#include "reference.h"

namespace tsr {
// Harness contract v2; deliberately separate from the unresolved neural channels.
using Motion = std::array<float, 2>;
enum class DepthReprojection { FixedCamera, PredictedPreviousZ };
struct FrameParameters {
    Size renderSize;
    Size outputSize;
    std::array<float, 2> jitterPixels{0, 0};
    float preExposure = 1;
    uint64_t frameIndex = 0;
    bool reset = true;
    DepthReprojection depthReprojection = DepthReprojection::FixedCamera;
};
inline bool SameSize(Size a, Size b) { return a.width == b.width && a.height == b.height; }
inline bool CanReuse(bool initialized, const FrameParameters& previous, const FrameParameters& current) {
    return initialized && !current.reset && SameSize(previous.renderSize, current.renderSize) &&
           SameSize(previous.outputSize, current.outputSize) &&
           previous.frameIndex != UINT64_MAX && current.frameIndex == previous.frameIndex + 1;
}
struct FrameInput {
    FrameParameters parameters;
    std::vector<Pixel> color; // Linear RGB * preExposure; independent alpha.
    std::vector<float> depth; // Positive linear view-space Z in meters.
    std::vector<Motion> motion; // Current -> previous, render pixels, excludes jitter.
    // In PredictedPreviousZ mode: previous-camera Z of the CURRENT surface.
    // Zero means no valid previous projection (e.g. behind the previous camera).
    std::vector<float> predictedPreviousDepth;
};
inline void ValidateFrame(const FrameInput& frame) {
    const auto& p = frame.parameters;
    const auto count = Count(p.renderSize);
    Count(p.outputSize);
    if (frame.color.size() != count || frame.depth.size() != count || frame.motion.size() != count)
        throw std::invalid_argument("Frame planes must match render dimensions");
    if (!std::isfinite(p.preExposure) || p.preExposure <= 0)
        throw std::invalid_argument("preExposure must be finite and positive");
    for (float jitter : p.jitterPixels)
        if (!std::isfinite(jitter) || std::abs(jitter) > .5f)
            throw std::invalid_argument("Jitter must be in [-0.5,0.5] render pixels");
    if (p.frameIndex == 0 && !p.reset)
        throw std::invalid_argument("First frame requires reset");
    if (p.depthReprojection != DepthReprojection::FixedCamera &&
        p.depthReprojection != DepthReprojection::PredictedPreviousZ)
        throw std::invalid_argument("Unknown depth reprojection mode");
    if (p.depthReprojection == DepthReprojection::PredictedPreviousZ) {
        if (frame.predictedPreviousDepth.size() != count)
            throw std::invalid_argument("Predicted previous depth must match render dimensions");
        for (float value : frame.predictedPreviousDepth)
            if (!std::isfinite(value) || value < 0)
                throw std::invalid_argument("Predicted previous depth must be finite and nonnegative");
    } else if (!frame.predictedPreviousDepth.empty()) {
        throw std::invalid_argument("Predicted depth requires explicit PredictedPreviousZ mode");
    }
    for (size_t i = 0; i < count; ++i) {
        for (float value : frame.color[i])
            if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite color");
        if (!std::isfinite(frame.depth[i]) || frame.depth[i] <= 0)
            throw std::invalid_argument("Depth must be finite positive view Z");
        for (float value : frame.motion[i])
            if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite motion");
    }
}
inline FrameInput FrameFixture(Size render, Size output) {
    FrameInput frame{{render, output}, Fixture(render),
                     std::vector<float>(Count(render), 10.f),
                     std::vector<Motion>(Count(render), Motion{0, 0})};
    return frame;
}
}
