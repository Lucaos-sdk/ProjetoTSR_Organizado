#include "frame_slots.h"
#include <iostream>
static void Require(bool ok){if(!ok)throw std::runtime_error("Frame slot invariant failed");}
template<class F> void Reject(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}Require(rejected);}
int main(){
    tsr::FrameSlots s(3);auto a=*s.Acquire(0);Require(!a.history);s.Commit(a,1,false);
    auto b=*s.Acquire(0);Require(b.history==a.slot);s.Commit(b,2,true);
    auto c=*s.Acquire(0);s.Commit(c,3,true);
    Require(!s.Acquire(0));Require(!s.Acquire(1)); // A's producer finished but consumer B did not.
    auto d=*s.Acquire(2);Require(d.slot==a.slot && d.history==c.slot);
    Reject([&]{s.Commit(a,4,true);});Reject([&]{s.Commit(d,3,true);});
    Require(!s.CanReconfigure(3));s.Cancel(d);Require(s.CanReconfigure(3));
    auto e=*s.Acquire(3);Require(e.slot!=c.slot);s.Commit(e,4,false);
    Reject([&]{s.Acquire(UINT64_MAX);});Reject([&]{s.Acquire(2);});Reject([&]{s.Acquire(5);});
    tsr::FrameSlots other(2);auto f=*other.Acquire(0);Reject([&]{other.Commit(a,1,false);});other.Cancel(f);
    auto g=*other.Acquire(0);Reject([&]{other.Commit(f,1,false);});
    Reject([&]{other.Commit(g,1,true);});other.Commit(g,1,false);
    Require(s.LastSubmitted()==4 && other.LastSubmitted()==1);
    tsr::FrameSlots live(3,true);
    auto p=*live.Acquire(0);live.Commit(p,1,false);
    auto q=*live.Acquire(1);live.Commit(q,2,true);
    auto r=*live.Acquire(2);live.Commit(r,3,true);
    Require(!live.Acquire(3) && !live.CanReconfigure(3));
    live.EndRecording(p);Require(!live.Acquire(3)); // Consumer Q still pins P.
    live.Replay(q,4);Require(live.LastSubmitted()==4);
    live.EndRecording(q);Require(!live.Acquire(3)); // Pins gone, but replay still in flight.
    auto free=*live.Acquire(4);Require(free.slot==p.slot && free.history==r.slot);live.Cancel(free);
    Reject([&]{live.Replay(q,5);});Reject([&]{live.EndRecording(q);});
    live.EndRecording(r);Require(live.CanReconfigure(4));
    std::cout<<"PASS consumer lifetime, busy slots, cancel, stale leases, reset and context isolation\n";
}
