#include "ObservationFence.h"
#include <iostream>
#include <future>
#include <thread>
#include <stdexcept>
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
 pottrace::ObservationFence fence;int published=0;
 auto initial=fence.capture();check(fence.publish(initial,[&]{++published;}),"stable observation rejected");
 auto beforeChange=fence.capture();
 {auto outer=fence.changing();check(!fence.publish(fence.capture(),[&]{++published;}),"accepted during transition");
  {auto inner=fence.changing();}
  check(!fence.publish(fence.capture(),[&]{++published;}),"nested transition incorrectly reopened fence");}
 check(!fence.publish(beforeChange,[&]{++published;}),"ABA/lifecycle change accepted an old observation");
 check(fence.publish(fence.capture(),[&]{++published;}),"stable observation after transition rejected");
 std::promise<void> captured,release;auto released=release.get_future();bool staleAccepted=true;
 std::thread reader([&]{auto ticket=fence.capture();captured.set_value();released.wait();staleAccepted=fence.publish(ticket,[&]{++published;});});
 captured.get_future().wait();fence.retire();release.set_value();reader.join();
 check(!staleAccepted,"query that finished after stop was published");
 check(!fence.publish(fence.capture(),[&]{++published;}),"retired connection reactivated");
 check(published==2,"unexpected publication count");
 std::cout<<"Observation fence: stable, nested change, ABA and concurrent retirement passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
