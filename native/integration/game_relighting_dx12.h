#pragma once
#include "game_relighting_embedded.h"
#include "camera_contract.h"
#include "relighting_geometry.h"
#include "command_list_lifetime.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <string>

namespace tsr::integration {
// Production pass: records into the caller's list; never closes, submits or waits.
// Inputs must already be COMPUTE_READ. Output starts/ends COMPUTE_READ, including replay.
class GameRelighting {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    static void Check(HRESULT hr) {if(FAILED(hr))throw std::runtime_error("Relighting D3D12 failure "+std::to_string(hr));}
    struct Programs {
        Ptr<ID3D12Device> device;
        Ptr<ID3D12RootSignature> root;
        Ptr<ID3D12PipelineState> pso;
        Ptr<ID3D12Resource> weights;
        explicit Programs(ID3D12Device* d,bool cacheDepth):device(d) {
            D3D12_DESCRIPTOR_RANGE ranges[2]{};
            ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=2;
            ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=1;
            D3D12_ROOT_PARAMETER params[4]{};
            for(UINT i=0;i<2;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
            params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,28};
            params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[3].Descriptor.ShaderRegister=1;
            D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=params;
            Ptr<ID3DBlob> blob,errors;
            Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors));
            Check(d->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)));
            const D3D_SHADER_MACRO directDepth[]={{"TSR_DIRECT_DEPTH_READS","1"},{nullptr,nullptr}};
            const auto hr=D3DCompile(GameLightShader,sizeof(GameLightShader)-1,"TSRGameRelighting",cacheDepth?nullptr:directDepth,nullptr,
                "main","cs_5_1",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&blob,&errors);
            if(FAILED(hr)&&errors)throw std::runtime_error(std::string(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize()));
            Check(hr);
            D3D12_COMPUTE_PIPELINE_STATE_DESC pd{};pd.pRootSignature=root.Get();pd.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
            Check(d->CreateComputePipelineState(&pd,IID_PPV_ARGS(&pso)));
            weights=Buffer(d,1024,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
            void* data=nullptr;D3D12_RANGE noRead{};Check(weights->Map(0,&noRead,&data));
            std::memset(data,0,1024);std::memcpy(data,GameLightWeights,sizeof(GameLightWeights));weights->Unmap(0,nullptr);
        }
    };
    static Ptr<ID3D12Resource> Buffer(ID3D12Device* d,UINT64 bytes,D3D12_HEAP_TYPE type,D3D12_RESOURCE_STATES state) {
        D3D12_HEAP_PROPERTIES hp{};hp.Type=type;hp.CreationNodeMask=hp.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=bytes;
        rd.Height=rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        Ptr<ID3D12Resource> result;Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,state,nullptr,IID_PPV_ARGS(&result)));return result;
    }
    struct Slot final:SubmissionObserver::Sink {
        std::mutex mutex;
        bool recording=false,poisoned=false;
        struct Stamp {Ptr<ID3D12CommandQueue> queue;Ptr<ID3D12Fence> fence;UINT64 value=0;};
        std::vector<Stamp> stamps;
        std::shared_ptr<Programs> programs;
        Ptr<ID3D12Resource> color,depth,output,timing;
        Ptr<ID3D12DescriptorHeap> heap;
        Ptr<ID3D12QueryHeap> query;
        UINT width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;
        UINT64 frequency=0;bool timed=false;
        explicit Slot(std::shared_ptr<Programs> p):programs(std::move(p)) {}
        void Submitted(ID3D12CommandQueue* q,ID3D12CommandList*) noexcept override {
            std::lock_guard lock(mutex);
            try {
                auto it=std::find_if(stamps.begin(),stamps.end(),[q](const Stamp& s){return s.queue.Get()==q;});
                if(it==stamps.end()) {
                    if(stamps.size()>=8){poisoned=true;return;}
                    Stamp s;s.queue=q;Check(programs->device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&s.fence)));
                    stamps.push_back(std::move(s));it=stamps.end()-1;
                }
                Check(q->Signal(it->fence.Get(),++it->value));
                if(stamps.size()==1)Check(q->GetTimestampFrequency(&frequency));else frequency=0;
            }catch(...){poisoned=true;}
        }
        void Replayed(ID3D12CommandQueue* q,ID3D12CommandList* l) noexcept override {Submitted(q,l);}
        void Discarded(ID3D12CommandList*) noexcept override {std::lock_guard lock(mutex);recording=false;timed=false;}
        void RecordingEnded(ID3D12CommandList*) noexcept override {std::lock_guard lock(mutex);recording=false;}
        bool Ready() {
            std::lock_guard lock(mutex);
            if(recording||poisoned)return false;
            for(const auto& s:stamps) {
                auto completed=s.fence->GetCompletedValue();
                if(completed==UINT64_MAX || completed<s.value)return false;
            }
            return true;
        }
        double ReadTiming() {
            if(!timed||!frequency)return -1;
            UINT64* ticks=nullptr;D3D12_RANGE range{0,16};Check(timing->Map(0,&range,reinterpret_cast<void**>(&ticks)));
            const double ms=ticks[1]>=ticks[0]?double(ticks[1]-ticks[0])*1000/double(frequency):-1;
            D3D12_RANGE noWrite{};timing->Unmap(0,&noWrite);timed=false;return ms;
        }
        void Prepare(ID3D12Resource* c,ID3D12Resource* z,UINT w,UINT h,DXGI_FORMAT depthFormat) {
            auto* d=programs->device.Get();auto cd=c->GetDesc();
            if(!output||width!=w||height!=h||format!=cd.Format) {
                D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
                rd.Width=w;rd.Height=h;rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;
                rd.Format=cd.Format;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
                D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
                Ptr<ID3D12Resource> replacement;
                Check(d->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&replacement)));
                output=std::move(replacement);width=w;height=h;format=cd.Format;
            }
            if(!heap) {
                D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                Check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)));
                D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=2;
                Check(d->CreateQueryHeap(&qd,IID_PPV_ARGS(&query)));
                timing=Buffer(d,16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            }
            color=c;depth=z;
            auto cpu=heap->GetCPUDescriptorHandleForHeapStart();const auto stride=d->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for(unsigned i=0;i<2;++i) {
                D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=i?depthFormat:cd.Format;
                v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;
                v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                d->CreateShaderResourceView(i?z:c,&v,cpu);cpu.ptr+=stride;
            }
            D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=cd.Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
            d->CreateUnorderedAccessView(output.Get(),nullptr,&v,cpu);
        }
    };
    std::shared_ptr<Programs> programs;
    std::shared_ptr<SubmissionObserver> observer;
    std::vector<std::shared_ptr<Slot>> slots;
    // On feature destruction, pins alone do not cover submitted-but-unfinished work.
    // Keep retired slots until BOTH recording ends and every queue fence completes.
    struct Retirement {std::mutex mutex;std::vector<std::shared_ptr<Slot>> slots;};
    static Retirement& Retired() {static auto* state=new Retirement;return *state;}
