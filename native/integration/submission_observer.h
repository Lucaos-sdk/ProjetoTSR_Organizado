#pragma once
#include <cstdint>
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

struct ID3D12CommandQueue;
struct ID3D12CommandList;

namespace tsr::integration {
// Keep the recording identity until successful Reset or object destruction.
// Callbacks are AFTER execution: replay detection cannot undo submitted work.
// Sinks must separately retain GPU resources until all fences complete.
class SubmissionObserver {
public:
    struct Sink {
        virtual ~Sink() = default;
        virtual void Submitted(ID3D12CommandQueue*, ID3D12CommandList*) noexcept = 0;
        virtual void Discarded(ID3D12CommandList*) noexcept = 0;
        virtual void Replayed(ID3D12CommandQueue*, ID3D12CommandList*) noexcept = 0;
        virtual void RecordingEnded(ID3D12CommandList*) noexcept = 0;
    };
    struct Ticket { ID3D12CommandList* list; uint64_t generation; const SubmissionObserver* owner; };
private:
    struct Entry { uint64_t generation; std::shared_ptr<Sink> sink; bool submitted=false; };
    std::mutex mutex;
    std::unordered_map<ID3D12CommandList*, Entry> pending;
    uint64_t generation = 0;
    std::atomic<bool> active{false};
public:
    Ticket Register(ID3D12CommandList* list, std::shared_ptr<Sink> sink) {
        if (!list || !sink) throw std::invalid_argument("Missing submission list/sink");
        std::lock_guard<std::mutex> lock(mutex);
        if (pending.count(list)) throw std::logic_error("List already awaiting submission");
        if (generation == UINT64_MAX) throw std::overflow_error("Submission generation exhausted");
        const auto id = ++generation;
        pending.emplace(list, Entry{id, std::move(sink)});
        active.store(true, std::memory_order_release);
        return {list, id, this};
    }
    // Caller must first discard the recorded commands and prevent concurrent submit.
    bool Cancel(Ticket ticket) {
        if (ticket.owner != this) return false;
        std::shared_ptr<Sink> released;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = pending.find(ticket.list);
            if (it == pending.end() || it->second.generation != ticket.generation || it->second.submitted) return false;
            released = std::move(it->second.sink);
            pending.erase(it);
            active.store(!pending.empty(), std::memory_order_release);
        }
        released->Discarded(ticket.list);
        return true;
    }
    void NotifyReset(ID3D12CommandList* list, bool succeeded) noexcept {
        if (!succeeded || !list || !active.load(std::memory_order_acquire)) return;
        std::shared_ptr<Sink> sink;
        bool submitted=false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto it = pending.find(list);
            if (it == pending.end()) return;
            sink = std::move(it->second.sink);
            submitted=it->second.submitted;
            pending.erase(it);
            active.store(!pending.empty(), std::memory_order_release);
        }
        if(!submitted) sink->Discarded(list);
        else sink->RecordingEnded(list);
    }
    // Identity only; never dereference the destroyed object.
    void NotifyDestroyed(ID3D12CommandList* list) noexcept { NotifyReset(list,true); }
    void NotifySubmitted(ID3D12CommandQueue* queue, unsigned count,
                         ID3D12CommandList* const* lists) noexcept {
        if (!queue || !lists || !active.load(std::memory_order_acquire)) return;
        for (unsigned i = 0; i < count; ++i) {
            std::shared_ptr<Sink> sink;
            bool replay=false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto it = pending.find(lists[i]);
                if (it == pending.end()) continue;
                sink = it->second.sink;
                replay=it->second.submitted;
                it->second.submitted=true;
            }
            // Outside the lock: callbacks may register another frame.
            if(replay) sink->Replayed(queue,lists[i]);
            else sink->Submitted(queue, lists[i]);
        }
    }
};
inline std::shared_ptr<SubmissionObserver> SharedSubmissions() {
    static auto observer=std::make_shared<SubmissionObserver>();
    return observer;
}
inline SubmissionObserver& Submissions() {return *SharedSubmissions();}
}
