#include "MpscQueue.h"
#include <thread>
#include <vector>
#include <iostream>
#include <stdexcept>
struct Item {int producer=-1,index=-1;};
void check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
int main(){try{
 pottrace::MpscQueue<Item,2> small;Item item;
 check(!small.pop(item),"empty queue yielded value");check(small.push({0,0})&&small.push({0,1}),"queue capacity lost");
 check(!small.push({0,2}),"full queue overwrote observation");check(small.pop(item)&&item.index==0,"first observation reordered");
 check(small.push({0,2}),"released slot not reused");check(small.pop(item)&&item.index==1&&small.pop(item)&&item.index==2,"wraparound reordered values");
 pottrace::MpscQueue<Item,64> queue;constexpr int count=20000;std::vector<std::thread> workers;
 for(int p=0;p<4;++p)workers.emplace_back([&,p]{for(int n=0;n<count;++n){while(!queue.push({p,n}))std::this_thread::yield();}});
 int next[4]={};for(int total=0;total<4*count;){if(queue.pop(item)){check(item.producer>=0&&item.producer<4,"corrupt producer");check(item.index==next[item.producer]++,"producer order/loss/duplicate");++total;}else std::this_thread::yield();}
 for(auto& worker:workers)worker.join();check(!queue.pop(item),"duplicate tail");
 std::cout<<"Bounded MPSC: full, empty, wraparound and 80000 concurrent observations passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
