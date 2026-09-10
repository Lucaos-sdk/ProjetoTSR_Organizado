#include "dx12_context.h"
#include "../integration/command_list_lifetime.h"
using namespace tsr::integration;
struct Counts {unsigned submitted=0,discarded=0,replayed=0;};
struct Counter final : SubmissionObserver::Sink {
    Counts& count;
    explicit Counter(Counts& c):count(c){}
    void Submitted(ID3D12CommandQueue*,ID3D12CommandList*) noexcept override {++count.submitted;}
    void Discarded(ID3D12CommandList*) noexcept override {++count.discarded;}
    void Replayed(ID3D12CommandQueue*,ID3D12CommandList*) noexcept override {++count.replayed;}
    void RecordingEnded(ID3D12CommandList*) noexcept override {}
};
static void Require(bool ok) {if(!ok)throw std::runtime_error("List lifetime assertion failed");}
int main(int argc,char** argv) {
    try {
        bool warp=false;
        for(int i=1;i<argc;++i){if(std::string(argv[i])=="--warp")warp=true;else throw std::invalid_argument("Expected --warp");}
        Context c(warp,true,true);
        Counts counts;
        auto observer=std::make_shared<SubmissionObserver>();
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
        Check(TrackListLifetime(list.Get(),observer),"Lifetime attachment");
        observer->Register(list.Get(),std::make_shared<Counter>(counts));
        Require(FAILED(TrackListLifetime(list.Get(),observer)));
        Require(counts.discarded==0);
        Check(list->Close(),"Close");
        list.Reset(); // Actual last COM Release, not a simulated callback.
        Require(counts.discarded==1);
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Second list");
        Check(TrackListLifetime(list.Get(),observer),"Second lifetime attachment");
        observer->Register(list.Get(),std::make_shared<Counter>(counts));
        Check(list->Close(),"Second close");
        ComPtr<ID3D12Fence> fence;
        Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
        // Empty commands: safe to replay, so test the observer without replaying TSR barriers.
        ID3D12CommandList* commands[]={list.Get()};
        for(unsigned i=0;i<2;++i){
            c.queue->ExecuteCommandLists(1,commands);
            observer->NotifySubmitted(c.queue.Get(),1,commands);
            Check(c.queue->Signal(fence.Get(),i+1),"Signal");
            Event event;Check(fence->SetEventOnCompletion(i+1,event.handle),"Completion");
            if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Timeout");
        }
        Require(counts.submitted==1 && counts.replayed==1);
        std::weak_ptr<SubmissionObserver> weak=observer;
        observer.reset();Require(!weak.expired()); // Sentinel retains its observer safely.
        list.Reset();Require(weak.expired());
        Require(counts.discarded==1); // Submitted work must not be cancelled on destruction.
        c.CheckDebugMessages();
        std::cout<<"PASS actual COM destruction, attachment collision, observer ownership and repeated submission\n";
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
