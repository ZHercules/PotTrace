#include "GraphTimeline.h"
#include "TerminalDelivery.h"
#include <iostream>
#include <stdexcept>
using namespace pottrace;
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
GraphFact ready(uint64_t binding,std::string path,uint64_t at,GraphState state=GraphState::Paused){
 GraphFact f;f.binding=binding;f.path=std::move(path);f.at=f.begin=at;f.state=state;f.qualified=true;return f;
}
int main(){try{
 GraphTimeline t;
 auto f=ready(1,"C:/a/movie.mp4",100);f.qualified=false;
 check(t.apply(f).empty(),"unqualified renderer admitted");f.qualified=true;f.state=GraphState::Unknown;
 check(t.apply(f).empty(),"unready media admitted");f.state=GraphState::Paused;
 auto opened=t.apply(f);check(opened.size()==1&&opened[0].kind=="MediaOpened"&&opened[0].at==100,"paused readiness not admitted at original timestamp");
 check(!opened[0].position,"position fabricated at open");auto epoch=opened[0].epoch;
 check(t.apply(ready(1,f.path,110)).empty(),"duplicate state emitted/reopened");
 auto sample=ready(1,f.path,120);sample.kind=GraphFactKind::Sample;sample.position=0;sample.duration=30000;sample.generation=2;
 check(t.apply(sample).size()==1,"initial paused position absent");
 auto running=ready(1,f.path,200,GraphState::Playing);running.generation=3;
 check(t.apply(running).at(0).kind=="Resumed","resume not emitted");
 sample.state=GraphState::Playing;sample.at=sample.begin=220;sample.position=20;sample.generation=2;
 check(t.apply(sample).empty(),"stale generation accepted");
 sample.generation=3;check(t.apply(sample).size()==1,"position progress absent");
 sample.at=sample.begin=230;check(t.apply(sample).empty(),"unchanged playing position fragments segment");
 auto boundary=ready(1,f.path,240);boundary.kind=GraphFactKind::Segment;boundary.generation=4;
 auto cut=t.apply(boundary);check(cut.size()==1&&cut[0].kind=="Seeked"&&cut[0].epoch==epoch,"seek changed media epoch");
 check(!cut[0].gap,"known seek discarded valid paused dwell before its boundary");
 auto next=t.apply(ready(1,"C:/b/movie.mp4",300));
 check(next.size()==2&&next[0].kind=="MediaClosed"&&next[1].epoch!=epoch,"reused graph merged distinct full paths");
 sample.at=sample.begin=310;sample.generation=5;check(t.apply(sample).empty(),"old path sample entered new media");
 auto close=ready(1,"",400);close.kind=GraphFactKind::Closed;check(t.apply(close).size()==1,"close lost");
 check(t.apply(ready(1,"C:/a/movie.mp4",410)).empty(),"retired binding reactivated");
 auto reopened=t.apply(ready(2,"C:/b/movie.mp4",500));check(reopened.size()==1&&reopened[0].epoch!=next[1].epoch,"same-path reopen merged");
 auto ambiguous=t.apply(ready(3,"C:/preview.mp4",510));check(ambiguous.size()==1&&ambiguous[0].gap,"competing renderer not rejected");
 check(t.apply(ready(2,"C:/b/movie.mp4",520)).empty(),"ambiguous ownership resumed without retirement");
 GraphTimeline ordered;ordered.apply(ready(9,"C:/a.mp4",1000));
 check(ordered.apply(ready(9,"C:/b.mp4",999)).empty(),"late state observation reversed identity");
 GraphTimeline dwell;dwell.apply(ready(10,"C:/a.mp4",0));
 auto beat=dwell.apply(ready(10,"C:/a.mp4",10001));check(beat.size()==1&&beat[0].gap,"unobserved long pause gap silently counted");
 GraphTimeline disconnected;disconnected.apply(ready(11,"C:/a.mp4",0));
 auto lostClose=ready(11,"",12000);lostClose.kind=GraphFactKind::Closed;
 check(disconnected.apply(lostClose).at(0).gap,"unknown 12 seconds before close became valid dwell");
 GraphTimeline replaced;replaced.apply(ready(12,"C:/a.mp4",0));
 check(replaced.apply(ready(12,"C:/b.mp4",12000)).at(0).gap,"unknown interval before source replacement counted as dwell");
 GraphTimeline stopped;stopped.apply(ready(13,"C:/a.mp4",0));
 check(stopped.apply(ready(13,"C:/a.mp4",12000,GraphState::Stopped)).at(0).gap,"unknown interval before stopped state counted as dwell");
 GraphTimeline pressure;pressure.apply(ready(20,"C:/a.mp4",100));TerminalDelivery terminal;terminal.mark(200);terminal.mark(900);
 check(!terminal.attempt([](uint64_t){return 1;}),"terminal rejection forgotten");
 check(pressure.apply(ready(21,"C:/b.mp4",300)).at(0).gap,"lost terminal did not expose ambiguity");
 check(terminal.attempt([&](uint64_t at){check(at==200,"terminal retry fabricated close time");auto end=ready(20,"",at);end.kind=GraphFactKind::Closed;pressure.apply(end);return 0;}),"terminal retry rejected");
 check(pressure.apply(ready(21,"C:/b.mp4",400)).at(0).kind=="MediaOpened","terminal retry failed to retire old binding");
 GraphTimeline tail;tail.apply(ready(30,"C:/a.mp4",0,GraphState::Playing));
 for(uint64_t at=250;at<=5000;at+=250){tail.apply(ready(30,"C:/a.mp4",at,GraphState::Playing));auto point=ready(30,"C:/a.mp4",at,GraphState::Playing);point.kind=GraphFactKind::Sample;point.position=at;point.duration=30000;tail.apply(point);}
 check(tail.apply(ready(30,"C:/a.mp4",5010)).empty(),"pause emitted stale position before final paused sample");
 auto finalPoint=ready(30,"C:/a.mp4",5030);finalPoint.kind=GraphFactKind::Sample;finalPoint.position=5010;finalPoint.duration=30000;
 auto finalTail=tail.apply(finalPoint);check(finalTail.size()>=1&&finalTail[0].kind=="Paused"&&finalTail[0].at==5010&&finalTail[0].position==5010,"pause tail not reconciled at captured state boundary");
 GraphTimeline initialAnchor;auto initialSegment=ready(40,"",10);initialSegment.kind=GraphFactKind::Segment;initialSegment.position=0;initialSegment.generation=2;
 check(initialAnchor.apply(initialSegment).empty(),"segment alone admitted a source");
 auto initialReady=ready(40,"C:/a.mp4",20);initialReady.generation=3;
 check(initialAnchor.apply(initialReady).at(0).position==0,"verified initial stream origin lost before first position query");
 auto waitingPause=[](){GraphTimeline t;t.apply(ready(50,"C:/a.mp4",0,GraphState::Playing));auto p=ready(50,"C:/a.mp4",0,GraphState::Playing);p.kind=GraphFactKind::Sample;p.position=0;p.duration=30000;t.apply(p);t.apply(ready(50,p.path,100));return t;};
 auto changed=waitingPause();auto late=ready(50,"C:/a.mp4",120,GraphState::Playing);late.kind=GraphFactKind::Sample;late.position=120;late.duration=30000;late.generation=1;
 auto changedEvents=changed.apply(late);auto resumed=ready(50,late.path,130,GraphState::Playing);resumed.generation=1;auto later=changed.apply(resumed);changedEvents.insert(changedEvents.end(),later.begin(),later.end());
 for(size_t i=1;i<changedEvents.size();++i)check(changedEvents[i-1].at<=changedEvents[i].at,"new-generation sample overtook deferred pause");
 auto repaused=waitingPause();late.state=GraphState::Paused;auto crossGeneration=repaused.apply(late);
 check(!crossGeneration.empty()&&!crossGeneration[0].position,"new generation position backfilled old pause");
 auto clockInvalidPause=waitingPause();auto invalidTail=late;invalidTail.generation=0;invalidTail.position=225;invalidTail.positionDiscontinuity=true;
 auto invalidTailEvents=clockInvalidPause.apply(invalidTail);
 check(invalidTailEvents.size()==2&&invalidTailEvents[0].kind=="Paused"&&!invalidTailEvents[0].position&&invalidTailEvents[0].at==100,
       "invalid clock position backfilled pending pause");
 check(invalidTailEvents[1].kind=="PositionDiscontinuity"&&!invalidTailEvents[1].gap&&invalidTailEvents[1].state=="Paused",
       "invalid pause tail lost position boundary or confirmed dwell");
 auto expired=waitingPause();late.generation=0;late.begin=late.at=12100;auto afterOutage=expired.apply(late);
 check(afterOutage.size()==2&&afterOutage.back().gap,"expired paused position manufactured 12 seconds of dwell");
 GraphTimeline staleOrigin;staleOrigin.apply(ready(60,"C:/a.mp4",0));staleOrigin.apply(ready(60,"C:/a.mp4",250));
 auto oldOrigin=ready(60,"",100);oldOrigin.kind=GraphFactKind::Segment;oldOrigin.position=4000;staleOrigin.apply(oldOrigin);
 check(!staleOrigin.apply(ready(60,"C:/b.mp4",300)).back().position,"late previous stream origin assigned to new source");
 GraphTimeline consumedOrigin;consumedOrigin.apply(ready(61,"C:/a.mp4",0));oldOrigin.binding=61;consumedOrigin.apply(oldOrigin);
 auto oldSourceSample=ready(61,"C:/a.mp4",200);oldSourceSample.kind=GraphFactKind::Sample;oldSourceSample.position=4000;oldSourceSample.duration=30000;consumedOrigin.apply(oldSourceSample);
 check(!consumedOrigin.apply(ready(61,"C:/b.mp4",300)).back().position,"already observed old source origin assigned to new source");
 GraphTimeline preOpenLoss;oldOrigin.binding=62;preOpenLoss.apply(oldOrigin);preOpenLoss.interrupt(200);
 check(!preOpenLoss.apply(ready(62,"C:/b.mp4",300)).at(0).position,"pre-admission queue loss retained an untrusted origin");
 GraphTimeline longLived;
 for(uint64_t i=1;i<=5000;++i){auto item=ready(i,"C:/a.mp4",i*10);check(longLived.apply(item).size()==1,"normal long-lived playback exhausted retired bindings");item.kind=GraphFactKind::Closed;item.at=item.begin=i*10+1;longLived.apply(item);}
 check(longLived.apply(ready(1,"C:/a.mp4",60000)).empty(),"compacted retirement admitted old binding");
 GraphTimeline speed;auto speedReady=ready(6000,"C:/rate.mp4",0,GraphState::Playing);speedReady.estimatedRate=true;speedReady.rateKnown=false;
 check(!speed.apply(speedReady).at(0).rateKnown,"graph open invented normal playback rate");
 auto point=speedReady;point.kind=GraphFactKind::Sample;point.at=point.begin=25;point.position=25;point.duration=30000;speed.apply(point);
 point.at=point.begin=50;point.position=50;point.rateKnown=true;point.rate=1;
 check(speed.apply(point).at(0).kind=="Snapshot","initial rate confirmation split a segment");
 point.at=point.begin=75;point.position=90;point.rateKnown=false;point.rateChange=true;point.rateWindowFrom=25;
 auto speedBoundary=speed.apply(point).at(0);check(speedBoundary.kind=="RateChanged"&&speedBoundary.estimatedRate&&!speedBoundary.rateKnown&&speedBoundary.rateWindowFrom==25,"unknown-speed transition or evidence window lost");
 point.at=point.begin=100;point.position=140;point.rateKnown=true;point.rate=2;point.rateChange=false;point.rateWindowFrom=0;
 check(speed.apply(point).at(0).kind=="Snapshot","settling rate generated a duplicate boundary");
 point.at=point.begin=125;point.position=365;point.positionDiscontinuity=true;
 auto invalidClock=speed.apply(point).at(0);
 check(invalidClock.kind=="PositionDiscontinuity"&&!invalidClock.gap,"clock mismatch failed to cut playing interval or discarded confirmed dwell");
 std::cout<<"Graph timeline identity, readiness, epochs, retirement, ambiguity and ordering passed\n";
 return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
