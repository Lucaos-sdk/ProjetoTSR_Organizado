#pragma once
#include "reference.h"
namespace tsr {
enum class Reconstruction : uint32_t { Bilinear = 0, CubicClamped = 1 };
// Catmull-Rom interpolating kernel, expressed independently as a distance kernel.
inline double CubicKernel(double distance) {
    const double x = std::abs(distance);
    if (x < 1) return 1.5*x*x*x - 2.5*x*x + 1;
    if (x < 2) return -.5*x*x*x + 2.5*x*x - 4*x + 2;
    return 0;
}
inline std::vector<Pixel> Reconstruct(const std::vector<Pixel>& input, Size src, Size dst,
                                    std::array<float,2> jitter, Reconstruction filter) {
    if (filter == Reconstruction::Bilinear) return Bilinear(input,src,dst,jitter);
    if (filter != Reconstruction::CubicClamped) throw std::invalid_argument("Unknown reconstruction");
    if (input.size()!=Count(src)) throw std::invalid_argument("Input size mismatch");
    std::vector<Pixel> output(Count(dst));
    auto get = [&](int x,int y,size_t c) { return double(input[size_t(std::clamp(y,0,int(src.height)-1))*src.width+
                                                                         std::clamp(x,0,int(src.width)-1)][c]); };
    for (uint32_t y=0;y<dst.height;++y) for (uint32_t x=0;x<dst.width;++x) {
        double px=(x+.5)*src.width/dst.width-.5-jitter[0];
        double py=(y+.5)*src.height/dst.height-.5-jitter[1];
        // Explicit tie convention shared with the GPU clipping neighborhood.
        if (std::abs(px-std::round(px))<1e-6) px=std::round(px);
        if (std::abs(py-std::round(py))<1e-6) py=std::round(py);
        const int lx=int(std::floor(px)), ly=int(std::floor(py));
        for (size_t c=0;c<4;++c) {
            double sum=0;
            for (int yy=-1;yy<=2;++yy) for (int xx=-1;xx<=2;++xx)
                sum+=get(lx+xx,ly+yy,c)*CubicKernel(px-(lx+xx))*CubicKernel(py-(ly+yy));
            const double a=get(lx,ly,c),b=get(lx+1,ly,c),d=get(lx,ly+1,c),e=get(lx+1,ly+1,c);
            output[size_t(y)*dst.width+x][c]=float(std::clamp(sum,std::min({a,b,d,e}),std::max({a,b,d,e})));
        }
    }
    return output;
}
}
