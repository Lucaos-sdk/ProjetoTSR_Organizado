#include "../integration/frame_generation_policy.h"
#include <stdexcept>
#include <iostream>
#include <limits>
int main() {
    try {
        int active=1,calls=0;
        auto accepted=[&](uint32_t){++calls;return true;};
        auto require=[](bool ok){if(!ok)throw std::runtime_error("FG policy failure");};
        for(auto request:{0u,2u,3u,std::numeric_limits<uint32_t>::max()})
            require(!tsr::integration::ApplyGeneratedFrameCount(request,1,active,accepted));
        require(calls==0 && active==1);
        require(!tsr::integration::ApplyGeneratedFrameCount(1,0,active,accepted));
        require(!tsr::integration::ApplyGeneratedFrameCount(1,-1,active,accepted));
        require(tsr::integration::ApplyGeneratedFrameCount(3,3,active,accepted) && active==3 && calls==1);
        require(!tsr::integration::ApplyGeneratedFrameCount(2,3,active,[](uint32_t){return false;}) && active==3);
        require(tsr::integration::ApplyGeneratedFrameCount(1,1,active,accepted) && active==1);
        std::cout<<"PASS capability bounds, unsupported calls, 4x count, failed update preserves active count\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
