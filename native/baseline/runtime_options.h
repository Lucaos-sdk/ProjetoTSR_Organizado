#pragma once
#include "frame_contract.h"
#include <charconv>
#include <climits>
#include <string_view>
namespace tsr {
inline uint32_t ParseUnsigned(std::string_view text) {
    uint32_t value=0;
    auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if (text.empty() || parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
        throw std::invalid_argument("Expected an unsigned integer");
    return value;
}
inline Size ParseResolution(std::string_view text) {
    const auto split=text.find('x');
    if (split==std::string_view::npos) throw std::invalid_argument("Use WIDTHxHEIGHT, e.g. 1920x1080");
    Size result{ParseUnsigned(text.substr(0,split)),ParseUnsigned(text.substr(split+1))};
    Count(result);
    return result;
}
inline int ParseAdapter(std::string_view text) {
    const auto value=ParseUnsigned(text);
    if (value>INT_MAX) throw std::invalid_argument("Adapter index too large");
    return int(value);
}
template<class Run> void ConfiguredResolutionCases(Run run,Size render,Size output) {
    const Motion jitter[]={{-.25f,-.25f},{.25f,.25f},{.25f,-.25f},{-.25f,.25f}};
    for (unsigned i=0;i<4;++i) {
        auto f=FrameFixture(render,output);
        f.parameters.frameIndex=i; f.parameters.reset=i==0; f.parameters.jitterPixels=jitter[i];
        for (auto& motion:f.motion) motion={-.25f,.125f};
        run(0,f,"configured resolution");
    }
}
}
