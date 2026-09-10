#pragma once
#include "frame_contract.h"
#include <filesystem>
#include <fstream>
#include <iostream>

namespace tsr {
// Analytic scene in render-pixel coordinates. Ground truth is sampled directly
// on the stable output grid, not reconstructed from the low-resolution input.
inline Pixel SceneColor(double x, double y, unsigned frame) {
    const double left = 25 + .75*frame;
    if (x >= left && x < left+18 && y >= 14 && y < 40)
        return {.9f,.16f,.06f,1};
    const double wave = .12*std::sin(x*.28)*std::cos(y*.23);
    const float line = std::abs(x-72.3) < .45 ? .8f : 0;
    return {float(.2+wave)+line,float(.35+wave)+line,float(.48+wave)+line,1};
}
inline void WriteBmp(const std::filesystem::path& path, const std::vector<Pixel>& data, Size size) {
    const uint32_t pitch = (size.width*3+3)&~3u, bytes = pitch*size.height;
    std::ofstream out(path,std::ios::binary);
    if (!out) throw std::runtime_error("Cannot write visual frame");
    auto u16 = [&](uint16_t v) { out.put(char(v&255)); out.put(char(v>>8)); };
    auto u32 = [&](uint32_t v) { for (unsigned i=0;i<4;++i) out.put(char((v>>(8*i))&255)); };
    out.write("BM",2); u32(54+bytes); u32(0); u32(54); u32(40);
    u32(size.width); u32(size.height); u16(1); u16(24); u32(0); u32(bytes);
    u32(2835); u32(2835); u32(0); u32(0);
    for (uint32_t row=0;row<size.height;++row) {
        const uint32_t y=size.height-1-row;
        for (uint32_t x=0;x<size.width;++x) for (int c=2;c>=0;--c) {
            const double linear=std::clamp(double(data[size_t(y)*size.width+x][c]),0.,1.);
            const double srgb=linear<=.0031308 ? 12.92*linear : 1.055*std::pow(linear,1/2.4)-.055;
            out.put(char(std::lround(255*srgb)));
        }
        for (uint32_t pad=size.width*3;pad<pitch;++pad) out.put(0);
    }
    if (!out) throw std::runtime_error("Visual frame write failed");
}
struct VisualMetrics {
    double error=0, change=0;
    std::vector<Pixel> previousResidual;
    void Add(const std::vector<Pixel>& actual,const std::vector<Pixel>& truth) {
        double mse=0,delta=0;
        std::vector<Pixel> residual(actual.size());
        for (size_t i=0;i<actual.size();++i) for (size_t c=0;c<3;++c) {
            const double r=double(actual[i][c])-truth[i][c];
            mse+=r*r;
            if (!previousResidual.empty()) { const double d=r-previousResidual[i][c]; delta+=d*d; }
            residual[i][c]=float(r);
        }
        error=mse/(3*actual.size());
        change=previousResidual.empty() ? 0 : delta/(3*actual.size());
        previousResidual=std::move(residual);
    }
};
template<class Run> void VisualSequence(Run run,const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    std::ofstream metrics(directory/"metrics.csv");
    if (!metrics) throw std::runtime_error("Cannot write metrics");
    metrics << "frame,method,mse_linear_rgb,residual_delta_mse\n";
    metrics << std::setprecision(10);
    const Size render{96,54}, output{192,108};
    const Motion phases[] = {{-.25f,-.25f},{.25f,.25f},{.25f,-.25f},{-.25f,.25f}};
    VisualMetrics state[3];
    for (unsigned t=0;t<16;++t) {
        auto f=FrameFixture(render,output);
        f.parameters.frameIndex=t; f.parameters.reset=t==0; f.parameters.jitterPixels=phases[t%4];
        for (uint32_t y=0;y<render.height;++y) for (uint32_t x=0;x<render.width;++x) {
            const size_t i=size_t(y)*render.width+x;
            const double sx=x+phases[t%4][0], sy=y+phases[t%4][1];
            f.color[i]=SceneColor(sx,sy,t);
            const bool foreground=sx>=25+.75*t && sx<43+.75*t && sy>=14 && sy<40;
            f.depth[i]=foreground ? 2.f : 10.f;
            f.motion[i]=foreground ? Motion{-.75f,0} : Motion{0,0};
        }
        const auto result=run(f);
        const auto raw=Bilinear(f.color,render,output);
        const auto spatial=Bilinear(f.color,render,output,f.parameters.jitterPixels);
        std::vector<Pixel> truth(Count(output));
        for (uint32_t y=0;y<output.height;++y) for (uint32_t x=0;x<output.width;++x)
            truth[size_t(y)*output.width+x]=SceneColor((x+.5)*render.width/output.width-.5,(y+.5)*render.height/output.height-.5,t);
        const std::vector<Pixel>* images[]={&raw,&spatial,&result.output};
        const char* names[]={"raw_cpu","stable_spatial_cpu","temporal_gpu"};
        for (unsigned m=0;m<3;++m) {
            state[m].Add(*images[m],truth);
            metrics << t << ',' << names[m] << ',' << state[m].error << ',' << state[m].change << '\n';
            WriteBmp(directory/(std::to_string(t)+"_"+names[m]+".bmp"),*images[m],output);
        }
        WriteBmp(directory/(std::to_string(t)+"_truth.bmp"),truth,output);
    }
    if (!metrics) throw std::runtime_error("Metrics write failed");
    std::cout << "PASS visual sequence: 16 frames; linear metrics; previews clipped to [0,1] and converted to sRGB\n";
}
}
