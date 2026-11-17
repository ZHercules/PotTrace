#pragma once
#include <atomic>
#include <cstdint>
namespace pottrace {
// Shared by the lifecycle caller and surviving observer workers. The original
// close timestamp survives temporary backpressure; workers retry off the stream.
class TerminalDelivery {
 std::atomic<uint64_t> ended{0};std::atomic<bool> accepted{false};
public:
 void mark(uint64_t at){uint64_t empty=0;ended.compare_exchange_strong(empty,at+1);}
 template<class Send> bool attempt(Send&& send){
  if(accepted)return true;
  const auto value=ended.load();if(!value)return false;
  // Duplicate successful delivery is harmless; no worker can forget a rejection.
  // A negative result means the entire bridge is permanently unavailable;
  // release COM resources instead of retaining a stopped graph forever.
  if(send(value-1)<=0){accepted=true;return true;}return false;
 }
};
}
