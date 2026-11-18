#include <iostream>
#include <stdexcept>
#if __has_include("CollectorRuntime.h")
#include "CollectorRuntime.h"
#include <future>
#include <chrono>
#include "GraphTimeline.h"
using namespace std::chrono_literals;
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
std::atomic<unsigned> admissions{0};std::atomic<uint64_t> nextBinding{1};
pottrace::GraphTimeline timeline;std::mutex timelineGate;std::atomic<uint64_t> closedAt{0};
bool retireBinding(uint64_t binding,uint64_t at) noexcept {std::lock_guard lock(timelineGate);pottrace::GraphFact f;f.kind=pottrace::GraphFactKind::Closed;f.binding=binding;f.begin=f.at=at;timeline.apply(f);closedAt=at;return true;}
struct Work : pottrace::CollectorWork {
 std::atomic<int> samples{0};std::atomic<bool> entered{false};std::shared_future<void> blocked;
 void Admitted() noexcept override {++admissions;control->binding=nextBinding++;control->onClose=retireBinding;}
 void Observe(bool metadata) override {if(!metadata&&blocked.valid()){entered=true;blocked.wait();}++samples;}
};
int main(){try{
 pottrace::CollectorRuntime runtime;std::promise<void> release;
 auto first=std::make_shared<Work>();first->blocked=release.get_future().share();
 auto a=runtime.Attach(first);check(bool(a),"first attach failed");
 auto second=std::make_shared<Work>();auto b=runtime.Attach(second);check(bool(b),"second attach failed");
 check(admissions==2,"binding IDs must be allocated on admission, not construction");
 auto third=runtime.Attach(std::make_shared<Work>());check(!third,"ExhaustedSlotsDoNotSpawnWorkers");
 check(admissions==2&&nextBinding==3,"rejected attachment consumed a binding ID");
 for(int i=0;i<200&&!first->entered;++i)Sleep(5);check(first->entered,"blocking query did not start");
 {std::lock_guard lock(timelineGate);pottrace::GraphFact f;f.binding=a->binding;f.qualified=true;f.begin=f.at=GetTickCount64();f.path="C:/old.mp4";f.state=pottrace::GraphState::Paused;check(!timeline.apply(f).empty(),"old source was not active");}
 auto ticket=a->fence.capture();auto start=GetTickCount64();runtime.Retire(a);
 check(GetTickCount64()-start<100,"BlockedQueryDoesNotBlockRetire");
 check(closedAt>=start,"retirement must reach bridge while readiness query is still blocked");
 check(!a->fence.publish(ticket,[]{throw std::runtime_error("stale publish");}),"RetiredGenerationCannotPublish");
 check(runtime.Snapshot().workerCount==4,"unbounded worker creation");
 runtime.Retire(b);second.reset();
 for(int i=0;i<200&&runtime.Snapshot().occupiedSlots>1;++i)Sleep(5);
 auto replacement=runtime.Attach(std::make_shared<Work>());check(bool(replacement)&&replacement->binding==3,"healthy replacement unavailable while old query blocked");
 {std::lock_guard lock(timelineGate);pottrace::GraphFact f;f.binding=replacement->binding;f.qualified=true;f.begin=f.at=GetTickCount64();f.path="C:/replacement.mp4";f.state=pottrace::GraphState::Paused;auto events=timeline.apply(f);check(!events.empty()&&events.back().kind=="MediaOpened","old binding suppressed replacement");}
 runtime.Retire(replacement);release.set_value();first.reset();
 for(int i=0;i<200&&runtime.Snapshot().occupiedSlots;++i)Sleep(5);
 check(runtime.Snapshot().occupiedSlots==0,"graph references not reclaimed");
 auto fresh=runtime.Attach(std::make_shared<Work>());check(bool(fresh),"retired slots cannot be reused");runtime.Retire(fresh);
 check(runtime.StopForTests(2000),"idle shutdown did not complete");
 std::cout<<"Collector bounded lifecycle tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){std::cerr<<"Bounded collector runtime is not implemented\n";return 1;}
#endif
