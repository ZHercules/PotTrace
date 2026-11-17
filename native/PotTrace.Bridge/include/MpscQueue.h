#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <type_traits>
namespace pottrace {
// Bounded sequence-cell ring: producers reserve a slot, publish with release;
// the single consumer acquires it and releases that slot for the next lap.
// No mutex, disk I/O, waits, or allocations within push/pop. Full returns false.
template<class T,size_t Capacity> class MpscQueue {
 static_assert(Capacity>=2&&(Capacity&(Capacity-1))==0);
 static_assert(std::is_nothrow_move_constructible_v<T>&&std::is_nothrow_move_assignable_v<T>,"A reserved ring slot must always be published/released");
 struct Cell {std::atomic<size_t> sequence{0};T value{};};
 std::unique_ptr<Cell[]> cells=std::make_unique<Cell[]>(Capacity);
 alignas(64) std::atomic<size_t> producer{0};
 alignas(64) size_t consumer=0;
public:
 MpscQueue(){for(size_t i=0;i<Capacity;++i)cells[i].sequence.store(i,std::memory_order_relaxed);}
 bool push(T value) noexcept {
  size_t position=producer.load(std::memory_order_relaxed);
  for(unsigned retry=0;retry<32;++retry){
   auto& cell=cells[position&(Capacity-1)];const auto sequence=cell.sequence.load(std::memory_order_acquire);
   const auto difference=static_cast<intptr_t>(sequence)-static_cast<intptr_t>(position);
   if(difference==0){
    if(producer.compare_exchange_weak(position,position+1,std::memory_order_relaxed)){
     cell.value=std::move(value);cell.sequence.store(position+1,std::memory_order_release);return true;
    }
   }else if(difference<0)return false;
   else position=producer.load(std::memory_order_relaxed);
  }return false;
 }
 bool pop(T& value) noexcept {
  auto& cell=cells[consumer&(Capacity-1)];
  if(cell.sequence.load(std::memory_order_acquire)!=consumer+1)return false;
  value=std::move(cell.value);cell.sequence.store(consumer+Capacity,std::memory_order_release);++consumer;return true;
 }
};
}
