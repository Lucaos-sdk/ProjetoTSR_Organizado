#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <algorithm>

namespace tsr::integration {
using CameraVector=std::array<float,3>;
using CameraMatrix=std::array<float,16>;
struct CameraSample {
    CameraMatrix projection{},inverse{};
    CameraVector right{},up{},forward{};
    std::array<float,2> jitter{};
    float nearPlane=0,farPlane=0,fov=0,aspect=0;
    uint32_t frame=0,viewport=0,source=0;
    int inverted=-1,orthographic=-1,reset=-1;
};
struct CameraProjection {
    float fx,fy,cx,cy,depthA,depthB,viewZSign;
};
inline float CameraDot(const CameraVector& a,const CameraVector& b) {
    return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];
}
inline bool ValidCameraBasis(const CameraSample& c) {
    for(const auto& v:{c.right,c.up,c.forward}) {
        for(float x:v)if(!std::isfinite(x))return false;
        if(std::abs(CameraDot(v,v)-1)>1e-3f)return false;
    }
    return std::abs(CameraDot(c.right,c.up))<1e-3f && std::abs(CameraDot(c.right,c.forward))<1e-3f &&
           std::abs(CameraDot(c.up,c.forward))<1e-3f;
}
// Validate the supplied row-major, unjittered pinhole projection and its inverse.
// No near/far/FOV defaults, identity fallback or transpose guessing.
inline std::optional<CameraProjection> ReadCameraProjection(const CameraSample& c,uint32_t width,uint32_t height) {
    if(!width||!height||width>16384||height>16384||c.orthographic!=0||
       (c.inverted!=0&&c.inverted!=1)||(c.reset!=0&&c.reset!=1))return {};
    for(float x:c.projection)if(!std::isfinite(x)||std::abs(x)>1e8f)return {};
    for(float x:c.inverse)if(!std::isfinite(x)||std::abs(x)>1e8f)return {};
    for(float x:c.jitter)if(!std::isfinite(x)||std::abs(x)>1)return {};
    const auto& m=c.projection;
    for(int i:{1,2,3,4,6,7,12,13,15})if(std::abs(m[i])>1e-6f)return {};
    if(m[0]<=0||m[5]<=0||std::abs(std::abs(m[11])-1)>1e-6f||std::abs(m[14])<1e-8f)return {};
    for(int row=0;row<4;++row)for(int col=0;col<4;++col) {
        double product=0;
        for(int k=0;k<4;++k)product+=double(m[row*4+k])*c.inverse[k*4+col];
        if(std::abs(product-(row==col?1.:0.))>1e-3)return {};
    }
    const float sign=m[11];
    CameraProjection p{m[0]*width*.5f,m[5]*height*.5f,
        width*.5f*(1+m[8]*sign)-.5f,height*.5f*(1-m[9]*sign)-.5f,m[10]*sign,m[14],sign};
    if((c.inverted==1)!=(p.depthB>0))return {};
    // Endpoints may include infinity, but their finite distances must be positive
    // and ordered consistently with the game's explicit inverted-depth flag.
    const double nearDen=(c.inverted?1.:0.)-p.depthA;
    const double farDen=(c.inverted?0.:1.)-p.depthA;
    if(nearDen==0)return {};
    const double nearZ=p.depthB/nearDen;
    const double farZ=farDen==0?INFINITY:p.depthB/farDen;
    if(!(nearZ>0)||!(farZ>nearZ))return {};
    return p;
}
inline std::optional<float> CameraLinearZ(float depth,const CameraProjection& p) {
    if(!std::isfinite(depth)||depth<0||depth>1)return {};
    const float denominator=depth-p.depthA;
    if(denominator==0)return {};
    const float z=p.depthB/denominator;
    if(!std::isfinite(z)||z<=0)return {};
    return z;
}
// Canonical coordinates match the experiment: X right, Y down, Z forward.
// This basis change lets a future model anchor its light to a reference camera.
inline CameraVector CameraNormalInReference(const CameraVector& n,const CameraSample& current,const CameraSample& reference) {
    CameraVector world{};
    for(int i=0;i<3;++i)world[i]=n[0]*current.right[i]-n[1]*current.up[i]+n[2]*current.forward[i];
    return {CameraDot(world,reference.right),-CameraDot(world,reference.up),CameraDot(world,reference.forward)};
}
}
