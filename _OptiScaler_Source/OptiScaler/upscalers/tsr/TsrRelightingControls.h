#pragma once
#include <atomic>
#include <mutex>
#include <string_view>
#include <Util.h>

namespace tsr::game {
inline std::atomic_bool relightingEnabled{false},relightingActive{false};
inline std::atomic<float> relightingStrength{.35f};
inline std::atomic<double> relightingGpuMs{-1};
inline std::atomic_uint64_t relightingFrames{0};
inline std::atomic<float> relightingSmoothing{1};
inline std::atomic<float> relightingColorTransfer{0};
inline std::atomic_bool relightingSceneryProtection{true};
inline std::atomic<float> relightingReach{80};
inline std::atomic_uint64_t relightingAnchorRevision{0};
inline std::atomic<const char*> relightingStatus{"Waiting for a frame"};
inline void SetRelightingStatus(std::string_view reason) {
    const char* text="Unsupported texture bindings";
    if(reason=="recorded")text="Running";
    else if(reason=="disabled")text="Disabled by control / zero intensity";
    else if(reason=="camera_or_resources_unmatched")text="Waiting for matching camera and textures";
    else if(reason=="submission_hooks_unavailable")text="GPU submission hooks unavailable";
    else if(reason=="nonlinear_color")text="Nonlinear input color is unsupported";
    else if(reason=="runtime_error")text="GPU initialization failed; check log";
    else if(reason=="gpu_slots_busy"||reason=="retirement_full")text="Waiting for GPU buffers";
    else if(reason=="list_already_registered")text="Command list already has recorded work";
    relightingStatus=text;
}
inline void InitRelightingControls() {
    static std::once_flag once;
    std::call_once(once,[]{
        const auto ini=Util::DllPath().parent_path()/L"OptiScaler.ini";
        relightingEnabled=GetPrivateProfileIntW(L"TSRRelighting",L"Enabled",0,ini.c_str())!=0;
        const UINT percent=GetPrivateProfileIntW(L"TSRRelighting",L"StrengthPercent",35,ini.c_str());
        relightingStrength=float((std::min)(percent,100u))/100.f;
        const UINT colorPercent=GetPrivateProfileIntW(L"TSRRelighting",L"ColorTransferPercent",0,ini.c_str());
        relightingColorTransfer=float((std::min)(colorPercent,100u))/100.f;
        relightingSceneryProtection=GetPrivateProfileIntW(L"TSRRelighting",L"SceneryProtection",1,ini.c_str())!=0;
        const UINT reach=GetPrivateProfileIntW(L"TSRRelighting",L"EffectReach",80,ini.c_str());
        relightingReach=float((std::clamp)(reach,20u,200u));
    });
}
inline void RelightingHotkey() {
    InitRelightingControls();
    static bool wasDown=false;
    DWORD foreground=0;GetWindowThreadProcessId(GetForegroundWindow(),&foreground);
    const bool down=foreground==GetCurrentProcessId()&&(GetAsyncKeyState(VK_F8)&0x8000);
    if(down&&!wasDown){relightingEnabled=!relightingEnabled.load();
        LOG_INFO("TSR relighting control: source=F8 enabled={}",relightingEnabled.load());}
    wasDown=down;
}
}
