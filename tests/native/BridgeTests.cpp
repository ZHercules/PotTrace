#include "BridgeSupport.h"
#include "PlaybackIdentity.h"
#include <iostream>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 using namespace pottrace;
 PlaybackIdentity item;
 const std::string a="D:\\one\\episode.mp4",b="D:\\two\\episode.mp4";
 item.callback("Open",a,1);check(!item.match(a,2),"disabled identity");
 item.enable(true);item.callback("Open",a,10);auto first=item.match(a,11);
 check(first&&!item.match(a,9)&&!item.match(b,11),"preloaded or unconfirmed source");
 item.callback("Time",a,15);check(item.match(a,16)==first,"heartbeat reopened media");
 item.callback("Complete",a,20);check(!item.match(a,21),"completed media");
 item.callback("Time",b,22);auto second=item.match(b,23);check(second&&second!=first&&!item.match(a,23),"seamless transition");
 item.callback("Close",a,24);check(item.match(b,25)==second,"late close cleared current");
 item.callback("Open",b,26);check(item.match(b,27)!=second,"same path reopen");
 item.enable(false);item.enable(true);check(!item.match(b,28),"stale identity after disable");
 item.callback("Time",b,30,40000,10);item.callback("Time",b,31,40000,1010);
 auto proof=item.observation(b,32);check(proof.first==10&&proof.next==1010&&proof.epoch!=0,"missing named positions");
 check(item.observation(a,32).first<0,"other file inherited reported position");
 item.callback("Open",b,33);check(item.observation(b,34).first<0,"reopen inherited positions");
 check(quote("a\"b\\c\n")=="\"a\\\"b\\\\c\\n\"","JSON escaping");
 Queue q;std::string original="Chinese path copied";check(q.push(original),"enqueue");original="changed";
 check(q.pop()=="Chinese path copied","synchronous copy");
 for(int i=0;i<4096;i++)check(q.push("x"),"queue limit");check(!q.push("overflow"),"bounded event count");
 Queue bytes;check(!bytes.push(std::string(8*1024*1024+1,'x')),"bounded bytes");
 check(classify(250,200,1).kind=="Snapshot","frame quantization");
 check(classify(250,500,1).kind=="Snapshot","500ms cached position must not look like 2x");
 check(classify(1000,2000,1).kind=="RateChanged","large speed change");
 check(estimateRate(2000,4000,1)==2,"stable 2x slope");
 check(estimateRate(2000,2499,1)==1,"cache jitter must not change speed");
 check(classify(250,1000,1).rate==2,"first cached 2x update is not 4x");
 check(estimateRate(2000,4999,2)==2,"2x cache jitter scales with speed");
 check(batchFits(800000,8,100000),"large-path batch below cap");
 check(!batchFits(900000,9,100000),"large-path batch cap");
 check(classify(250,10000,1).kind=="Seeked","forward seek");
 check(classify(250,-5000,1).kind=="Seeked","loop seek");
 check(classify(10000,100,1).gap,"sleep gap");
 std::cout<<"All bridge behaviors passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
