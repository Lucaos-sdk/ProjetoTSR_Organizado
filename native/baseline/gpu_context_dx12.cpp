#include "gpu_frame_context.h"
#include "temporal_pass.h"
#include "output_history_reference.h"
#include "test_texture_transfer.h"
#include <array>
#include <fstream>
#include <iomanip>
#include "../integration/submission_observer.h"
#include "../integration/command_list_lifetime.h"
#include "test_ngx_parameters.h"

struct FrameSubmission final : tsr::integration::SubmissionObserver::Sink {
    GpuFrameContext& context;
    GpuFrameContext::Lease lease;
    ComPtr<ID3D12Fence> fence;
    UINT64 value;
    std::exception_ptr error;
    bool submitted=false;
    bool discarded=false;
    unsigned replays=0;
    ComPtr<ID3D12CommandQueue> submittedQueue;
    FrameSubmission(GpuFrameContext& c,GpuFrameContext::Lease l,ID3D12Fence* f,UINT64 v)
        :context(c),lease(l),fence(f),value(v){}
    void Submitted(ID3D12CommandQueue* queue,ID3D12CommandList*) noexcept override {
        try {Check(queue->Signal(fence.Get(),value),"Observed queue signal");context.Commit(lease,value);submitted=true;submittedQueue=queue;}
        catch(...) {error=std::current_exception();}
    }
    void Discarded(ID3D12CommandList*) noexcept override {
        try { context.Cancel(lease);discarded=true; }
        catch(...) {error=std::current_exception();}
    }
    void Replayed(ID3D12CommandQueue* queue,ID3D12CommandList*) noexcept override {
        try {
            if(queue!=submittedQueue.Get())throw std::runtime_error("Cross-queue replay unsupported");
            const auto next=context.LastSubmitted()+1;
            Check(queue->Signal(fence.Get(),next),"Replay signal");context.Replay(lease,next);++replays;
        }
        catch(...) {error=std::current_exception();}
    }
    void RecordingEnded(ID3D12CommandList*) noexcept override {
        try {context.EndRecording(lease);}
        catch(...) {error=std::current_exception();}
    }
};

