#include "../integration/camera_history.h"
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace tsr::integration;
static void Require(bool ok){if(!ok)throw std::runtime_error("Camera history regression");}
static HistoricalCamera Sample(uint32_t frame,float jitter,uint64_t time) {
    HistoricalCamera v;v.serial=frame;v.milliseconds=time;auto& c=v.camera;c.frame=frame;c.source=1;c.viewport=0;
    c.orthographic=0;c.reset=0;c.inverted=1;c.right={1,0,0};c.up={0,1,0};c.forward={0,0,1};
    c.jitter={jitter,-jitter};
    // Independent canonical infinite reversed projection and its analytic inverse.
    c.projection={1,0,0,0,0,2,0,0,0,0,0,1,0,0,.2f,0};
    c.inverse={1,0,0,0,0,.5f,0,0,0,0,0,5,0,0,1,0};return v;
}
int main(){try {
    CameraHistory h;Require(h.Find({0,0},100,1476,830).status==CameraMatchStatus::Missing);
    h.Push(Sample(40,.1f,100));h.Push(Sample(41,.2f,110));
    auto m=h.Find({.1f,-.1f},115,1476,830);
    Require(m.status==CameraMatchStatus::UniqueCandidate && m.candidate->camera.frame==40);
    // The search also finds a lag of three: no hard-coded frame-1 or Halton generator.
    h.Push(Sample(42,.3f,120));h.Push(Sample(43,.4f,130));
    Require(h.Find({.1f,-.1f},135,1476,830).candidate->camera.frame==40);
    Require(h.Find({.1f,-.1f},201,1476,830).status==CameraMatchStatus::Missing);
    Require(h.Find({.4f,-.4f},129,1476,830).status==CameraMatchStatus::Missing);
    h.Push(Sample(44,.1f,140));m=h.Find({.1f,-.1f},145,1476,830);
    Require(m.status==CameraMatchStatus::Ambiguous && !m.candidate && m.matches==2);
    Require(h.Find({.2f,-.2f},145,1476,830,true).status==CameraMatchStatus::Reset);
    Require(h.Find({NAN,0},145,1476,830).status==CameraMatchStatus::InvalidInput);
    Require(h.Find({0,0},145,0,830).status==CameraMatchStatus::InvalidInput);
    // Scene reset, source/viewport changes, clock rollback and frame restart invalidate history.
    for(int mode=0;mode<6;++mode) {
        h.Clear();h.Push(Sample(40,.1f,100));auto changed=Sample(41,.2f,110);
        if(mode==0)changed.camera.reset=1;
        if(mode==1)changed.camera.viewport=2;
        if(mode==2)changed.camera.source=2;
        if(mode==3)changed.milliseconds=99;
        if(mode==4)changed.camera.frame=1;
        if(mode==5)changed.camera.projection[0]=NAN;
        h.Push(changed);Require(!h.Find({.1f,-.1f},115,1476,830).candidate);
    }
    // Multiple identical callbacks are ambiguous too, rather than silently replacing poses.
    h.Clear();h.Push(Sample(40,.1f,100));auto duplicate=Sample(40,.1f,101);duplicate.serial=41;h.Push(duplicate);
    Require(h.Find({.1f,-.1f},105,1476,830).status==CameraMatchStatus::Ambiguous);
    h.Clear();for(uint32_t i=1;i<=10;++i)h.Push(Sample(i,float(i)*.01f,i));
    Require(h.Size()==8);Require(!h.Find({.01f,-.01f},10,1476,830).candidate);
    Require(h.Find({.1f,-.1f},10,1476,830).candidate->camera.frame==10);
    std::cout<<"PASS jitter-selected history, variable lag, ambiguity, reset, age, stream changes and ring eviction\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
