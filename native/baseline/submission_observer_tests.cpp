#include "../integration/submission_observer.h"
#include <iostream>
using tsr::integration::SubmissionObserver;
static void Require(bool ok) { if (!ok) throw std::runtime_error("Submission observer check failed"); }
struct Sink : SubmissionObserver::Sink {
    unsigned calls=0;
    unsigned discards=0;
    unsigned replays=0;
    ID3D12CommandQueue* queue=nullptr;
    ID3D12CommandList* list=nullptr;
    void Submitted(ID3D12CommandQueue* q, ID3D12CommandList* l) noexcept override { ++calls;queue=q;list=l; }
    void Discarded(ID3D12CommandList* l) noexcept override { ++discards;list=l; }
    void Replayed(ID3D12CommandQueue*,ID3D12CommandList*) noexcept override {++replays;}
    void RecordingEnded(ID3D12CommandList*) noexcept override {}
};
int main() {
    try {
        SubmissionObserver observer;
        // Opaque identities only; CPU tests never dereference DX12 pointers.
        int a=0,b=0,q=0;
        auto list=reinterpret_cast<ID3D12CommandList*>(&a);
        auto other=reinterpret_cast<ID3D12CommandList*>(&b);
        auto queue=reinterpret_cast<ID3D12CommandQueue*>(&q);
        auto sink=std::make_shared<Sink>();
        auto ticket=observer.Register(list,sink);
        SubmissionObserver otherObserver;
        auto otherTicket=otherObserver.Register(list,sink);
        Require(!observer.Cancel(otherTicket));
        Require(otherObserver.Cancel(otherTicket));
        Require(sink->calls==0);
        bool duplicate=false;
        try { observer.Register(list,sink); } catch(const std::logic_error&) { duplicate=true; }
        Require(duplicate);
        ID3D12CommandList* unrelated[]={other};
        observer.NotifySubmitted(queue,1,unrelated);Require(sink->calls==0);
        Require(observer.Cancel(ticket));
        auto replacement=observer.Register(list,sink);
        Require(!observer.Cancel(ticket)); // Stale ticket must not cancel a new recording.
        ID3D12CommandList* batch[]={other,list,list};
        observer.NotifySubmitted(queue,3,batch);
        Require(sink->calls==1 && sink->queue==queue && sink->list==list);
        observer.NotifySubmitted(queue,3,batch);Require(sink->calls==1);
        Require(sink->replays==3);
        bool liveRejected=false;
        try {observer.Register(list,sink);}catch(const std::logic_error&){liveRejected=true;}
        Require(liveRejected);
        Require(!observer.Cancel(replacement));
        observer.NotifyReset(list,true);
        auto retained=std::make_shared<Sink>();std::weak_ptr<Sink> weak=retained;
        auto hold=observer.Register(other,retained);retained.reset();Require(!weak.expired());
        Require(observer.Cancel(hold));Require(weak.expired());
        auto resetSink=std::make_shared<Sink>();
        auto resetTicket=observer.Register(list,resetSink);
        observer.NotifyReset(list,false);
        Require(resetSink->discards==0);
        observer.NotifySubmitted(queue,3,batch);
        Require(resetSink->calls==1); // A failed Reset must keep the pending frame.
        observer.NotifyReset(list,true);
        observer.Register(list,resetSink);
        observer.NotifyReset(list,true);
        Require(resetSink->discards==1 && !observer.Cancel(resetTicket));
        observer.NotifyReset(list,true);
        observer.NotifySubmitted(queue,3,batch);
        Require(resetSink->discards==1 && resetSink->calls==1);
        observer.Register(list,resetSink);
        observer.NotifySubmitted(queue,3,batch);
        observer.NotifyReset(list,true);
        Require(resetSink->calls==2 && resetSink->discards==1); // Submitted GPU work is never cancelled by Reset.
        observer.Register(list,resetSink);
        observer.NotifyDestroyed(list);
        Require(resetSink->discards==2);
        observer.Register(list,resetSink); // Recycled address starts a new recording.
        observer.NotifySubmitted(queue,1,&list);
        observer.NotifyDestroyed(list);
        Require(resetSink->calls==3 && resetSink->discards==2);
        std::cout<<"PASS submission routing, duplicate/stale tickets, one-shot delivery and retained lifetime\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
