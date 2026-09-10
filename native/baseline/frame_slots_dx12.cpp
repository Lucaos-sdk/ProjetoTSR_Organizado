#include "frame_slots.h"
#include "temporal_pass.h"
#include "output_history_reference.h"
#include "test_texture_transfer.h"
#include <array>

static void Wait(ID3D12Fence* fence,UINT64 value) {
    if(fence->GetCompletedValue()==UINT64_MAX)throw std::runtime_error("Device removed");
    if(fence->GetCompletedValue()>=value)return;
    Event event;Check(fence->SetEventOnCompletion(value,event.handle),"Slot completion");
    if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Slot wait failed");
}
int main(int argc,char** argv) {
    try {
        bool warp=false,debug=false,full=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];if(arg=="--warp")warp=true;else if(arg=="--debug")debug=true;
            else if(arg=="--1080p")full=true;else throw std::invalid_argument("Usage: tsr_frame_slots_dx12 [--warp] [--debug] [--1080p]");
        }
        Context c(warp,debug,true);TemporalPass pass(c.device.Get(),true);tsr::FrameSlots tracker(3);
        const tsr::Size size=full?tsr::Size{1920,1080}:tsr::Size{19,11};
        ComPtr<ID3D12CommandAllocator> initAllocator;ComPtr<ID3D12GraphicsCommandList> init;
        Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&initAllocator)),"Init allocator");
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,initAllocator.Get(),nullptr,IID_PPV_ARGS(&init)),"Init list");
        auto fixture=tsr::FrameFixture(size,size);std::vector<tsr::Pixel> packed(tsr::Count(size),{10,0,0,10});
        Transfer current(c,init.Get(),size,DXGI_FORMAT_R32G32B32A32_FLOAT,fixture.color.data(),16);
        Transfer geometry(c,init.Get(),size,DXGI_FORMAT_R32G32B32A32_FLOAT,packed.data(),16);
        struct Slot {
            ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
            ComPtr<ID3D12DescriptorHeap> heap;
            std::unique_ptr<Transfer> color,geometry;
            tsr::TemporalResult expected;
            bool used=false,unchecked=false;
        };
        std::array<Slot,3> slots;
        for(auto& slot:slots) {
            Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)),"Slot allocator");
            Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list)),"Slot list");
            Check(slot.list->Close(),"Initial slot close");
            D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=6;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Check(c.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&slot.heap)),"Slot heap");
            slot.color=std::make_unique<Transfer>(c,init.Get(),size,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
            slot.geometry=std::make_unique<Transfer>(c,init.Get(),size,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
        }
        Check(init->Close(),"Init close");ComPtr<ID3D12Fence> initFence,fence,gate;
        Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&initFence)),"Init fence");
        Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Work fence");
        Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"Gate fence");
        ID3D12CommandList* setup[]={init.Get()};c.queue->ExecuteCommandLists(1,setup);Check(c.queue->Signal(initFence.Get(),1),"Init signal");Wait(initFence.Get(),1);
        // Cleanup releases the deterministic GPU gate and drains submitted work
        // before any slot resource dies, including during exception unwinding.
        struct Drain {
            Context& c;ID3D12Fence* gate;
            ~Drain(){gate->Signal(1);ComPtr<ID3D12Fence> f;if(SUCCEEDED(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))) && SUCCEEDED(c.queue->Signal(f.Get(),1))){try{Wait(f.Get(),1);}catch(...){}}}
        } drain{c,gate.Get()};
        Check(c.queue->Wait(gate.Get(),1),"Queue gate");
        tsr::OutputHistoryReference reference;tsr::FrameParameters previous{};
        unsigned reuses=0;
        auto validate=[&](Slot& slot) {
            if(!slot.unchecked)return;
            tsr::Verify(slot.color->Read(size),slot.expected.output);
            tsr::Verify(slot.geometry->Read(size),slot.expected.geometry);slot.unchecked=false;
        };
        for(unsigned frame=0;frame<9;++frame) {
            if(frame==3) {
                if(tracker.Acquire(fence->GetCompletedValue()))throw std::runtime_error("Busy GPU slots were released early");
                Check(gate->Signal(1),"Release gate");Wait(fence.Get(),tracker.LastSubmitted());
                std::cout<<"PASS three pending submissions refused early slot reuse\n";
            }
            auto lease=tracker.Acquire(fence->GetCompletedValue());
            if(!lease){Wait(fence.Get(),tracker.LastSubmitted());lease=tracker.Acquire(fence->GetCompletedValue());}
            if(!lease)throw std::runtime_error("No slot after completion");
            auto& slot=slots[lease->slot];validate(slot);
            Check(slot.allocator->Reset(),"Slot allocator reset");Check(slot.list->Reset(slot.allocator.Get(),nullptr),"Slot list reset");
            if(slot.used) {
                ++reuses;
                Transition(slot.list.Get(),slot.color->texture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Transition(slot.list.Get(),slot.geometry->texture.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            }
            auto frameInput=fixture;frameInput.parameters.frameIndex=frame;frameInput.parameters.reset=frame==0 || frame==5;
            frameInput.parameters.preExposure=frame%2?2.f:1.f;
            const bool reuse=tsr::CanReuse(frame!=0,previous,frameInput.parameters);
            slot.expected=reference.Run(frameInput);
            ID3D12Resource* oldColor=lease->history?slots[*lease->history].color->texture.Get():current.texture.Get();
            ID3D12Resource* oldGeometry=lease->history?slots[*lease->history].geometry->texture.Get():geometry.texture.Get();
            ID3D12Resource* resources[]={current.texture.Get(),geometry.texture.Get(),oldColor,oldGeometry,slot.color->texture.Get(),slot.geometry->texture.Get()};
            const auto stride=c.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);auto cpu=slot.heap->GetCPUDescriptorHandleForHeapStart();
            for(unsigned i=0;i<6;++i) {
                if(i<4){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Texture2D.MipLevels=1;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;c.device->CreateShaderResourceView(resources[i],&d,cpu);}
                else {D3D12_UNORDERED_ACCESS_VIEW_DESC d{};d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;d.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;c.device->CreateUnorderedAccessView(resources[i],nullptr,&d,cpu);}
                cpu.ptr+=stride;
            }
            auto gpu=slot.heap->GetGPUDescriptorHandleForHeapStart();pass.Record(slot.list.Get(),{slot.heap.Get(),gpu,{gpu.ptr+UINT64(4)*stride}},frameInput.parameters,previous,reuse);
            slot.color->CopyBack(slot.list.Get());slot.geometry->CopyBack(slot.list.Get());
            Transition(slot.list.Get(),slot.color->texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(slot.list.Get(),slot.geometry->texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Check(slot.list->Close(),"Slot close");ID3D12CommandList* commands[]={slot.list.Get()};c.queue->ExecuteCommandLists(1,commands);
            const UINT64 value=frame+1;Check(c.queue->Signal(fence.Get(),value),"Slot signal");tracker.Commit(*lease,value,reuse);
            slot.used=slot.unchecked=true;previous=frameInput.parameters;
        }
        Wait(fence.Get(),tracker.LastSubmitted());for(auto& slot:slots)validate(slot);
        if(reuses!=6 || !tracker.CanReconfigure(fence->GetCompletedValue()))throw std::runtime_error("Slot reuse/drain failed");
        Check(c.device->GetDeviceRemovedReason(),"Slot device removed");c.CheckDebugMessages();
        std::cout<<"PASS nine frames, three persistent slots, six resource reuses, CPU reference and safe drain\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
