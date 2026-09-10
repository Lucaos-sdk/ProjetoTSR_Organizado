#pragma once
#include "camera_contract.h"

namespace tsr::integration {
struct HistoricalCamera {
    CameraSample camera;
    uint64_t serial=0,milliseconds=0;
};
enum class CameraMatchStatus { Missing,UniqueCandidate,Ambiguous,InvalidInput,Reset };
inline const char* CameraMatchName(CameraMatchStatus status) {
    switch(status) {
    case CameraMatchStatus::UniqueCandidate:return "unique_candidate";
    case CameraMatchStatus::Ambiguous:return "ambiguous";
    case CameraMatchStatus::InvalidInput:return "invalid_input";
    case CameraMatchStatus::Reset:return "reset";
    default:return "missing";
    }
}
struct CameraMatch {
    CameraMatchStatus status=CameraMatchStatus::Missing;
    std::optional<HistoricalCamera> candidate;
    size_t matches=0;
};
// A bounded candidate search, not an authority for GPU frame/resource identity.
// The owner serializes access. No allocations or borrowed resource pointers.
class CameraHistory {
    std::array<HistoricalCamera,8> entries{};
    size_t count=0,next=0;
    std::optional<HistoricalCamera> latest;
public:
    void Clear(){count=next=0;latest.reset();}
    size_t Size() const{return count;}
    void Push(const HistoricalCamera& value) {
        const auto& c=value.camera;
        if(latest && (value.milliseconds<latest->milliseconds || value.serial<=latest->serial ||
           c.source!=latest->camera.source || c.viewport!=latest->camera.viewport || c.frame<latest->camera.frame))Clear();
        // A reset or invalid new camera must never expose a candidate from the old scene.
        if(c.reset!=0||!ReadCameraProjection(c,1,1)||!ValidCameraBasis(c)){Clear();return;}
        entries[next]=value;next=(next+1)%entries.size();count=std::min(count+1,entries.size());latest=value;
    }
    CameraMatch Find(std::array<float,2> jitter,uint64_t now,uint32_t width,uint32_t height,
                     bool inputReset=false,uint64_t maxAgeMilliseconds=100) const {
        if(inputReset)return {CameraMatchStatus::Reset,{},0};
        for(float x:jitter)if(!std::isfinite(x)||std::abs(x)>1)return {CameraMatchStatus::InvalidInput,{},0};
        if(!width||!height||width>16384||height>16384||maxAgeMilliseconds>1000)
            return {CameraMatchStatus::InvalidInput,{},0};
        CameraMatch result;
        for(size_t i=0;i<count;++i) {
            const auto& entry=entries[i];
            if(now<entry.milliseconds||now-entry.milliseconds>maxAgeMilliseconds)continue;
            if(std::abs(entry.camera.jitter[0]-jitter[0])>2e-6f||std::abs(entry.camera.jitter[1]-jitter[1])>2e-6f)continue;
            if(!ReadCameraProjection(entry.camera,width,height)||!ValidCameraBasis(entry.camera))continue;
            ++result.matches;result.candidate=entry;
        }
        if(result.matches==1)result.status=CameraMatchStatus::UniqueCandidate;
        else if(result.matches>1){result.status=CameraMatchStatus::Ambiguous;result.candidate.reset();}
        return result;
    }
};
}
