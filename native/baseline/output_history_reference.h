#pragma once
#include "temporal_reference.h"
namespace tsr {
// Experimental sample accumulation on the stable OUTPUT grid.
class OutputHistoryReference {
    bool initialized=false;
    FrameParameters previous{};
    TemporalResult history;
public:
    TemporalResult Run(const FrameInput& frame) {
        ValidateFrame(frame);
        const auto& p=frame.parameters;
        if (p.outputSize.width<p.renderSize.width || p.outputSize.height<p.renderSize.height)
            throw std::invalid_argument("Output history supports native resolution or upscaling, not downsampling");
        const bool reuse=CanReuse(initialized,previous,p);
        const double exposure=reuse ? double(p.preExposure)/previous.preExposure : 1;
        if (!std::isfinite(float(exposure))) throw std::invalid_argument("Exposure ratio overflows float");
        const auto src=p.renderSize, dst=p.outputSize;
        TemporalResult result{Bilinear(frame.color,src,dst,p.jitterPixels),std::vector<Pixel>(Count(dst))};
        for (uint32_t y=0;y<dst.height;++y) for (uint32_t x=0;x<dst.width;++x) {
            const size_t i=size_t(y)*dst.width+x;
            double sx=(x+.5)*src.width/dst.width-.5-p.jitterPixels[0];
            double sy=(y+.5)*src.height/dst.height-.5-p.jitterPixels[1];
            if (std::abs(sx-std::round(sx*2)*.5)<1e-6) sx=std::round(sx*2)*.5;
            if (std::abs(sy-std::round(sy*2)*.5)<1e-6) sy=std::round(sy*2)*.5;
            const int nx=int(std::floor(sx+.5)),ny=int(std::floor(sy+.5));
            const size_t sample=size_t(std::clamp(ny,0,int(src.height)-1))*src.width+std::clamp(nx,0,int(src.width)-1);
            double sampleWeight=std::max(0.,1-2*std::abs(sx-nx))*std::max(0.,1-2*std::abs(sy-ny));
            if (nx<0 || ny<0 || nx>=int(src.width) || ny>=int(src.height)) sampleWeight=0;
            const double predicted=p.depthReprojection==DepthReprojection::PredictedPreviousZ ?
                                   frame.predictedPreviousDepth[sample] : frame.depth[sample];
            const double px=x+double(frame.motion[sample][0])*dst.width/src.width;
            const double py=y+double(frame.motion[sample][1])*dst.height/src.height;
            double oldWeight=0;
            std::array<double,3> oldColor{};
            bool valid=reuse && predicted>0 && px>=0 && py>=0 && px<=dst.width-1 && py<=dst.height-1;
            if (valid) {
                const int lx=int(std::floor(px)),ly=int(std::floor(py));
                const double fx=px-lx,fy=py-ly;
                for (int yy=0;yy<2;++yy) for (int xx=0;xx<2;++xx) {
                    const double w=(xx?fx:1-fx)*(yy?fy:1-fy);
                    if (w==0) continue;
                    const size_t tap=size_t(std::min(ly+yy,int(dst.height)-1))*dst.width+std::min(lx+xx,int(dst.width)-1);
                    const auto& g=history.geometry[tap];
                    if (g[1]<=0 || std::abs(double(g[0])-predicted)>.01+.01*predicted) valid=false;
                    const double mass=w*g[1];
                    oldWeight+=mass;
                    for (size_t c=0;c<3;++c) oldColor[c]+=mass*history.color[tap][c];
                }
            }
            if (!valid) { oldWeight=0; oldColor={}; }
            oldWeight*=.9;
            const double total=oldWeight+sampleWeight;
            if (total>0) for (size_t c=0;c<3;++c)
                result.color[i][c]=float((.9*oldColor[c]*exposure+sampleWeight*frame.color[sample][c])/total);
            result.geometry[i]={frame.depth[sample],float(std::min(total,4.)),0,valid?1.f:0.f};
        }
        history=result;
        previous=p;
        initialized=true;
        result.output=result.color;
        return result;
    }
};
}
