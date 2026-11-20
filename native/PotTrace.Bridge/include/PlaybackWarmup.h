#pragma once
#include "GraphTimeline.h"

namespace pottrace {
// A Time callback names a file and a media position, but its delivery time
// is unrelated to presentation. Keep measured graph clocks until two named
// positions corroborate one uninterrupted traversal of that same file.
class PlaybackWarmup {
 std::vector<GraphFact> pending;
 uint64_t epoch=0,matchedAt=0,lastReady=0;
 uint64_t readyGeneration=0,readyEpoch=0;GraphState readyState=GraphState::Unknown;

 void forwardConfirmed(std::vector<GraphFact>& out){
  for(auto& fact:pending)if(fact.identityEpoch)out.push_back(std::move(fact));
  reset();
 }
 static void withdraw(const GraphFact& f,std::vector<GraphFact>& out){
  if(f.kind!=GraphFactKind::Ready)return;
  auto ended=f;ended.kind=GraphFactKind::Withdrawn;ended.path.clear();out.push_back(std::move(ended));
 }
 bool recover(const GraphFact& confirmation,std::vector<GraphFact>& out){
  if(confirmation.witnessFirst<0||confirmation.witnessNext-confirmation.witnessFirst<250)return false;
  size_t first=pending.size();int64_t nearest=66;bool second=false;const GraphFact* previous=nullptr;
  for(size_t i=0;i<pending.size();++i){
   const auto& f=pending[i];
   if(f.kind!=GraphFactKind::Sample)continue;
   if(!f.position||!f.duration||std::abs(*f.duration-confirmation.witnessDuration)>50)return false;
   const auto position=*f.position;
   if(previous&&(f.at<previous->at||f.at-previous->at>150||f.positionDiscontinuity||
                 (f.state==GraphState::Playing&&position<*previous->position)||
                 std::abs(position-*previous->position)>(f.state==GraphState::Paused?100:500)))return false;
   previous=&f;
   if(f.state!=GraphState::Playing)continue;
   auto distance=std::abs(position-confirmation.witnessFirst);
   if(distance<nearest){nearest=distance;first=i;}
   if(first!=pending.size()&&position>=confirmation.witnessNext&&position-confirmation.witnessNext<=65)second=true;
  }
  if(!second)return false;
  // Start names this traversal. Extend its first Time anchor only to an
  // actual near-zero sample also observed running by the main player.
  if(confirmation.witnessStartDuration==confirmation.witnessDuration){
   for(size_t i=0;i<first;++i){
    const auto& point=pending[i];
    if(point.foregroundStart&&point.position&&*point.position<=100&&
       pending[first].at-point.at<=250){first=i;break;}
   }
  }
  auto open=pending[first];open.kind=GraphFactKind::Ready;open.identityEpoch=epoch;
  out.push_back(std::move(open));
  for(size_t j=first;j<pending.size();++j){pending[j].identityEpoch=epoch;out.push_back(std::move(pending[j]));}
  reset();return true;
 }
public:
 void reset(){pending.clear();epoch=matchedAt=lastReady=0;}
 std::vector<GraphFact> accept(const GraphFact& f){
  std::vector<GraphFact> out;
  bool eligible=f.confirmationRequired&&(f.state==GraphState::Playing||(f.state==GraphState::Paused&&!pending.empty()))&&f.path.size()<=2048&&
    (f.kind==GraphFactKind::Ready||f.kind==GraphFactKind::Sample);
  if(!pending.empty()){
   const auto& start=pending.front();
   const auto& last=pending.back();
   const bool pausing=last.state==GraphState::Playing&&f.state==GraphState::Paused;
   if(!eligible||start.binding!=f.binding||start.path!=f.path||
      (last.generation!=f.generation&&!pausing)||(last.state==GraphState::Paused&&f.state==GraphState::Playing)||
      start.policyEpoch!=f.policyEpoch||f.at<start.at||f.at-start.at>20000||pending.size()>=2048||
      (matchedAt&&f.at>matchedAt+1800)||
      (epoch&&f.identityEpoch!=epoch)){
    if(start.policyEpoch!=f.policyEpoch&&f.confirmationRequired)reset();else forwardConfirmed(out);
   }
  }
  if(!eligible||(pending.empty()&&(f.identityEpoch||f.state==GraphState::Paused))){
   if(!f.confirmationRequired||f.identityEpoch)out.push_back(f);else withdraw(f,out);
   return out;
  }
  if(!f.identityEpoch)withdraw(f,out);
  if(f.kind==GraphFactKind::Sample||!pending.empty()){
   // Concurrent state and metadata workers can finish in either order.
   if(pending.empty()||f.at>=pending.back().at){
    bool save=f.kind!=GraphFactKind::Ready||pending.empty()||lastReady==0||f.at-lastReady>=100||
     readyState!=f.state||readyGeneration!=f.generation||readyEpoch!=f.identityEpoch;
    if(save){
     if(f.kind==GraphFactKind::Ready){lastReady=f.at;readyState=f.state;readyGeneration=f.generation;readyEpoch=f.identityEpoch;}
     pending.push_back(f);
    }
   }
   if(f.identityEpoch){if(!epoch)matchedAt=f.at;epoch=f.identityEpoch;recover(f,out);}
  }
  return out;
 }
};
}
