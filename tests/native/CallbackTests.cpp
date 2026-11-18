// Exercise the production callback state machine with only the host query boundary replaced.
#include "../../native/PotTrace.Bridge/src/Bridge.cpp"
#include <iostream>
#include <stdexcept>
namespace {
long long hostState=1,hostPosition=0,hostDuration=120000;
bool hostAvailable=true;
bool query(HWND,WPARAM key,long long& value){
 if(!hostAvailable)return false;
 switch(key){case 0x5006:value=hostState;break;case 0x5004:value=hostPosition;break;
 case 0x5002:value=hostDuration;break;default:return false;}return true;
}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void deliver(State& s,const char* kind){callback(s,{std::string(kind)+"\t120000\t0\tC:\\movie.avi",GetTickCount64(),utc()});}
}
int main(){int failures=0;
 auto test=[&](const char* name,auto body){
  hostAvailable=true;hostState=1;hostPosition=0;hostDuration=120000;
  try{body();std::cout<<"PASS "<<name<<'\n';}
  catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
 };
 test("Pause arrives before delayed Start",[]{State s;deliver(s,"Open");deliver(s,"Pause");deliver(s,"Start");
  check(s.active&&s.playState=="Paused","Start must observe paused state, never invent Playing");
  for(auto& event:s.outgoing)check(event.json.find("\"state\":\"Playing\"")==std::string::npos,"no fictitious playing event");});
 test("Start after playback has stopped",[]{State s;hostState=-1;deliver(s,"Open");deliver(s,"Start");
  check(!s.active,"stale Start must not activate a stopped movie");});
 test("Start query timeout",[]{State s;hostAvailable=false;deliver(s,"Open");deliver(s,"Start");
  check(!s.active&&s.playState=="Unknown","missing observation must not create playing time");
  hostAvailable=true;sample(s,GetTickCount64());check(s.active&&s.playState=="Paused","later successful observation can activate");});
 test("New Open clears old duration",[]{State s;s.duration=999999;deliver(s,"Open");
  check(s.duration==0,"old movie duration must not leak into new Open");});
 test("Playing Start uses actual position",[]{State s;hostState=2;hostPosition=5432;deliver(s,"Start");
  check(s.active&&s.playState=="Playing"&&s.pos==5432,"observed playing state and position are preserved");});
 test("Graph mode ignores delayed script and window samples",[]{State s;s.graphMode=true;deliver(s,"Open");deliver(s,"Start");sample(s,100);
  check(s.outgoing.empty()&&s.path.empty(),"legacy observations mutated graph mode");});
 test("Graph serialization preserves source capture and unknown position",[]{State s;s.graphMode=true;
  GraphFact f;f.binding=1;f.qualified=true;f.at=f.begin=100;f.path="C:/a.mp4";f.state=GraphState::Paused;
  graphCallback(s,f,"2026-11-17T00:00:00.100Z");check(s.outgoing.size()==1,"graph open absent");
  auto j=s.outgoing.front().json;check(j.find("\"monotonicMs\":100")!=std::string::npos&&j.find("\"positionMs\":null")!=std::string::npos&&j.find("\"quality\":\"Observed\"")!=std::string::npos,"graph payload fabricated position/time/quality");});
 test("Deferred pause preserves UTC as well as monotonic capture",[]{State s;s.graphMode=true;
  GraphFact f;f.binding=1;f.qualified=true;f.at=f.begin=100;f.path="C:/a.mp4";f.state=GraphState::Playing;
  graphCallback(s,f,"2026-11-17T00:00:00.100Z");
  f.kind=GraphFactKind::Sample;f.at=f.begin=200;f.position=100;f.duration=30000;graphCallback(s,f,"2026-11-17T00:00:00.200Z");
  f.kind=GraphFactKind::Ready;f.at=f.begin=210;f.state=GraphState::Paused;graphCallback(s,f,"2026-11-17T00:00:00.210Z");
  f.kind=GraphFactKind::Sample;f.at=f.begin=230;f.position=110;graphCallback(s,f,"2026-11-17T00:00:00.230Z");
  bool found=false;for(auto& e:s.outgoing)if(e.json.find("\"kind\":\"Paused\"")!=std::string::npos){found=true;check(e.json.find("2026-11-17T00:00:00.210Z")!=std::string::npos,"pause UTC used late position timestamp");}
  check(found,"deferred pause absent");});
 test("Estimated rate transition preserves unknown value and window",[]{State s;s.graphMode=true;
  GraphTransition e;e.epoch=1;e.path="C:/a.mp4";e.at=1000;e.kind="RateChanged";e.state="Playing";e.estimatedRate=true;e.rateKnown=false;e.rateWindowFrom=200;
  graphTransitions(s,{e},"2026-11-17T00:00:01.000Z",1000);auto j=s.outgoing.front().json;
  check(j.find("\"rate\":null")!=std::string::npos&&j.find("\"rateIsEstimated\":true")!=std::string::npos&&j.find("\"rateWindowStartMs\":200")!=std::string::npos,"unknown rate converted to 1 or window dropped");
 });
 test("Graph disable preserves workers for later re-enable",[]{State s;s.graphMode=true;auto previous=state;state=&s;PotTrace_Shutdown();state=previous;
  check(!s.stop,"script finalization killed graph workers permanently");});
 test("Terminal close survives a full sample queue",[]{State s;s.graphMode=true;auto previous=state;state=&s;
  GraphFact open;open.binding=1;open.qualified=true;open.at=open.begin=100;open.path="C:/movie.mp4";open.state=GraphState::Paused;
  graphCallback(s,open,"2026-11-18T00:00:00.100Z");
  GraphRaw queued;while(s.graphRaw.push(queued)){}
  const auto result=PotTrace_RetireGraph(1,200);drainGraphRetirements(s);state=previous;
  check(result==0,"terminal close was rejected by sample backpressure");
  check(s.outgoing.back().json.find("MediaClosed")!=std::string::npos,"close not delivered");});
 return failures?1:0;
}
