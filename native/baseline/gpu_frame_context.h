#pragma once
#include "input_adapter_pass.h"
#include "temporal_pass.h"
#include "output_adapter_pass.h"
#include "frame_slots.h"
#include <array>

// Caller owns the queue, fence, command list and external resource states.
// Never submits, waits, uploads or reads back. Destroy only after SafeToDestroy.
class GpuFrameContext {
public:
    using Lease=tsr::FrameSlots::Lease;
    struct Inputs { ID3D12Resource* color; ID3D12Resource* depth; ID3D12Resource* motion; ID3D12Resource* output; };
    // Caller keeps the heap alive through completion and resolves/reads after its fence.
    class Timing {
        ComPtr<ID3D12QueryHeap> heap;
    public:
        explicit Timing(ID3D12Device* device) {
            D3D12_QUERY_HEAP_DESC desc{};desc.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;desc.Count=4;
            Check(device->CreateQueryHeap(&desc,IID_PPV_ARGS(&heap)),"Stage queries");
        }
        ID3D12QueryHeap* Heap() const {return heap.Get();}
    };
private:
    ComPtr<ID3D12Device> device;
    tsr::Size renderSize,outputSize;
    InputAdapterPass adapter;
    TemporalPass temporal;
    OutputAdapterPass output;
    tsr::FrameSlots tracker;
    struct Slot {
        ComPtr<ID3D12Resource> packedColor,packedGeometry,historyColor,historyGeometry;
        ComPtr<ID3D12DescriptorHeap> heap;
        std::array<ComPtr<ID3D12Resource>,4> external;
        bool used=false;
    };
    std::vector<Slot> slots;
    tsr::FrameParameters previous{},recordedParameters{};
    bool recorded=false,recordedReuse=false;
    D3D12_RESOURCE_DESC Describe(ID3D12Resource* resource,bool writable) const {
        if(!resource)throw std::invalid_argument("Missing external texture");
        ComPtr<ID3D12Device> owner;Check(resource->GetDevice(IID_PPV_ARGS(&owner)),"External texture device");
        if(owner.Get()!=device.Get())throw std::invalid_argument("Texture belongs to another device");
        const auto d=resource->GetDesc();
        if(d.Dimension!=D3D12_RESOURCE_DIMENSION_TEXTURE2D || d.DepthOrArraySize!=1 || d.MipLevels!=1 || d.SampleDesc.Count!=1 ||
            d.Width>16384 || d.Height>16384 || (d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE) ||
            (writable && !(d.Flags&D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS)))
            throw std::invalid_argument("Unsupported texture layout/flags");
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{d.Format};
        Check(device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)),"External format support");
        if(writable ? !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) : !(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD))
            throw std::invalid_argument("Unsupported texture access");
        return d;
    }