static void Wait(ID3D12Fence* fence,UINT64 value) {
    if(fence->GetCompletedValue()==UINT64_MAX)throw std::runtime_error("Device removed");
    if(fence->GetCompletedValue()>=value)return;
    Event event;Check(fence->SetEventOnCompletion(value,event.handle),"Slot completion");
    if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Slot wait failed");
}
int main(int argc,char** argv) {
    try {
        bool warp=false,debug=false,full=false,fp16=false,currentOnly=false;
        std::string benchmark;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];if(arg=="--warp")warp=true;else if(arg=="--debug")debug=true;
            else if(arg=="--benchmark" && i+1<argc)benchmark=argv[++i];
            else if(arg=="--1080p")full=true;else if(arg=="--fp16")fp16=true;else if(arg=="--current-frame-only")currentOnly=true;else throw std::invalid_argument("Usage: tsr_gpu_context_dx12 [--warp] [--debug] [--1080p] [--fp16] [--current-frame-only] [--benchmark CSV]");
        }
        Context c(warp,debug,true);
        const tsr::Size render=full?tsr::Size{1280,720}:tsr::Size{19,11};
        const tsr::Size size=full?tsr::Size{1920,1080}:tsr::Size{29,17};
        GpuFrameContext tracker(c.device.Get(),render,size,3);
        ComPtr<ID3D12CommandAllocator> initAllocator;ComPtr<ID3D12GraphicsCommandList> init;
        Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&initAllocator)),"Init allocator");
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,initAllocator.Get(),nullptr,IID_PPV_ARGS(&init)),"Init list");
        auto fixture=tsr::FrameFixture(render,size);
        std::vector<uint16_t> half(fixture.color.size()*4),halfMotion(fixture.color.size()*2,0);
        for(size_t i=0;i<fixture.color.size();++i)for(unsigned ch=0;ch<4;++ch){
            half[i*4+ch]=DirectX::PackedVector::XMConvertFloatToHalf(fixture.color[i][ch]);
            if(fp16)fixture.color[i][ch]=DirectX::PackedVector::XMConvertHalfToFloat(half[i*4+ch]);
        }
        const auto format=fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        Transfer current(c,init.Get(),render,format,fp16?static_cast<const void*>(half.data()):fixture.color.data(),fp16?8:16);
        Transfer depth(c,init.Get(),render,DXGI_FORMAT_R32_FLOAT,fixture.depth.data(),4);
        Transfer motion(c,init.Get(),render,fp16?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32G32_FLOAT,fp16?static_cast<const void*>(halfMotion.data()):fixture.motion.data(),fp16?4:8);
        struct Slot {
            ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
            std::unique_ptr<Transfer> color;
            std::unique_ptr<GpuFrameContext::Timing> timing;
            ComPtr<ID3D12Resource> timestamps;
            std::shared_ptr<FrameSubmission> submission;
            tsr::TemporalResult expected;
            bool used=false,unchecked=false;
        };
        std::array<Slot,3> slots;
        for(auto& slot:slots) {
            Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&slot.allocator)),"Slot allocator");
            Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list)),"Slot list");
            Check(tsr::integration::TrackListLifetime(slot.list.Get()),"Track slot lifetime");
            Check(slot.list->Close(),"Initial slot close");
            slot.color=std::make_unique<Transfer>(c,init.Get(),size,format,nullptr,fp16?8:16);
            if(!benchmark.empty()) {
                slot.timing=std::make_unique<GpuFrameContext::Timing>(c.device.Get());
                slot.timestamps=Buffer(c.device.Get(),4*sizeof(UINT64),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            }
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
        tsr::OutputHistoryReference reference;
        unsigned reuses=0;
        Slot* lastSlot=nullptr;
        auto validate=[&](Slot& slot) {
            if(!slot.unchecked)return;
            const auto actual=slot.color->Read(size);
            if(!fp16)tsr::Verify(actual,slot.expected.output);
            else for(size_t i=0;i<actual.size();++i)for(unsigned ch=0;ch<4;++ch){
                const float expected=slot.expected.output[i][ch];
                const float tolerance=.0001f+std::abs(expected)*.001f;
                if(!std::isfinite(actual[i][ch]) || std::abs(actual[i][ch]-expected)>tolerance)
                    throw std::runtime_error("Context FP16 output mismatch");
            }
            slot.unchecked=false;
        };
        auto endRecordings=[&] {
            Wait(fence.Get(),tracker.LastSubmitted());
            for(auto& slot:slots)if(slot.used) {
                Check(slot.list->Reset(slot.allocator.Get(),nullptr),"Retire recording reset");
                tsr::integration::Submissions().NotifyReset(slot.list.Get(),true);
                Check(slot.list->Close(),"Retire recording close");
            }
        };
        auto replay=[&](Slot& slot) {
            // DX12 requires the previous execution of this command list to finish.
            Wait(fence.Get(),tracker.LastSubmitted());
            const auto before=tracker.LastSubmitted();
            const auto previousReplays=slot.submission->replays;
            ID3D12CommandList* commands[]={slot.list.Get()};
            c.queue->ExecuteCommandLists(1,commands);
            tsr::integration::Submissions().NotifySubmitted(c.queue.Get(),1,commands);
            if(slot.submission->error)std::rethrow_exception(slot.submission->error);
            if(slot.submission->replays!=previousReplays+1 || tracker.LastSubmitted()!=before+1)
                throw std::runtime_error("Replay fence missing");
        };
        for(unsigned frame=0;frame<9;++frame) {
            if(frame==3) {
                if(tracker.Acquire(fence->GetCompletedValue()))throw std::runtime_error("Busy GPU slots were released early");
                Check(gate->Signal(1),"Release gate");Wait(fence.Get(),tracker.LastSubmitted());
                if(tracker.Acquire(fence->GetCompletedValue()))throw std::runtime_error("Completed but replayable buffers were released");
                for(auto& slot:slots)replay(slot);
                replay(slots[1]); // Old history producer after the latest logical frame.
                std::cout<<"PASS three pending submissions refused early slot reuse\n";
            }
            const UINT64 submittedBefore=tracker.LastSubmitted();
            auto lease=tracker.Acquire(fence->GetCompletedValue());
            if(!lease){endRecordings();lease=tracker.Acquire(fence->GetCompletedValue());}
            if(!lease)throw std::runtime_error("No slot after completion");
            auto& slot=slots[lease->slot];validate(slot);
            Check(slot.allocator->Reset(),"Slot allocator reset");Check(slot.list->Reset(slot.allocator.Get(),nullptr),"Slot list reset");
            tsr::integration::Submissions().NotifyReset(slot.list.Get(),true);
            if(slot.used) {
                ++reuses;
            }
            auto frameInput=fixture;frameInput.parameters.frameIndex=frame;frameInput.parameters.reset=currentOnly || frame==0 || frame==5;
            frameInput.parameters.preExposure=frame%2?2.f:1.f;
            slot.expected=reference.Run(frameInput);
            tsr::InputConversion conversion{};conversion.size=render;conversion.fixedCamera=1;
            GpuFrameContext::Inputs in{current.texture.Get(),depth.texture.Get(),motion.texture.Get(),slot.color->texture.Get()};
            if(frame==0) {
                unsigned rejected=0;
                auto bad=in;bad.motion=current.texture.Get();
                try{tracker.Record(*lease,slot.list.Get(),bad,frameInput.parameters,conversion,{size});}catch(const std::invalid_argument&){++rejected;}
                try{tracker.Record(*lease,slot.list.Get(),in,frameInput.parameters,conversion,{size,0,0,1,0});}catch(const std::invalid_argument&){++rejected;}
                if(rejected!=2)throw std::runtime_error("Context accepted invalid resources/region");
                tracker.Record(*lease,slot.list.Get(),in,frameInput.parameters,conversion,{size});
                bool duplicateRejected=false;
                try{tracker.Record(*lease,slot.list.Get(),in,frameInput.parameters,conversion,{size});}catch(const std::logic_error&){duplicateRejected=true;}
                if(!duplicateRejected)throw std::runtime_error("Context recorded lease twice");
                // Discard commands before cancelling. Internal states must remain COMMON.
                auto discardedSubmission=std::make_shared<FrameSubmission>(tracker,*lease,fence.Get(),1);
                tsr::integration::Submissions().Register(slot.list.Get(),discardedSubmission);
                Check(slot.list->Close(),"Discard close");Check(slot.allocator->Reset(),"Discard allocator");
                const HRESULT resetResult=slot.list->Reset(slot.allocator.Get(),nullptr);
                tsr::integration::Submissions().NotifyReset(slot.list.Get(),SUCCEEDED(resetResult));
                Check(resetResult,"Discard list");
                if(discardedSubmission->error)std::rethrow_exception(discardedSubmission->error);
                if(!discardedSubmission->discarded || discardedSubmission->submitted || tracker.LastSubmitted()!=0)
                    throw std::runtime_error("Discard notification failed");
                lease=tracker.Acquire(0);
                if(!lease)throw std::runtime_error("Cancelled context could not reacquire");
                tracker.Record(*lease,slot.list.Get(),in,frameInput.parameters,conversion,{size});
                auto destroyed=std::make_shared<FrameSubmission>(tracker,*lease,fence.Get(),1);
                tsr::integration::Submissions().Register(slot.list.Get(),destroyed);
                Check(slot.list->Close(),"Destroy unsubmitted close");
                slot.list.Reset(); // Final COM Release invokes the private-data sentinel.
                if(destroyed->error)std::rethrow_exception(destroyed->error);
                if(!destroyed->discarded || tracker.LastSubmitted()!=0)throw std::runtime_error("Destroyed frame was not cancelled");
                Check(slot.allocator->Reset(),"Destroyed allocator reset");
                Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,slot.allocator.Get(),nullptr,IID_PPV_ARGS(&slot.list)),"Replacement list");
                Check(tsr::integration::TrackListLifetime(slot.list.Get()),"Replacement lifetime");
                lease=tracker.Acquire(0);
                if(!lease)throw std::runtime_error("Destroyed frame could not reacquire");
                std::cout<<"PASS actual destruction of unsubmitted temporal work recovered its slot\n";
                std::cout<<"PASS invalid format/region, duplicate recording and discarded frame recovery\n";
            }
            if(frame==4) {
                const auto discardedSlot=lease->slot;
                tracker.Record(*lease,slot.list.Get(),in,frameInput.parameters,conversion,{size});
                auto cancelled=std::make_shared<FrameSubmission>(tracker,*lease,fence.Get(),frame+1);
                tsr::integration::Submissions().Register(slot.list.Get(),cancelled);
                Check(slot.list->Close(),"History discard close");
                Check(slot.allocator->Reset(),"History discard allocator");
                const HRESULT reset=slot.list->Reset(slot.allocator.Get(),nullptr);
                tsr::integration::Submissions().NotifyReset(slot.list.Get(),SUCCEEDED(reset));
                Check(reset,"History discard reset");
                if(cancelled->error)std::rethrow_exception(cancelled->error);
                if(!cancelled->discarded || tracker.LastSubmitted()!=submittedBefore)
                    throw std::runtime_error("Discard changed submitted history");
                lease=tracker.Acquire(fence->GetCompletedValue());
                if(!lease || lease->slot!=discardedSlot)throw std::runtime_error("Discarded slot not reusable");
                std::cout<<"PASS discarded later frame preserved submitted history\n";
            }
            auto ngx=MakeNgxParameters(frameInput.parameters,in.color,in.depth,in.motion,in.output);
            tsr::integration::NgxConventions conventions{render,size};
            conventions.frameIndex=frame;conventions.linearColorConfirmed=conventions.linearDepthConfirmed=true;
            conventions.fixedCameraConfirmed=!currentOnly; // Known only for this synthetic fixture.
            const auto mapped=tsr::integration::ReadNgxFrame(ngx,conventions);
            tracker.Record(*lease,slot.list.Get(),{mapped.color,mapped.depth,mapped.motion,mapped.output},mapped.frame,mapped.conversion,mapped.region,slot.timing.get());
            if(slot.timing)slot.list->ResolveQueryData(slot.timing->Heap(),D3D12_QUERY_TYPE_TIMESTAMP,0,4,slot.timestamps.Get(),0);
            slot.color->CopyBack(slot.list.Get());
            Transition(slot.list.Get(),slot.color->texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
            Check(slot.list->Close(),"Slot close");ID3D12CommandList* commands[]={slot.list.Get()};
            auto submission=std::make_shared<FrameSubmission>(tracker,*lease,fence.Get(),submittedBefore+1);
            slot.submission=submission;
            tsr::integration::Submissions().Register(slot.list.Get(),submission);
            if(tracker.LastSubmitted()!=submittedBefore)throw std::runtime_error("Frame committed before real submission");
            c.queue->ExecuteCommandLists(1,commands);
            // Same entry point called by both OptiScaler ExecuteCommandLists hook paths.
            tsr::integration::Submissions().NotifySubmitted(c.queue.Get(),1,commands);
            if(submission->error)std::rethrow_exception(submission->error);
            if(!submission->submitted)throw std::runtime_error("Submission notification missing");
            if(frame>=3)replay(slot);
            slot.used=slot.unchecked=true;
            lastSlot=&slot;
        }
        Wait(fence.Get(),tracker.LastSubmitted());for(auto& slot:slots)validate(slot);
        if(!benchmark.empty()) {
            UINT64 frequency=0;Check(c.queue->GetTimestampFrequency(&frequency),"Timestamp frequency");
            if(!frequency)throw std::runtime_error("Zero timestamp frequency");
            std::ofstream csv(benchmark);if(!csv)throw std::runtime_error("Cannot open benchmark CSV");
            csv<<"sample,input_ms,temporal_ms,output_ms,total_ms\n"<<std::setprecision(10);
            std::array<std::vector<double>,4> measurements;
            for(unsigned sample=0;sample<120;++sample) {
                replay(*lastSlot);Wait(fence.Get(),tracker.LastSubmitted());
                UINT64* data=nullptr;D3D12_RANGE range{0,4*sizeof(UINT64)};
                Check(lastSlot->timestamps->Map(0,&range,reinterpret_cast<void**>(&data)),"Read completed timestamps");
                std::array<UINT64,4> ticks{data[0],data[1],data[2],data[3]};
                D3D12_RANGE noWrite{0,0};lastSlot->timestamps->Unmap(0,&noWrite);
                if(!std::is_sorted(ticks.begin(),ticks.end()))throw std::runtime_error("Non-monotonic GPU timestamps");
                if(sample<20)continue;
                const std::array<double,4> ms{(ticks[1]-ticks[0])*1000.0/frequency,(ticks[2]-ticks[1])*1000.0/frequency,
                    (ticks[3]-ticks[2])*1000.0/frequency,(ticks[3]-ticks[0])*1000.0/frequency};
                csv<<sample-20;
                for(unsigned stage=0;stage<4;++stage){if(!std::isfinite(ms[stage]))throw std::runtime_error("Invalid GPU duration");measurements[stage].push_back(ms[stage]);csv<<','<<ms[stage];}
                csv<<'\n';
            }
            csv.close();if(!csv)throw std::runtime_error("Benchmark write failed");
            const char* labels[]={"input","temporal","output","total"};
            std::cout<<"GPU fixed-frame replay: 20 warmups, 100 samples; "<<render.width<<'x'<<render.height<<" -> "<<size.width<<'x'<<size.height
                     <<"; external "<<(fp16?"FP16":"FP32")<<"; current-only="<<currentOnly<<"; debug="<<debug<<"; frequency="<<frequency<<" Hz\n";
            for(unsigned stage=0;stage<4;++stage){auto& m=measurements[stage];std::sort(m.begin(),m.end());std::cout<<labels[stage]<<" median="<<(m[49]+m[50])/2<<" ms p95="<<m[94]<<" ms\n";}
            lastSlot->unchecked=true;validate(*lastSlot);
        }
        if(tracker.SafeToDestroy(fence->GetCompletedValue()))throw std::runtime_error("Live recordings permitted destruction");
        endRecordings();
        if(reuses!=6 || !tracker.SafeToDestroy(fence->GetCompletedValue()))throw std::runtime_error("Slot reuse/drain failed");
        Check(c.device->GetDeviceRemovedReason(),"Slot device removed");c.CheckDebugMessages();
        std::cout<<"PASS unified context: nine logical frames, nineteen executions including older history replay, retained recordings, six slot reuses and CPU reference\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
