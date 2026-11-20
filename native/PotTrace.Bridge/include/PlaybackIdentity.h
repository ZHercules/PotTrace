#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>

namespace pottrace {
// A callback supplies an exact local path, but never a playback timestamp.
// Only observations starting after that confirmation may use its generation.
class PlaybackIdentity {
 std::mutex mutex;
 bool enabled=false;
 std::string path;
 uint64_t generation=0,confirmedAt=0,policyEpoch=0;
 int64_t first=-1,next=-1,total=-1,startDuration=-1;
public:
 struct Proof {uint64_t epoch,policy;int64_t first,next,total,startDuration;};
 void enable(bool value){
  std::lock_guard lock(mutex);if(enabled!=value)++policyEpoch;enabled=value;if(!value)path.clear();
 }
 void callback(std::string_view kind,std::string_view candidate,uint64_t at,int64_t duration=-1,int64_t position=-1){
  std::lock_guard lock(mutex);
  if(!enabled)return;
  if(kind=="Close"||kind=="Complete"){if(path==candidate)path.clear();return;}
  if(kind!="Open"&&kind!="Start"&&kind!="Time"&&kind!="Pause"&&kind!="Resume")return;
  if(kind=="Open"||path!=candidate){
   path.clear();first=next=total=startDuration=-1;
   if(candidate.size()<4||!((candidate[0]>='A'&&candidate[0]<='Z')||(candidate[0]>='a'&&candidate[0]<='z'))||candidate[1]!=':'||(candidate[2]!='\\'&&candidate[2]!='/'))return;
   path=candidate;confirmedAt=at;++generation;
  }
  if(kind=="Start"&&duration>0)startDuration=duration;
  if(kind=="Time"&&duration>0&&position>=0&&position<=duration){
   if(first<0){first=position;total=duration;}
   else if(next<0&&duration==total&&position>first)next=position;
  }
 }
 Proof observation(std::string_view source,uint64_t begin){
  std::lock_guard lock(mutex);
  bool matches=enabled&&!path.empty()&&begin>confirmedAt&&source==path;
  return {matches?generation:0,policyEpoch,matches?first:-1,matches?next:-1,matches?total:-1,matches?startDuration:-1};
 }
 uint64_t match(std::string_view source,uint64_t begin){
  std::lock_guard lock(mutex);
  return enabled&&!path.empty()&&begin>confirmedAt&&source==path?generation:0;
 }
 uint64_t current(uint64_t begin){
  std::lock_guard lock(mutex);return enabled&&!path.empty()&&begin>confirmedAt?generation:0;
 }
};
}