public:
    GpuFrameContext(ID3D12Device* d,tsr::Size render,tsr::Size out,size_t count=3):device(d),renderSize(render),outputSize(out),
        adapter(d),temporal(d,true),output(d),tracker(count,true),slots(count) {
        tsr::Count(render);tsr::Count(out);
        if(out.width<render.width || out.height<render.height)throw std::invalid_argument("Downsampling unsupported");
        for(auto& slot:slots) {
            slot.packedColor=Texture(d,render,true);slot.packedGeometry=Texture(d,render,true);
            slot.historyColor=Texture(d,out,true);slot.historyGeometry=Texture(d,out,true);
            D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=13;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Check(d->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&slot.heap)),"Context descriptors");
        }
    }
    std::optional<Lease> Acquire(uint64_t completed) {return tracker.Acquire(completed);}
    uint64_t LastSubmitted() const {return tracker.LastSubmitted();}
    bool SafeToDestroy(uint64_t completed) const {return tracker.CanReconfigure(completed);}
    // Inputs are SRV, output is UAV on entry and remain so on exit. The caller
    // must order external producers/consumers and include all usage in the fence.
    void Record(const Lease& lease,ID3D12GraphicsCommandList* list,const Inputs& in,
                const tsr::FrameParameters& frame,const tsr::InputConversion& conversion,const tsr::OutputRegion& region,
                const Timing* timing=nullptr) {
        tracker.ValidateLease(lease);
        if(recorded)throw std::logic_error("Lease already recorded");
        if(!list)throw std::invalid_argument("Missing command list");
        ComPtr<ID3D12Device> owner;Check(list->GetDevice(IID_PPV_ARGS(&owner)),"Command list device");
        if(owner.Get()!=device.Get() || list->GetType()!=D3D12_COMMAND_LIST_TYPE_DIRECT)throw std::invalid_argument("Unsupported command list");
        if(!tsr::SameSize(frame.renderSize,renderSize) || !tsr::SameSize(frame.outputSize,outputSize) || !tsr::SameSize(conversion.size,renderSize))
            throw std::invalid_argument("Context dimensions changed; drain and recreate");
        if(in.output==in.color || in.output==in.depth || in.output==in.motion)throw std::invalid_argument("Input/output alias unsupported");
        const auto cd=Describe(in.color,false),dd=Describe(in.depth,false),md=Describe(in.motion,false),od=Describe(in.output,true);
        if((cd.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && cd.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT) || dd.Format!=DXGI_FORMAT_R32_FLOAT ||
           (md.Format!=DXGI_FORMAT_R16G16_FLOAT && md.Format!=DXGI_FORMAT_R32G32_FLOAT) ||
           (od.Format!=DXGI_FORMAT_R16G16B16A16_FLOAT && od.Format!=DXGI_FORMAT_R32G32B32A32_FLOAT))throw std::invalid_argument("Unsupported input/output format");
        const tsr::Size source{UINT(cd.Width),cd.Height},destination{UINT(od.Width),od.Height};
        if(dd.Width!=cd.Width || dd.Height!=cd.Height || md.Width!=cd.Width || md.Height!=cd.Height)throw std::invalid_argument("Input dimensions differ");
        tsr::ValidateConversion(conversion);tsr::ValidateRegion(source,renderSize,conversion.originX,conversion.originY);
        tsr::ValidateRegion(outputSize,region.size,region.sourceX,region.sourceY);tsr::ValidateRegion(destination,region.size,region.destinationX,region.destinationY);
        const bool reuse=tsr::CanReuse(lease.history.has_value(),previous,frame);
        temporal.ValidateParameters(frame,previous,reuse);
        if(timing) {
            if(!timing->Heap())throw std::invalid_argument("Missing timestamp heap");
            ComPtr<ID3D12Device> queryDevice;Check(timing->Heap()->GetDevice(IID_PPV_ARGS(&queryDevice)),"Query device");
            if(queryDevice.Get()!=device.Get())throw std::invalid_argument("Timestamp device mismatch");
        }
        // All fallible preconditions precede descriptor mutation and command recording.
        auto& slot=slots[lease.slot];
        slot.external={in.color,in.depth,in.motion,in.output};
        ID3D12Resource* oldC=reuse?slots[*lease.history].historyColor.Get():slot.packedColor.Get();
        ID3D12Resource* oldG=reuse?slots[*lease.history].historyGeometry.Get():slot.packedGeometry.Get();
        ID3D12Resource* resources[]={in.color,in.depth,in.motion,slot.packedColor.Get(),slot.packedGeometry.Get(),slot.packedColor.Get(),slot.packedGeometry.Get(),oldC,oldG,slot.historyColor.Get(),slot.historyGeometry.Get(),slot.historyColor.Get(),in.output};
        const UINT stride=device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        auto cpu=slot.heap->GetCPUDescriptorHandleForHeapStart();
        for(unsigned i=0;i<13;++i) {
            if(i==3 || i==4 || i==9 || i==10 || i==12){D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=resources[i]->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;device->CreateUnorderedAccessView(resources[i],nullptr,&v,cpu);}
            else {D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=resources[i]->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;device->CreateShaderResourceView(resources[i],&v,cpu);}
            cpu.ptr+=stride;
        }
        auto at=[&](unsigned i){auto h=slot.heap->GetGPUDescriptorHandleForHeapStart();h.ptr+=UINT64(i)*stride;return h;};
        const auto before=D3D12_RESOURCE_STATE_COMMON;
        auto stamp=[&](UINT i){if(timing)list->EndQuery(timing->Heap(),D3D12_QUERY_TYPE_TIMESTAMP,i);};
        stamp(0);
        for(auto* r:{slot.packedColor.Get(),slot.packedGeometry.Get(),slot.historyColor.Get(),slot.historyGeometry.Get()})Transition(list,r,before,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        adapter.Record(list,slot.heap.Get(),at(0),at(3),conversion,source);
        Transition(list,slot.packedColor.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,slot.packedGeometry.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        stamp(1);
        if(reuse)for(auto* r:{oldC,oldG})Transition(list,r,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        temporal.Record(list,{slot.heap.Get(),at(5),at(9)},frame,previous,reuse);
        Transition(list,slot.historyColor.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        Transition(list,slot.historyGeometry.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        stamp(2);
        output.Record(list,slot.heap.Get(),at(11),at(12),region,outputSize,destination);
        for(auto* r:{slot.packedColor.Get(),slot.packedGeometry.Get(),slot.historyColor.Get(),slot.historyGeometry.Get()})
            Transition(list,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        if(reuse)for(auto* r:{oldC,oldG})Transition(list,r,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        stamp(3);
        recordedParameters=frame;recordedReuse=reuse;recorded=true;
    }
    // Call only after successful submission and queue signal. No queue ownership.
    void Commit(const Lease& lease,uint64_t submittedFence) {
        tracker.ValidateLease(lease);if(!recorded)throw std::logic_error("No recorded frame");
        tracker.Commit(lease,submittedFence,recordedReuse);slots[lease.slot].used=true;
        previous=recordedParameters;recorded=false;
    }
    // Caller must discard the entire recorded command list before cancellation.
    void Cancel(const Lease& lease) {
        tracker.Cancel(lease);slots[lease.slot].external={};recorded=false;
    }
    void Replay(const Lease& lease,uint64_t fence) {tracker.Replay(lease,fence);}
    void EndRecording(const Lease& lease) {tracker.EndRecording(lease);}
};
