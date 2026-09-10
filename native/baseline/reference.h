#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <limits>
#include <sstream>
#include <iomanip>
#include <vector>

namespace tsr {
using Pixel = std::array<float, 4>;
static_assert(sizeof(Pixel) == 16);
struct Size { uint32_t width, height; };
inline size_t Count(Size s) {
    if (!s.width || !s.height || s.width > 16384 || s.height > 16384)
        throw std::invalid_argument("Dimensions must be in [1,16384]");
    return size_t(s.width) * s.height;
}
// Double-precision reference, center-aligned coordinates and edge clamping.
inline std::vector<Pixel> Bilinear(const std::vector<Pixel>& input, Size src, Size dst,
                                   std::array<float, 2> jitter = {0,0}) {
    if (input.size() != Count(src)) throw std::invalid_argument("Input size mismatch");
    std::vector<Pixel> output(Count(dst));
    for (uint32_t y = 0; y < dst.height; ++y) {
        const double py = (double(y) + .5) * src.height / dst.height - .5 - jitter[1];
        const int y0 = int(std::floor(py));
        const double fy = py - y0;
        for (uint32_t x = 0; x < dst.width; ++x) {
            const double px = (double(x) + .5) * src.width / dst.width - .5 - jitter[0];
            const int x0 = int(std::floor(px));
            const double fx = px - x0;
            auto get = [&](int xx, int yy, size_t c) {
                return double(input[size_t(std::clamp(yy, 0, int(src.height)-1))*src.width +
                                    std::clamp(xx, 0, int(src.width)-1)][c]);
            };
            for (size_t c = 0; c < 4; ++c)
                output[size_t(y)*dst.width+x][c] = float(
                    (1-fy)*((1-fx)*get(x0,y0,c)+fx*get(x0+1,y0,c)) +
                    fy*((1-fx)*get(x0,y0+1,c)+fx*get(x0+1,y0+1,c)));
        }
    }
    return output;
}
inline std::vector<Pixel> Fixture(Size size) {
    std::vector<Pixel> result(Count(size));
    for (uint32_t y=0; y<size.height; ++y)
        for (uint32_t x=0; x<size.width; ++x)
            result[size_t(y)*size.width+x] = {float(x % 13)-3.f, float(y % 7)*8.f,
                                             (x+y)%2 ? 64.f : -2.f, 1.f};
    return result;
}
inline double Verify(const std::vector<Pixel>& actual, const std::vector<Pixel>& expected) {
    if (actual.size() != expected.size()) throw std::runtime_error("Output size mismatch");
    double maxError = 0;
    for (size_t i=0; i<actual.size(); ++i)
        for (size_t c=0; c<4; ++c) {
            const double error = std::abs(double(actual[i][c])-expected[i][c]);
            const double tolerance = .0005 + .00005*std::abs(expected[i][c]);
            if (!std::isfinite(actual[i][c]) || error > tolerance) {
                std::ostringstream message;
                message << std::setprecision(10) << "GPU/reference mismatch at pixel " << i
                        << ", channel " << c << ": actual=" << actual[i][c]
                        << ", expected=" << expected[i][c] << ", abs_error=" << error
                        << ", tolerance=" << tolerance;
                throw std::runtime_error(message.str());
            }
            maxError = std::max(maxError, error);
        }
    return maxError;
}
}
