#pragma once
#include "GraphPacket.h"
#include "CollectorConfig.h"
namespace pottrace {
class CollectorSink {
 using Init=int32_t(*)(const uint8_t*,uint32_t);using Submit=int32_t(*)(const PotTraceGraphPacket*);using Status=uint32_t(*)();using Retire=int32_t(*)(uint64_t,uint64_t);
 Submit submit=nullptr;Status status=nullptr;HMODULE module=nullptr;
 Retire retire=nullptr;
 using IdentitySwitch=void(*)(int);
 IdentitySwitch identitySwitch=nullptr;
 using Timing=void(*)(uint64_t*);Timing timing=nullptr;
public:
 std::atomic<uint64_t> next{1},lost{0};
 bool Start(const CollectorConfig& config){
  if(submit)return true;if(module||!config.valid)return false;
  auto bridgeConfig=(std::filesystem::path(config.bridge).parent_path()/L"pottrace-bridge.ini").wstring();
  if(ReadCollectorIni(bridgeConfig,L"PotTrace",L"SourceMode")!=L"DirectShow"||ReadCollectorIni(bridgeConfig,L"PotTrace",L"CaptureBackend")!=L"Collector")return false;
  module=LoadLibraryExW(config.bridge.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!module)return false;
  auto initialize=reinterpret_cast<Init>(GetProcAddress(module,"PotTrace_Initialize"));auto enqueue=reinterpret_cast<Submit>(GetProcAddress(module,"PotTrace_Graph"));
  status=reinterpret_cast<Status>(GetProcAddress(module,"PotTrace_CollectorState"));
  retire=reinterpret_cast<Retire>(GetProcAddress(module,"PotTrace_RetireGraph"));
  identitySwitch=reinterpret_cast<IdentitySwitch>(GetProcAddress(module,"PotTrace_EnableItemConfirmation"));
  timing=reinterpret_cast<Timing>(GetProcAddress(module,"PotTrace_ConfirmationTiming"));
  auto folder=std::filesystem::path(config.bridge).parent_path().wstring();
  int n=WideCharToMultiByte(CP_UTF8,0,folder.data(),static_cast<int>(folder.size()),nullptr,0,nullptr,nullptr);std::string encoded(n,'\0');WideCharToMultiByte(CP_UTF8,0,folder.data(),static_cast<int>(folder.size()),encoded.data(),n,nullptr,nullptr);
  if(!initialize||!enqueue||!status||!retire||initialize(reinterpret_cast<const uint8_t*>(encoded.data()),static_cast<uint32_t>(encoded.size()))!=0||(status()&1)==0)return false;
  submit=enqueue;return true;
 }
 uint32_t State() const noexcept{return status?status():0;}
 void EnableConfirmation(bool enabled){if(identitySwitch)identitySwitch(enabled);}
 void ReadTiming(uint64_t* values){if(timing)timing(values);}
 int32_t Close(uint64_t binding,uint64_t at) noexcept{return retire?retire(binding,at):2;}
 int32_t Send(PotTraceGraphPacket packet) noexcept{if(!submit)return 2;auto result=submit(&packet);if(result<0||result==1)++lost;return result;}
};
}
