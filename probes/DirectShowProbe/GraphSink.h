#pragma once
#include "../../native/PotTrace.Bridge/include/GraphPacket.h"
// Opt-in isolated integration. Loading the diagnostic AX alone never enables
// recording. No registration, writes or library discovery on the stream path.
struct GraphSink {
 using Init=int32_t(*)(const uint8_t*,uint32_t);
 using Submit=int32_t(*)(const PotTraceGraphPacket*);
 Submit submit=nullptr;std::atomic<uint64_t> next{1};std::atomic<unsigned long> lost{0};
 GraphSink(){
  wchar_t path[32768]{};DWORD count=GetEnvironmentVariableW(L"POTTRACE_GRAPH_BRIDGE",path,32768);
  if(!count||count>=32768)return;
  std::wstring full(path,count);auto slash=full.find_last_of(L"\\/");
  if(slash==std::wstring::npos||full.size()<3||full[1]!=L':')return;
  auto module=LoadLibraryExW(full.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module)return;
  auto initialize=reinterpret_cast<Init>(GetProcAddress(module,"PotTrace_Initialize"));
  auto enqueue=reinterpret_cast<Submit>(GetProcAddress(module,"PotTrace_Graph"));
  auto folder=utf8(full.substr(0,slash));
  if(initialize&&enqueue&&initialize(reinterpret_cast<const uint8_t*>(folder.data()),static_cast<uint32_t>(folder.size()))==0)submit=enqueue;
  // The bridge pins itself while workers run; this explicit load is retained.
 }
 int32_t send(uint32_t kind,uint64_t binding,uint64_t generation,uint64_t begin,uint64_t at,int state,const std::string& path={},int64_t position=-1,int64_t duration=-1,double rate=1,bool rateKnown=false,bool rateChange=false,uint64_t rateWindowFrom=0,bool positionDiscontinuity=false) noexcept {
  if(!submit||!binding)return 0;
  PotTraceGraphPacket p;p.kind=kind;p.qualified=1;p.binding=binding;p.generation=generation;p.begin=begin;p.at=at;p.state=state;p.path=path.data();p.pathLength=static_cast<uint32_t>(path.size());p.position=position;p.duration=duration;p.rate=rate;
  p.rateFlags=2|(rateKnown?1:0)|(rateChange?4:0)|(positionDiscontinuity?8:0);p.rateWindowFrom=rateWindowFrom;
  const auto result=submit(&p);if(result!=0)++lost;return result;
 }
};
GraphSink& graphSink(){static GraphSink* sink=new GraphSink;return *sink;}
