#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include <stdexcept>
namespace tsr {
// One context, one monotonic queue/fence timeline; externally serialized calls.
// The most recent history is pinned even after completion. A consumer extends
// its source slot's retirement fence so CPU writes cannot race GPU reads.
class FrameSlots {
public:
    struct Lease { size_t slot; uint64_t token; std::optional<size_t> history; const FrameSlots* owner; };
private:
    std::vector<uint64_t> retire;
    struct Recording { Lease lease; bool readsHistory; };
    std::vector<std::optional<Recording>> recordings;
    std::vector<unsigned> pins;
    bool keepRecordings;
    std::optional<size_t> latest;
    std::optional<Lease> pending;
    uint64_t serial=0,lastSubmitted=0,lastCompleted=0;
    void CheckLease(const Lease& lease) const {
        if(lease.owner!=this || !pending || pending->slot!=lease.slot || pending->token!=lease.token || pending->history!=lease.history)
            throw std::logic_error("Invalid or stale frame lease");
    }
public:
    explicit FrameSlots(size_t count,bool retainRecordings=false):retire(count,0),recordings(count),pins(count,0),keepRecordings(retainRecordings) {
        if(count<2 || count>16)throw std::invalid_argument("Frame slots must be in 2..16");
    }
    FrameSlots(const FrameSlots&)=delete;
    FrameSlots& operator=(const FrameSlots&)=delete;
    std::optional<Lease> Acquire(uint64_t completed) {
        if(completed==UINT64_MAX)throw std::runtime_error("Fence reports device removal");
        if(completed<lastCompleted || completed>lastSubmitted)throw std::invalid_argument("Wrong completion timeline");
        if(pending)throw std::logic_error("Only one recording lease may be outstanding");
        lastCompleted=completed;
        for(size_t i=0;i<retire.size();++i) if(latest!=i && !pins[i] && retire[i]<=completed) {
            if(serial==UINT64_MAX)throw std::overflow_error("Frame lease overflow");
            pending=Lease{i,++serial,latest,this};return pending;
        }
        return std::nullopt; // Caller may wait or defer; no resource may be overwritten.
    }
    void Commit(const Lease& lease,uint64_t submittedFence,bool readsHistory) {
        CheckLease(lease);
        if(!submittedFence || submittedFence==UINT64_MAX || submittedFence<=lastSubmitted)
            throw std::invalid_argument("Submission fence must increase");
        if(readsHistory && !lease.history)throw std::invalid_argument("No history to consume");
        retire[lease.slot]=submittedFence;
        if(readsHistory)retire[*lease.history]=submittedFence;
        if(keepRecordings) {
            recordings[lease.slot]=Recording{lease,readsHistory};
            ++pins[lease.slot];
            if(readsHistory)++pins[*lease.history];
        }
        latest=lease.slot;lastSubmitted=submittedFence;pending.reset();
    }
    const Recording& CheckRecording(const Lease& lease) const {
        if(lease.owner!=this || lease.slot>=recordings.size() || !recordings[lease.slot] ||
           recordings[lease.slot]->lease.token!=lease.token || recordings[lease.slot]->lease.history!=lease.history)
            throw std::logic_error("Invalid or ended recording");
        return *recordings[lease.slot];
    }
    // Same queue only. A replay is additional GPU use, not a new logical frame.
    void Replay(const Lease& lease,uint64_t fence) {
        const auto& r=CheckRecording(lease);
        if(!fence || fence==UINT64_MAX || fence<=lastSubmitted)throw std::invalid_argument("Replay fence must increase");
        retire[lease.slot]=fence;
        if(r.readsHistory)retire[*lease.history]=fence;
        lastSubmitted=fence;
    }
    // Reset/destruction ends possible future replay; submitted uses still require their fence.
    void EndRecording(const Lease& lease) {
        const auto& r=CheckRecording(lease);
        --pins[lease.slot];if(r.readsHistory)--pins[*lease.history];
        recordings[lease.slot].reset();
    }
    // Only legal when the caller discarded unsubmitted commands for this lease.
    void Cancel(const Lease& lease) { CheckLease(lease);pending.reset(); }
    void ValidateLease(const Lease& lease) const { CheckLease(lease); }
    uint64_t LastSubmitted() const {return lastSubmitted;}
    // Resize/destruction must wait for all submitted consumers, not just producers.
    bool CanReconfigure(uint64_t completed) const {
        for(auto pin:pins)if(pin)return false;
        return completed!=UINT64_MAX && !pending && completed==lastSubmitted;
    }
};
}