public:
    double lastGpuMs=-1;
    explicit GameRelighting(ID3D12Device* d,std::shared_ptr<SubmissionObserver> o=SharedSubmissions(),bool cacheDepth=true):programs(std::make_shared<Programs>(d,cacheDepth)),observer(std::move(o)){}
    ~GameRelighting() {
        auto& retired=Retired();std::lock_guard lock(retired.mutex);
        for(auto& s:slots)if(!s->Ready())retired.slots.push_back(std::move(s));
    }
    // Fail before any commands are recorded; caller keeps the original color.
    ID3D12Resource* Record(ID3D12GraphicsCommandList* list,ID3D12Resource* color,ID3D12Resource* depth,
        UINT width,UINT height,const CameraProjection& p,float strength,std::string& reason,const LightingFrame& lighting=LightingFrame{}) {
        reason="unsupported_bindings";
        if(!ValidLightingFrame(lighting)||!list||!color||!depth||color==depth||list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT||
           !width||!height||width>16384||height>16384||!std::isfinite(strength)||strength<=0||strength>1||
           !std::isfinite(p.fx)||!std::isfinite(p.fy)||p.fx<=0||p.fy<=0||!std::isfinite(p.cx)||!std::isfinite(p.cy)||
           !std::isfinite(p.depthA)||!std::isfinite(p.depthB)||p.depthB==0)return nullptr;
        auto cd=color->GetDesc(),dd=depth->GetDesc();
        for(const auto& d:{cd,dd})if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D||d.Width!=width||d.Height!=height||
            d.DepthOrArraySize!=1||d.MipLevels!=1||d.SampleDesc.Count!=1||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE))return nullptr;
        if(cd.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT&&cd.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT)return nullptr;
        DXGI_FORMAT depthFormat=DXGI_FORMAT_UNKNOWN;
        if(dd.Format==DXGI_FORMAT_R32_FLOAT||dd.Format==DXGI_FORMAT_R32_TYPELESS)depthFormat=DXGI_FORMAT_R32_FLOAT;
        if(dd.Format==DXGI_FORMAT_R32G8X24_TYPELESS)depthFormat=DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
        if(depthFormat==DXGI_FORMAT_UNKNOWN)return nullptr;
        auto& retired=Retired();{
            std::lock_guard lock(retired.mutex);
            retired.slots.erase(std::remove_if(retired.slots.begin(),retired.slots.end(),[](auto& s){return s->Ready();}),retired.slots.end());
            if(retired.slots.size()>=24){reason="retirement_full";return nullptr;}
        }
        std::shared_ptr<Slot> slot;
        for(auto& s:slots)if(s->Ready()){slot=s;break;}
        if(!slot){if(slots.size()>=6){reason="gpu_slots_busy";return nullptr;}slot=std::make_shared<Slot>(programs);slots.push_back(slot);}
        const double timing=slot->ReadTiming();if(timing>=0)lastGpuMs=timing;
        slot->Prepare(color,depth,width,height,depthFormat);
        const HRESULT tracked=TrackListLifetime(list,observer);
        if(FAILED(tracked)&&tracked!=HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)){reason="list_lifetime_unavailable";return nullptr;}
        try {observer->Register(list,slot);}catch(const std::logic_error&){reason="list_already_registered";return nullptr;}
        slot->recording=true;slot->timed=true;
        // No fallible operation follows registration. Descriptors/constants stay immutable until retirement.
        struct Parameters {UINT w,h;float fx,fy,cx,cy,strength,limit,a,b,outputLimit,smoothing;std::array<float,12> transform;float colorTransfer,sceneryProtection,fadeStart,fadeEnd;};
        static_assert(sizeof(Parameters)==112);
        const Parameters params{width,height,p.fx,p.fy,p.cx,p.cy,strength,.02f,p.depthA,p.depthB,
            cd.Format==DXGI_FORMAT_R16G16B16A16_FLOAT?65504.f:3.4e38f,lighting.smoothing,lighting.normalTransform,
            lighting.colorTransfer,lighting.sceneryProtection,lighting.fadeStart,lighting.fadeEnd};
        D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition={slot->output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS};
        list->EndQuery(slot->query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        list->ResourceBarrier(1,&barrier);
        ID3D12DescriptorHeap* heaps[]={slot->heap.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(programs->root.Get());list->SetPipelineState(programs->pso.Get());
        auto gpu=slot->heap->GetGPUDescriptorHandleForHeapStart();
        list->SetComputeRootDescriptorTable(0,gpu);
        gpu.ptr+=2*UINT64(programs->device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));
        list->SetComputeRootDescriptorTable(1,gpu);list->SetComputeRoot32BitConstants(2,28,&params,0);
        list->SetComputeRootConstantBufferView(3,programs->weights->GetGPUVirtualAddress());
        list->Dispatch((width+7)/8,(height+7)/8,1);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);list->ResourceBarrier(1,&barrier);
        list->EndQuery(slot->query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        list->ResolveQueryData(slot->query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,slot->timing.Get(),0);
        reason="recorded";return slot->output.Get();
    }
};
}
