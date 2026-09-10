#include "reconstruction.h"
#include <iostream>
void require(bool v) { if (!v) throw std::runtime_error("Reconstruction test failed"); }
int main() {
    try {
        using namespace tsr;
        const auto mode=Reconstruction::CubicClamped;
        const Pixel hdr{-3,5000,.25f,1};
        for (const auto& p : Reconstruct({hdr},{1,1},{13,7},{.5f,-.5f},mode)) require(p==hdr);
        const auto source=Fixture({7,5});
        require(Reconstruct(source,{7,5},{7,5},{0,0},mode)==source);
        const std::vector<Pixel> impulse={Pixel{0,0,0,0},Pixel{1,1,1,1},Pixel{0,0,0,0},Pixel{0,0,0,0}};
        const auto out=Reconstruct(impulse,{4,1},{8,1},{0,0},mode);
        require(out[2][0]==111.f/128 && out[3][0]==111.f/128);
        require(out[2][0]>Bilinear(impulse,{4,1},{8,1})[2][0]);
        const std::vector<Pixel> step={Pixel{-2,-2,-2,0},Pixel{-2,-2,-2,0},Pixel{64,64,64,1},Pixel{64,64,64,1}};
        for (const auto& p : Reconstruct(step,{4,1},{37,3},{.25f,-.25f},mode)) {
            require(p[0]>=-2 && p[0]<=64 && p[3]>=0 && p[3]<=1);
        }
        require(CubicKernel(0)==1 && CubicKernel(1)==0 && CubicKernel(2)==0);
        std::cout << "PASS cubic: HDR constant, identity, known impulse weights, ringing bounds and alpha\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
