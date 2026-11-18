#pragma once
#include <cstdint>
#include <optional>
#include <deque>
#include <cmath>
#include <algorithm>
namespace pottrace {
// Last successfully submitted clock, not the last queried clock. Rejected
// observations must not consume evidence of a lost position boundary.
class ClockContinuity {
 uint64_t anchor=0;bool pending=false;
public:
 bool observe(uint64_t identity,bool usable){pending=pending||!usable||(anchor&&anchor!=identity);return pending;}
 void invalidate(){pending=true;}
 bool invalid()const{return pending;}
 void submitted(uint64_t identity,bool accepted){if(accepted){anchor=identity;pending=false;}}
};
struct EffectiveRateResult {std::optional<double> value;bool changed=false;uint64_t from=0;bool discontinuity=false;};
class EffectiveRate {
 struct Point {uint64_t at;double wall,position,clock;};
 std::deque<Point> points;
 std::optional<double> value;
 std::optional<double> candidate;
 double valueError=0,candidateError=0;
 bool transitioning=false;
 uint64_t generation=0,clockId=0,lastFit=0,candidateAt=0,candidateFrom=0;
 struct Fit {double rate=0,error=0,rms=0;};
 static Fit fit(const std::deque<Point>& p,size_t first,size_t end){
  if(end-first<3)return {};
  double sx=0,sy=0,sxx=0,sxy=0;const auto& origin=p[first];
  for(auto i=first;i<end;++i){double x=p[i].wall-origin.wall,y=p[i].position-origin.position;sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;}
  double n=double(end-first),denom=n*sxx-sx*sx;if(denom<=0)return {};
  double rate=(n*sxy-sx*sy)/denom,residual=0;
  for(auto i=first;i<end;++i){double e=(p[i].position-origin.position-sy/n)-rate*(p[i].wall-origin.wall-sx/n);residual+=e*e;}
  double rms=std::sqrt(residual/n);
  return {rate,3*rms/std::sqrt(sxx-sx*sx/n),rms};
 }
public:
 void swap(EffectiveRate& other) noexcept {
  using std::swap;points.swap(other.points);swap(value,other.value);swap(candidate,other.candidate);swap(valueError,other.valueError);swap(candidateError,other.candidateError);swap(transitioning,other.transitioning);
  swap(generation,other.generation);swap(clockId,other.clockId);swap(lastFit,other.lastFit);swap(candidateAt,other.candidateAt);swap(candidateFrom,other.candidateFrom);
 }
 void reset(){points.clear();value.reset();candidate.reset();transitioning=false;generation=clockId=lastFit=candidateAt=candidateFrom=0;}
 EffectiveRateResult observe(uint64_t at,double wall,double position,double clock,uint64_t gen,uint64_t identity){
  if(!identity||!std::isfinite(wall)||!std::isfinite(position)||!std::isfinite(clock)){reset();return {std::nullopt,false,0,true};}
  bool broken=false;
  if(!points.empty()&&(gen!=generation||identity!=clockId||at<points.back().at||at-points.back().at>1500||wall<=points.back().wall||position<points.back().position||clock<points.back().clock)){
   broken=gen==generation;reset();
  }
  generation=gen;clockId=identity;
  if(!points.empty()){
   if(position==points.back().position){if(at-points.back().at>1000)reset();return {value,false,0};}
   // A clock alone cannot establish effective media speed; both increments
   // must agree. Native seek/generation fencing remains independently required.
   if(std::abs((position-points.back().position)-(clock-points.back().clock))>30){broken=true;reset();generation=gen;clockId=identity;}
  }
  points.push_back({at,wall,position,clock});
  while(points.size()>64||(points.size()>2&&wall-points[1].wall>=600))points.pop_front();
  if(points.size()<8||wall-points.front().wall<600)return {value,false,0,broken};
  const auto measured=fit(points,0,points.size());const double rate=measured.rate;const auto mid=points.size()/2;
  const auto left=fit(points,0,mid+1),right=fit(points,mid,points.size());
  const bool stable=rate>=.05&&rate<=16&&std::abs(left.rate-right.rate)<=std::max(.04,left.error+right.error)&&measured.rms<=std::max(15.,rate*25);
  if(!stable){candidate.reset();if(lastFit&&at-lastFit>1500)value.reset();return {value,false,0};}
  lastFit=at;
  if(transitioning){
   if(!candidate||std::abs(rate-*candidate)>std::max(.025,*candidate*.025)+measured.error+candidateError){candidate=rate;candidateError=measured.error;candidateAt=at;return {};}
   if(at-candidateAt<1000)return {};
   transitioning=false;value=rate;valueError=measured.error;candidate.reset();return {value,false,0};
  }
  if(!value){value=rate;valueError=measured.error;candidate.reset();return {value,false,0};}
  if(std::abs(rate-*value)<=std::max(.025,*value*.025)+measured.error+valueError){candidate.reset();return {value,false,0};}
  if(!candidate||std::abs(rate-*candidate)>std::max(.025,*candidate*.025)+measured.error+candidateError){
   candidate=rate;candidateError=measured.error;candidateAt=at;candidateFrom=points.front().at;return {value,false,0};
  }
  if(at-candidateAt<200)return {value,false,0};
  transitioning=true;value.reset();candidate=rate;candidateError=measured.error;candidateAt=at;return {std::nullopt,true,candidateFrom};
 }
};
}
