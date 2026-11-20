#pragma once
#include <string>
#include <vector>
#include <optional>
#include <set>
#include <map>
#include <cstdint>
#include <cmath>
#include <algorithm>
namespace pottrace {
enum class GraphState { Unknown=-1, Stopped=0, Paused=1, Playing=2 };
enum class GraphFactKind { Ready, Sample, Segment, Closed, Gap, Withdrawn };
struct GraphFact {
 GraphFactKind kind=GraphFactKind::Ready;
 uint64_t identityEpoch=0;
 bool confirmationRequired=false,foregroundStart=false;
 uint64_t policyEpoch=0;
 int64_t witnessFirst=-1,witnessNext=-1,witnessDuration=-1,witnessStartDuration=-1;
 uint64_t binding=0,generation=0,begin=0,at=0;
 std::string path;
 GraphState state=GraphState::Unknown;
 bool qualified=false;
 std::optional<int64_t> position,duration;
 double rate=1;
 bool estimatedRate=false,rateKnown=true,rateChange=false;uint64_t rateWindowFrom=0;
 bool positionDiscontinuity=false;
};
struct GraphTransition {
 uint64_t epoch=0,at=0;std::string path,kind,state;
 std::optional<int64_t> position,duration;double rate=1;bool gap=false;
 bool estimatedRate=false,rateKnown=true;uint64_t rateWindowFrom=0;
};
// Single consumer. Facts have already passed connection/lifecycle proof; this
// reducer never obtains identity from a title, command line or delayed script.
class GraphTimeline {
 std::map<uint64_t,uint64_t> live;
 std::set<uint64_t> retired;
 std::map<uint64_t,GraphFact> streamOrigins;
 uint64_t binding=0,epoch=0,lastFact=0,lastHealth=0,lastState=0,generation=0,publishedAt=0,retiredThrough=0;
 bool active=false,exhausted=false,healthKnown=false;
 GraphTransition current;
 std::optional<GraphFact> pendingPause;
 bool stale(uint64_t at)const{return !healthKnown||(at>lastHealth&&at-lastHealth>1500);}
 static std::string name(GraphState s){return s==GraphState::Playing?"Playing":s==GraphState::Paused?"Paused":"Stopped";}
 GraphTransition emit(std::string kind,uint64_t at,bool gap=false){if(kind!="RateChanged")current.rateWindowFrom=0;current.kind=std::move(kind);current.at=at;current.gap=gap;publishedAt=std::max(publishedAt,at);return current;}
 void finishPause(std::vector<GraphTransition>& out,std::optional<int64_t> position=std::nullopt){
  if(!pendingPause)return;
  current.position=position;current.state="Paused";
  out.push_back(emit("Paused",pendingPause->at));lastFact=pendingPause->at;pendingPause.reset();
 }
public:
 std::vector<GraphTransition> interrupt(uint64_t at){
  streamOrigins.clear();
  if(!active)return {};
  std::vector<GraphTransition> out;finishPause(out);
  current.position.reset();lastFact=lastState=std::max(at,lastFact);
  out.push_back(emit("Gap",lastFact,true));return out;
 }
 std::vector<GraphTransition> apply(const GraphFact& f){
  std::vector<GraphTransition> out;
  if(!f.binding||exhausted)return out;
  if(f.kind==GraphFactKind::Closed){
   live.erase(f.binding);streamOrigins.erase(f.binding);
   if(f.binding>retiredThrough)retired.insert(f.binding);
   while(retired.erase(retiredThrough+1))++retiredThrough;
   if(retired.size()>4096)exhausted=true; // unresolved holes, not normal opens
   if(active&&binding==f.binding){finishPause(out);out.push_back(emit("MediaClosed",std::max(f.at,lastFact),stale(f.at)));active=false;}
   return out;
  }
  if(f.binding<=retiredThrough||retired.count(f.binding)||!f.qualified||f.at<f.begin||f.at<publishedAt)return out;
  if(f.kind==GraphFactKind::Withdrawn){
   live.erase(f.binding);streamOrigins.erase(f.binding);
   if(active&&binding==f.binding){finishPause(out);out.push_back(emit("MediaClosed",f.at,stale(f.at)));active=false;}
   return out;
  }
  if(f.identityEpoch&&f.kind!=GraphFactKind::Ready&&
     !(f.kind==GraphFactKind::Segment&&!live.count(f.binding))&&
     (!live.count(f.binding)||live.at(f.binding)!=f.identityEpoch))return out;
  if(f.kind==GraphFactKind::Segment||f.kind==GraphFactKind::Gap){
   if(active&&binding==f.binding&&(f.at<lastHealth||f.generation<generation))return out;
   if(f.kind==GraphFactKind::Gap)streamOrigins.erase(f.binding);
   if(f.kind==GraphFactKind::Segment&&f.position){
    if(streamOrigins.size()>=4096&&!streamOrigins.count(f.binding)){exhausted=true;return out;}
    streamOrigins[f.binding]=f;
   }
   if(active&&binding==f.binding&&f.at>=lastFact){
    finishPause(out);
    generation=std::max(generation,f.generation);lastState=lastFact=f.at;
    current.position.reset();out.push_back(emit(f.kind==GraphFactKind::Segment?"Seeked":"Gap",f.at,f.kind==GraphFactKind::Gap));
   }return out;
  }
  if(f.kind==GraphFactKind::Ready){
   if(f.state==GraphState::Unknown||f.path.empty())return out;
   if(active&&binding==f.binding&&f.at<lastFact)return out;
   std::optional<int64_t> streamOrigin;
   auto origin=streamOrigins.find(f.binding);
   if(origin!=streamOrigins.end()&&f.at>=origin->second.at&&f.generation>=origin->second.generation){
    // A native stream origin does not identify media or open a session. Only
    // the first subsequent qualified source/readiness observation can use it.
    if(origin->second.identityEpoch==f.identityEpoch)streamOrigin=origin->second.position;
    streamOrigins.erase(origin);
   }
   if(f.state==GraphState::Stopped){
    if(active&&binding==f.binding){finishPause(out);out.push_back(emit("MediaClosed",f.at,stale(f.at)));active=false;}
    live.erase(f.binding);return out;
   }
   auto previous=live.find(f.binding);
   if(active&&binding==f.binding&&previous!=live.end()&&previous->second!=f.identityEpoch){
    finishPause(out);out.push_back(emit("MediaClosed",f.at,stale(f.at)));active=false;
   }
   live[f.binding]=f.identityEpoch;
   if(live.size()>1){
    if(active){finishPause(out);out.push_back(emit("MediaClosed",std::max(lastFact,f.at),true));active=false;}
    return out;
   }
   if(active&&(binding!=f.binding||current.path!=f.path)){
    finishPause(out);out.push_back(emit("MediaClosed",f.at,stale(f.at)));active=false;
   }
   if(!active){
    binding=f.binding;generation=f.generation;active=true;lastFact=lastState=lastHealth=f.at;healthKnown=true;
    current={++epoch,f.at,f.path,"MediaOpened",name(f.state),streamOrigin,std::nullopt,1,false};
    current.estimatedRate=f.estimatedRate;current.rateKnown=!f.estimatedRate&&f.rateKnown;
    publishedAt=f.at;out.push_back(current);return out;
   }
   const bool gap=healthKnown&&f.at-lastHealth>1500;
   lastHealth=f.at;generation=std::max(generation,f.generation);
   const auto desired=name(f.state);
   if(pendingPause){
    if(desired=="Paused"&&f.generation==pendingPause->generation&&f.at-pendingPause->at<=250)return out;
    finishPause(out); // no final position before timeout/resume: keep it unknown
   }
   if(desired=="Paused"&&current.state=="Playing"&&!gap){
    pendingPause=f;lastState=lastFact=f.at;return out;
   }
   if(desired!=current.state&&!(desired=="Playing"&&current.state=="Buffering")){
    current.state=desired;lastState=f.at;
    out.push_back(emit(gap?"Gap":desired=="Paused"?"Paused":"Resumed",f.at,gap));lastFact=f.at;
   }else if(gap||(desired=="Paused"&&f.at-current.at>=250)){
    out.push_back(emit(gap?"Gap":"Snapshot",f.at,gap));lastFact=f.at;
   }else if(desired=="Playing"&&current.state=="Playing"&&f.at-current.at>=1000){
    current.state="Buffering";out.push_back(emit("BufferingStarted",f.at,true));lastFact=f.at;
   }
   return out;
  }
  // A slow position query cannot introduce a source or cross a state/seek
  // boundary, even if it returns a newer timestamp after that boundary.
  if(!active||binding!=f.binding||current.path!=f.path||f.begin<lastState||f.at<lastFact||f.generation<generation||!f.position||!f.duration||!std::isfinite(f.rate)||f.rate<=0||f.at-f.begin>50)return out;
  auto consumed=streamOrigins.find(f.binding);
  if(consumed!=streamOrigins.end()&&f.at>=consumed->second.at&&f.generation>=consumed->second.generation)streamOrigins.erase(consumed);
  if(pendingPause&&(f.positionDiscontinuity||f.state!=GraphState::Paused||f.generation!=pendingPause->generation||f.at-pendingPause->at>250||stale(f.at)))finishPause(out);
  if(pendingPause&&f.state==GraphState::Paused){
   current.duration=f.duration;current.rate=f.rate;current.rateKnown=f.rateKnown;current.estimatedRate=f.estimatedRate;finishPause(out,f.position);
   generation=f.generation;lastFact=f.at;
   out.push_back(emit("Snapshot",f.at));return out;
  }
  auto desired=name(f.state);if(desired!=current.state&&!(desired=="Playing"&&current.state=="Buffering"))return out;
  if(!f.rateChange&&!f.positionDiscontinuity&&desired=="Playing"&&current.position==f.position&&current.rate==f.rate&&current.rateKnown==f.rateKnown)return out;
  if(!f.rateChange&&!f.positionDiscontinuity&&desired=="Paused"&&current.position==f.position&&current.duration==f.duration&&current.rate==f.rate&&current.rateKnown==f.rateKnown)return out;
  auto kind=std::string("Snapshot");bool gap=f.at-current.at>1500;
  if(gap)kind="Gap";
  else if(f.positionDiscontinuity)kind="PositionDiscontinuity";
  else if(f.estimatedRate?f.rateChange:current.rate!=f.rate)kind="RateChanged";
  else if(current.state=="Buffering")kind="BufferingEnded";
  else if(current.position&&(*f.position<*current.position||std::abs(double(*f.position-*current.position)-(f.at-current.at)*f.rate)>std::max(500.,(f.at-current.at)*f.rate*.2))){kind="Seeked";gap=true;}
  current.position=f.position;current.duration=f.duration;current.rate=f.rate;current.state=desired;
  current.rateKnown=f.rateKnown;current.estimatedRate=f.estimatedRate;current.rateWindowFrom=f.rateWindowFrom;
  generation=f.generation;lastFact=f.at;out.push_back(emit(kind,f.at,gap));return out;
 }
};
}
