#include "EffectiveRate.h"
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <fstream>
#include <sstream>
using namespace pottrace;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
int main(int argc,char** argv){try {
 if(argc>1){
  std::ifstream file(argv[1]);check(bool(file),"recorded fixture missing");EffectiveRate recorded;std::string line;int changes=0;
  while(std::getline(file,line)){if(line.empty()||line[0]=='#')continue;std::istringstream in(line);uint64_t at,gen;double wall,p,c;in>>at>>wall>>p>>c>>gen;check(bool(in),"bad recorded sample");auto r=recorded.observe(at,wall,p,c,gen,1);
   if(r.changed){++changes;std::cout<<"recorded transition "<<r.from<<".."<<at<<'\n';check(!r.value,"transition claimed a precise speed");}
   if(at>11000&&at<11500)check(r.value&&*r.value>1.8&&*r.value<2.2,"settled 2x missing");
   if(at>18000)check(r.value&&*r.value>.9&&*r.value<1.1,"settled normal speed missing");
  }
  check(changes==2,"recorded 1->2->1 boundaries missing or fragmented");return 0;
 }
 EffectiveRate rate;int changes=0;double position=0;
 for(uint64_t at=0;at<=6000;at+=20){
  if(at)position+=at<=2000?20:at<=4000?40:20;
  auto result=rate.observe(at,double(at),position,position,1,1);
  if(result.changed){++changes;check(result.from<= (changes==1?2000:4000)&&at>=(changes==1?2000:4000),"change window excludes real boundary");}
  if(at==1900)check(result.value&&std::abs(*result.value-1)<.02,"normal rate missing");
  if(at==3900)check(result.value&&std::abs(*result.value-2)<.02,"2x remains falsely 1");
 }
 check(changes==2,"1->2->1 did not produce exactly two changes");
 rate.reset();changes=0;
 for(uint64_t at=0;at<=20000;at+=25){double p=double(at)+(at%100==0?5:-5);auto result=rate.observe(at,double(at),p,p,1,1);changes+=result.changed;}
 check(changes==0,"steady clock jitter fragmented playback");
 check(!rate.observe(20025,20025,20025,20025,2,1).value,"seek generation retained old rate");
 for(uint64_t at=20050;at<=21000;at+=25)rate.observe(at,double(at),double(at),double(at),2,1);
 check(!rate.observe(21025,21025,21025,21025,2,2).value,"new clock retained rate evidence");
 check(!rate.observe(25000,25000,25000,25000,2,2).value,"outage retained rate evidence");
 rate.reset();check(!rate.observe(0,0,0,0,1,1).value,"first point invented rate");
 for(uint64_t at=25;at<=2000;at+=25){auto r=rate.observe(at,double(at),0,double(at),1,1);check(!r.value,"clock alone while position frozen invented playback rate");}
 rate.reset();rate.observe(0,0,0,0,1,1);
 check(rate.observe(25,25,225,25,1,1).discontinuity,"clock mismatch only cleared rate and left false continuity");
 check(rate.observe(50,50,250,50,1,2).discontinuity,"clock replacement did not invalidate continuity");
 ClockContinuity clock;clock.submitted(1,true);
 check(clock.observe(2,true),"new clock did not invalidate anchor");clock.submitted(2,false);
 check(clock.observe(1,true),"rejected clock change was forgotten when original clock returned");
 clock.submitted(1,true);check(!clock.observe(1,true),"accepted boundary not cleared");
 clock.observe(0,false);clock.submitted(0,false);check(clock.observe(1,true),"failed clock query lost invalidation");
 std::cout<<"Effective rate changes, jitter, lifecycle and frozen-position checks passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
