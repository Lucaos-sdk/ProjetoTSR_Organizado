#pragma once
#include "frame_contract.h"

namespace tsr {
struct TemporalResult {
    std::vector<Pixel> color;
    std::vector<Pixel> geometry; // depth in X, accepted-history mask in W.
    std::vector<Pixel> output; // Stable output grid; not fed back into history.
};
// All contributing history taps must match the current surface's previous Z.
// The fixed blend is a correctness baseline, not a learned confidence estimate.
class TemporalReference {
    bool initialized = false;
    FrameParameters previous{};
    TemporalResult history;
public:
    TemporalResult Run(const FrameInput& frame) {
        ValidateFrame(frame);
        const auto& p = frame.parameters;
        const bool reuse = CanReuse(initialized, previous, p);
        const double ratio = reuse ? double(p.preExposure) / previous.preExposure : 1.;
        if (!std::isfinite(float(ratio))) throw std::invalid_argument("Exposure ratio overflows float");
        TemporalResult result{frame.color, std::vector<Pixel>(frame.color.size())};
        const auto size = p.renderSize;
        for (uint32_t y = 0; y < size.height; ++y) for (uint32_t x = 0; x < size.width; ++x) {
            const size_t i = size_t(y) * size.width + x;
            result.geometry[i] = {frame.depth[i], 0, 0, 0};
            if (!reuse) continue;
            const double expectedDepth = p.depthReprojection == DepthReprojection::PredictedPreviousZ ?
                                         frame.predictedPreviousDepth[i] : frame.depth[i];
            if (expectedDepth <= 0) continue;
            const double px = x + double(frame.motion[i][0]) + p.jitterPixels[0] - previous.jitterPixels[0];
            const double py = y + double(frame.motion[i][1]) + p.jitterPixels[1] - previous.jitterPixels[1];
            if (px < 0 || py < 0 || px > size.width - 1 || py > size.height - 1) continue;
            const int lx = int(std::floor(px)), ly = int(std::floor(py));
            const double fx = px - lx, fy = py - ly;
            std::array<double, 3> sum{};
            bool valid = true;
            for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx) {
                const double weight = (dx ? fx : 1 - fx) * (dy ? fy : 1 - fy);
                if (weight == 0) continue;
                const size_t tap = size_t(std::min(ly + dy, int(size.height)-1)) * size.width +
                                   std::min(lx + dx, int(size.width)-1);
                if (std::abs(double(history.geometry[tap][0]) - expectedDepth) > .01 + .01 * expectedDepth)
                    valid = false;
                for (size_t c = 0; c < 3; ++c) sum[c] += weight * history.color[tap][c];
            }
            if (valid) {
                for (size_t c = 0; c < 3; ++c) result.color[i][c] = float(.25 * frame.color[i][c] + .75 * sum[c] * ratio);
                result.geometry[i][3] = 1;
            }
        }
        history = result;
        previous = p;
        initialized = true;
        result.output = Bilinear(result.color, p.renderSize, p.outputSize, p.jitterPixels);
        return result;
    }
};
}
