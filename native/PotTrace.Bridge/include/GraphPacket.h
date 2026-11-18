#pragma once
#include <cstdint>
// In-process v2 ABI. The receiver copies the UTF-8 path before returning.
struct PotTraceGraphPacket {
 uint32_t size=sizeof(PotTraceGraphPacket),version=2,kind=0,qualified=0;
 uint64_t binding=0,generation=0,begin=0,at=0;
 int32_t state=-1;uint32_t pathLength=0;
 int64_t position=-1,duration=-1;
 double rate=1;
 const char* path=nullptr;
 uint32_t rateFlags=0; // 1 known, 2 estimated, 4 transition confirmed, 8 position discontinuity
 uint64_t rateWindowFrom=0;
};
