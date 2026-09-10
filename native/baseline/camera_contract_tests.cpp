#include "../integration/camera_contract.h"
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace tsr::integration;
static void Require(bool b){if(!b)throw std::runtime_error("Camera contract test failed");}
static CameraMatrix Invert(CameraMatrix m) {
    // Independent double Gauss-Jordan inverse, not the production validation.
    double a[4][8]{};for(int i=0;i<4;i++)for(int j=0;j<4;j++){a[i][j]=m[i*4+j];a[i][j+4]=i==j;}
    for(int c=0;c<4;c++) {
        int pivot=c;for(int r=c+1;r<4;r++)if(std::abs(a[r][c])>std::abs(a[pivot][c]))pivot=r;
        Require(std::abs(a[pivot][c])>1e-12);for(int j=0;j<8;j++)std::swap(a[pivot][j],a[c][j]);
        double div=a[c][c];for(int j=0;j<8;j++)a[c][j]/=div;
        for(int r=0;r<4;r++)if(r!=c){double f=a[r][c];for(int j=0;j<8;j++)a[r][j]-=f*a[c][j];}
    }
    CameraMatrix out{};for(int i=0;i<4;i++)for(int j=0;j<4;j++)out[i*4+j]=float(a[i][j+4]);return out;
}
static CameraSample Fixture(bool reversed,bool infinite,float sign) {
    CameraSample c;c.right={1,0,0};c.up={0,1,0};c.forward={0,0,1};c.orthographic=0;c.reset=0;c.inverted=reversed;
    const float n=.1f,f=100.f;
    const float A=reversed?(infinite?0:n/(n-f)):(infinite?1:f/(f-n));
    const float B=reversed?(infinite?n:n*f/(f-n)):(infinite?-n:n*f/(n-f));
    c.projection={1.3f,0,0,0, 0,1.9f,0,0, .07f,-.03f,A*sign,sign, 0,0,B,0};
    c.inverse=Invert(c.projection);return c;
}
int main(){try {
    for(bool reverse:{false,true})for(bool infinite:{false,true})for(float sign:{-1.f,1.f}) {
        auto c=Fixture(reverse,infinite,sign);auto p=ReadCameraProjection(c,1476,830);Require(bool(p));Require(ValidCameraBasis(c));
        for(float positiveZ:{.1f,.5f,3.f,50.f}) {
            // Forward project a known 3D point using a full row-vector multiply.
            const float v[4]={.03f,.02f,sign*positiveZ,1};float clip[4]{};
            for(int col=0;col<4;col++)for(int row=0;row<4;row++)clip[col]+=v[row]*c.projection[row*4+col];
            auto z=CameraLinearZ(std::clamp(clip[2]/clip[3],0.f,1.f),*p);Require(bool(z));
            Require(std::abs(*z-positiveZ)<positiveZ*8e-5f);
            const float px=(clip[0]/clip[3]+1)*1476*.5f-.5f;
            const float py=(1-clip[1]/clip[3])*830*.5f-.5f;
            Require(std::abs((px-p->cx)/p->fx*positiveZ-v[0])<1e-5f);
            Require(std::abs((py-p->cy)/p->fy*positiveZ+v[1])<1e-5f);
        }
        auto bad=c;bad.projection[0]=std::numeric_limits<float>::quiet_NaN();Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.inverse[0]+=1;Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.orthographic=1;Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.inverted=2;Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.inverted=!reverse;Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.reset=2;Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.jitter[0]=2;Require(!ReadCameraProjection(bad,1476,830));
        bad=c;bad.up=bad.right;Require(!ValidCameraBasis(bad));
        Require(!ReadCameraProjection(c,0,830));Require(!CameraLinearZ(-.1f,*p));
        Require(!CameraLinearZ(std::numeric_limits<float>::infinity(),*p));
    }
    auto reference=Fixture(true,true,1);auto rotated=reference;
    rotated.right={0,0,-1};rotated.forward={1,0,0};
    const auto anchored=CameraNormalInReference({0,0,1},rotated,reference);
    Require(anchored==CameraVector{1,0,0});
    // A fixed world normal seen by the rotated camera maps to the same reference normal.
    Require(CameraNormalInReference({-1,0,0},rotated,reference)==CameraVector{0,0,1});
    std::cout<<"PASS LH/RH, finite/infinite/reversed depth, asymmetric projection, invalid camera, world-anchored normal\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
