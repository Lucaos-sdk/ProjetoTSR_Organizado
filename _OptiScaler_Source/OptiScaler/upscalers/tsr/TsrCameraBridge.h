#pragma once
#include "../../../../native/integration/camera_contract.h"
#include "../../../../native/integration/camera_history.h"
#include <mutex>
#include <optional>
#include <cstring>
#include <atomic>

namespace tsr::game {
struct CameraObservation {
    integration::CameraSample camera;
    uint64_t serial=0,milliseconds=0;
};
// Metadata only. Latest is a diagnostic candidate, NEVER an implicit association
// to a frame/viewport/resource. No resource pointers or user/game memory retained.
inline std::mutex cameraMutex;
inline std::optional<CameraObservation> lastCamera;
inline uint64_t cameraSerial=0;
inline integration::CameraHistory cameraHistory;
// Borrowed resources scoped to ONE native XeSS execution on this thread.
// Unique jitter matching is experimental evidence, not an explicit Streamline frame ID.
struct RelightingBinding {
    ID3D12GraphicsCommandList* list=nullptr;
    ID3D12Resource* color=nullptr;
    ID3D12Resource* depth=nullptr;
    uint32_t width=0,height=0;
    std::optional<integration::HistoricalCamera> camera;
    const char* reason="outside_xess_scope";
};
inline thread_local RelightingBinding relightingBinding;
class ScopedXeSSRelighting {
    RelightingBinding previous;
public:
    ScopedXeSSRelighting(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* depth,
        uint32_t width,uint32_t height,float jitterX,float jitterY,uint32_t colorX,uint32_t colorY,
        uint32_t depthX,uint32_t depthY,bool reset):previous(relightingBinding) {
        relightingBinding={list,color,depth,width,height,{}};
        if(colorX||colorY||depthX||depthY){relightingBinding.reason="input_subregion";return;}
        if(reset){relightingBinding.reason="input_reset";return;}
        std::scoped_lock lock(cameraMutex);
        auto match=cameraHistory.Find({jitterX,jitterY},GetTickCount64(),width,height);
        relightingBinding.reason=integration::CameraMatchName(match.status);
        if(match.status==integration::CameraMatchStatus::UniqueCandidate)relightingBinding.camera=match.candidate;
    }
    ~ScopedXeSSRelighting(){relightingBinding=previous;}
};
inline void ObserveSl1Tag(uint32_t tag,uint32_t viewport,const void* native,uint32_t left,uint32_t top,uint32_t width,uint32_t height) {
    if(tag>4)return; // depth, motion, HUD-less, scaling input/output
    static std::array<std::atomic_uint64_t,5> counts{};
    const auto count=++counts[tag];
    if(count<=3||count%600==0)
        LOG_INFO("TSR camera tag: source=SL1 tag={} sample={} viewport={} resource={} extent={},{},{},{} association=unverified",
            tag,count,viewport,native,left,top,width,height);
}
template<class Constants>
inline void ObserveCamera(const Constants& values,uint32_t frame,uint32_t viewport,uint32_t source) {
    integration::CameraSample c{};
    static_assert(sizeof(values.cameraViewToClip)==sizeof(c.projection));
    static_assert(sizeof(values.clipToCameraView)==sizeof(c.inverse));
    std::memcpy(c.projection.data(),&values.cameraViewToClip,sizeof(c.projection));
    std::memcpy(c.inverse.data(),&values.clipToCameraView,sizeof(c.inverse));
    c.right={values.cameraRight.x,values.cameraRight.y,values.cameraRight.z};
    c.up={values.cameraUp.x,values.cameraUp.y,values.cameraUp.z};
    c.forward={values.cameraFwd.x,values.cameraFwd.y,values.cameraFwd.z};
    c.jitter={values.jitterOffset.x,values.jitterOffset.y};
    c.nearPlane=values.cameraNear;c.farPlane=values.cameraFar;c.fov=values.cameraFOV;c.aspect=values.cameraAspectRatio;
    c.frame=frame;c.viewport=viewport;c.source=source;
    c.inverted=int(values.depthInverted);c.orthographic=int(values.orthographicProjection);c.reset=int(values.reset);
    uint64_t serial;
    {std::scoped_lock lock(cameraMutex);serial=++cameraSerial;lastCamera=CameraObservation{c,serial,GetTickCount64()};
        cameraHistory.Push({c,serial,lastCamera->milliseconds});}
    if(serial<=3||serial%600==0) {
        const auto& m=c.projection;
        LOG_INFO("TSR camera observed: source=SL{} serial={} frame={} viewport={} inverted={} ortho={} reset={} near={} far={} fov={} aspect={} jitter={},{} basis_valid={} projection=[{},{},{},{};{},{},{},{};{},{},{},{};{},{},{},{}]",
            source,serial,frame,viewport,c.inverted,c.orthographic,c.reset,c.nearPlane,c.farPlane,c.fov,c.aspect,c.jitter[0],c.jitter[1],
            integration::ValidCameraBasis(c),m[0],m[1],m[2],m[3],m[4],m[5],m[6],m[7],m[8],m[9],m[10],m[11],m[12],m[13],m[14],m[15]);
        LOG_INFO("TSR camera basis: serial={} right={},{},{} up={},{},{} forward={},{},{}",
            serial,c.right[0],c.right[1],c.right[2],c.up[0],c.up[1],c.up[2],c.forward[0],c.forward[1],c.forward[2]);
        const auto& inv=c.inverse;
        LOG_INFO("TSR camera inverse: serial={} matrix=[{},{},{},{};{},{},{},{};{},{},{},{};{},{},{},{}]",
            serial,inv[0],inv[1],inv[2],inv[3],inv[4],inv[5],inv[6],inv[7],inv[8],inv[9],inv[10],inv[11],inv[12],inv[13],inv[14],inv[15]);
    }
}
inline void AuditXeSSCamera(uint32_t width,uint32_t height,float jitterX,float jitterY,
                           uint32_t initFlags,float exposureScale,ID3D12Resource* color,ID3D12Resource* depth,
                           uint32_t colorX,uint32_t colorY,uint32_t depthX,uint32_t depthY,bool inputReset) {
    static std::atomic_uint64_t calls=0;
    const auto call=++calls;
    std::optional<CameraObservation> observation;
    integration::CameraMatch match;size_t historySize=0;const auto now=GetTickCount64();
    {std::scoped_lock lock(cameraMutex);
        if(inputReset)cameraHistory.Clear();
        // Diagnostic sampling keeps matrix validation out of the ordinary hot path.
        if(call>3&&call%600!=0)return;
        observation=lastCamera;historySize=cameraHistory.Size();
        match=cameraHistory.Find({jitterX,jitterY},now,width,height,inputReset);}
    LOG_INFO("TSR camera history: call={} status={} matches={} size={} input_reset={} latest_serial={} matched_serial={} matched_frame={} matched_viewport={} matched_source={} age_ms={} serial_lag={} association=unverified own_neural=false",
        call,integration::CameraMatchName(match.status),match.matches,historySize,inputReset,
        observation?observation->serial:0,match.candidate?match.candidate->serial:0,match.candidate?match.candidate->camera.frame:0,
        match.candidate?match.candidate->camera.viewport:0,match.candidate?match.candidate->camera.source:0,
        match.candidate?now-match.candidate->milliseconds:0,
        observation&&match.candidate?observation->serial-match.candidate->serial:0);
    const auto colorDesc=color?color->GetDesc():D3D12_RESOURCE_DESC{};
    const auto depthDesc=depth?depth->GetDesc():D3D12_RESOURCE_DESC{};
    LOG_INFO("TSR relighting resources: call={} color={} depth={}",call,static_cast<void*>(color),static_cast<void*>(depth));
    LOG_INFO("TSR relighting bridge: call={} input=XeSS render={}x{} flags={} exposure_scale={} jitter={},{} color_format={} color_size={}x{} color_origin={},{} depth_format={} depth_size={}x{} depth_origin={},{} camera_observed={} frame_association=unverified viewport_association=unverified own_neural=false",
        call,width,height,initFlags,exposureScale,jitterX,jitterY,int(colorDesc.Format),colorDesc.Width,colorDesc.Height,colorX,colorY,
        int(depthDesc.Format),depthDesc.Width,depthDesc.Height,depthX,depthY,bool(observation));
    if(!observation)return;
    const auto& c=observation->camera;
    auto p=integration::ReadCameraProjection(c,width,height);
    LOG_INFO("TSR relighting camera candidate: call={} serial={} source=SL{} frame={} viewport={} age_ms={} projection_valid={} basis_valid={} jitter_delta={},{} fx={} fy={} cx={} cy={} depth_a={} depth_b={} view_z_sign={} inference=blocked_unverified_frame",
        call,observation->serial,c.source,c.frame,c.viewport,GetTickCount64()-observation->milliseconds,bool(p),integration::ValidCameraBasis(c),
        jitterX-c.jitter[0],jitterY-c.jitter[1],p?p->fx:0,p?p->fy:0,p?p->cx:0,p?p->cy:0,p?p->depthA:0,p?p->depthB:0,p?p->viewZSign:0);
}
}
