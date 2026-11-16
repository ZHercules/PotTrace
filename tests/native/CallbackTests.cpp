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
 return failures?1:0;
}
