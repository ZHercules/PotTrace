#pragma once
#include <cstdint>
#include <mutex>
#include <utility>
namespace pottrace {
// The lock covers only a generation check and a bounded nonblocking publish.
// Never hold it while calling a player/COM interface or waiting for I/O.
class ObservationFence {
 std::mutex gate;uint64_t generation=1;unsigned changes=0;bool alive=true;
 void begin(){std::lock_guard lock(gate);++generation;++changes;}
 void end(){std::lock_guard lock(gate);++generation;--changes;}
public:
 struct Ticket {uint64_t generation;bool allowed;};
 class Change {
  ObservationFence* owner;
 public:
  explicit Change(ObservationFence& value):owner(&value){owner->begin();}
  Change(const Change&)=delete;Change& operator=(const Change&)=delete;
  Change(Change&& other) noexcept:owner(std::exchange(other.owner,nullptr)){}
  ~Change(){if(owner)owner->end();}
 };
 Change changing(){return Change(*this);}
 Ticket capture(){std::lock_guard lock(gate);return {generation,alive&&changes==0};}
 void retire(){std::lock_guard lock(gate);alive=false;++generation;}
 template<class Publish> bool publish(Ticket ticket,Publish&& publish){
  std::unique_lock lock(gate,std::try_to_lock);
  if(!lock||!alive||changes||!ticket.allowed||ticket.generation!=generation)return false;
  std::forward<Publish>(publish)();return true;
 }
};
}
