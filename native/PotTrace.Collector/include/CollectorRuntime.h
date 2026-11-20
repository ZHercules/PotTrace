#pragma once
#include <windows.h>
#include <objbase.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include "ObservationFence.h"

namespace pottrace {
struct GraphControl {
 std::atomic<bool> windowVerified{false};
 std::atomic<bool> retired{false};std::atomic<uint64_t> binding{0};ObservationFence fence;
 bool (*onClose)(uint64_t,uint64_t) noexcept=nullptr;
 void Close(uint64_t at) noexcept {auto id=binding.exchange(0);if(id&&onClose)onClose(id,at);}
};
using GraphLease=std::shared_ptr<GraphControl>;
struct CollectorWork {
 GraphLease control=std::make_shared<GraphControl>();
 virtual void Admitted() noexcept {}
 virtual void Observe(bool metadata)=0;
 virtual DWORD MetadataInterval() const noexcept{return 25;}
 virtual void Finished(bool) noexcept {}
 virtual ~CollectorWork()=default;
};
struct CollectorHealth {unsigned workerCount=0,occupiedSlots=0;uint64_t failures=0;};
// Four process-wide query slots, two per graph. COM calls and final releases
// execute only on these workers. Retire only flips a fence; it never joins.
class CollectorRuntime {
 struct Slot {std::shared_ptr<CollectorWork> pending;std::atomic<bool> occupied{false};};
 struct State {std::mutex gate;std::array<Slot,4> slots;std::array<std::weak_ptr<GraphControl>,2> controls;std::atomic<bool> stop{false};std::atomic<uint64_t> failures{0};};
 struct Parameter {std::shared_ptr<State> state;unsigned index;};
 std::shared_ptr<State> state=std::make_shared<State>();std::array<HANDLE,4> threads{};
 static DWORD WINAPI Run(void* raw){
  std::unique_ptr<Parameter> parameter(static_cast<Parameter*>(raw));auto s=parameter->state;const auto index=parameter->index;parameter.reset();
  auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  while(!s->stop){
   std::shared_ptr<CollectorWork> work;
   {std::lock_guard lock(s->gate);work=std::move(s->slots[index].pending);}
   if(!work){Sleep(2);continue;}
   if(FAILED(initialized)){work->control->retired=true;++s->failures;}
   while(!s->stop&&!work->control->retired){
    try{work->Observe(index%2!=0);}catch(...){++s->failures;work->control->fence.retire();work->control->retired=true;}
    Sleep(index%2?work->MetadataInterval():10);
   }
   work->Finished(index%2!=0);
   // A blocking Release retains this slot; no replacement thread is created.
   work.reset();s->slots[index].occupied=false;
  }
  if(SUCCEEDED(initialized))CoUninitialize();return 0;
 }
public:
 CollectorRuntime(){
  for(unsigned i=0;i<threads.size();++i){auto p=std::make_unique<Parameter>(Parameter{state,i});threads[i]=CreateThread(nullptr,0,Run,p.get(),0,nullptr);if(threads[i])p.release();else state->stop=true;}
 }
 ~CollectorRuntime(){state->stop=true;for(auto h:threads)if(h)CloseHandle(h);}
 CollectorRuntime(const CollectorRuntime&)=delete;
 GraphLease Attach(std::shared_ptr<CollectorWork> work){
  if(!work||state->stop)return {};std::lock_guard lock(state->gate);
  for(unsigned i=0;i<4;i+=2)if(!state->slots[i].occupied&&!state->slots[i+1].occupied){
   work->Admitted();
   state->controls[i/2]=work->control;
   for(unsigned j=i;j<i+2;++j){state->slots[j].occupied=true;state->slots[j].pending=work;}return work->control;
  }return {};
 }
 void Retire(const GraphLease& lease) noexcept{if(lease){const auto at=GetTickCount64();lease->fence.retire();lease->retired=true;lease->Close(at);}}
 void SuspendCapture(uint64_t at) noexcept{
  std::array<GraphLease,2> controls;{std::lock_guard lock(state->gate);for(unsigned i=0;i<2;++i)controls[i]=state->controls[i].lock();}
  for(auto& control:controls)if(control){auto change=control->fence.changing();control->Close(at);}
 }
 CollectorHealth Snapshot() const noexcept{CollectorHealth h;for(unsigned i=0;i<4;++i){if(threads[i])++h.workerCount;if(state->slots[i].occupied)++h.occupiedSlots;}h.failures=state->failures;return h;}
 bool StopForTests(DWORD milliseconds){state->stop=true;auto deadline=GetTickCount64()+milliseconds;for(auto h:threads){auto now=GetTickCount64();if(h&&WaitForSingleObject(h,static_cast<DWORD>(now<deadline?deadline-now:0))!=WAIT_OBJECT_0)return false;}return true;}
};
}
