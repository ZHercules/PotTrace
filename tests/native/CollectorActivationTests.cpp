#include <iostream>
#include <stdexcept>
#include <thread>
#if __has_include("CollectorConfig.h")
#include "CollectorConfig.h"
void check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
 using namespace pottrace;
 check(!ParseCapturePolicy("").valid,"MissingPolicyDoesNotCapture");
 check(!ParseCapturePolicy(R"({"Enabled":true,"RecordingPaused":false,"Revision":1,"Enabled":false})").valid,"duplicate policy keys accepted");
 auto p=ParseCapturePolicy(R"({"Enabled":true,"RecordingPaused":false,"Revision":1})");
 check(p.valid&&p.allowed(),"enabled policy rejected");
 check(!ParseCapturePolicy(R"({"Enabled":true,"RecordingPaused":true,"Revision":2})").allowed(),"paused policy captures");
 check(!IsAbsoluteLocalPath(L"PotTrace.Bridge.dll"),"RejectRelativeBridgePath");
 check(!IsAbsoluteLocalPath(L"\\\\server\\share\\bridge.dll"),"network config accepted");
 check(IsAbsoluteLocalPath(L"D:\\safe folder\\PotTrace.Bridge.dll"),"absolute path rejected");
 wchar_t temporary[MAX_PATH]{},profile[MAX_PATH]{};GetTempPathW(MAX_PATH,temporary);GetTempFileNameW(temporary,L"ptc",0,profile);
 CollectorConfig config;config.valid=true;config.playerIni=profile;
 WritePrivateProfileStringW(L"ExtensionSection",L"PlaybackStatistics",L"PlaybackStatistics - PotTrace.as",profile);
 WritePrivateProfileStringW(L"ExtensionInfoList",L"0",L"PlaybackStatistics - PotTrace.as",profile);
 WritePrivateProfileStringW(L"ExtensionInfoList",L"0DN",L"1",profile);
 check(IsPlayerPluginEnabled(config),"configured selected plugin rejected");
 WritePrivateProfileStringW(L"ExtensionInfoList",L"0DN",L"0",profile);
 const bool capturesUnchecked=IsPlayerPluginEnabled(config);DeleteFileW(profile);
 check(!capturesUnchecked,"unchecked plugin still selected in ExtensionSection must not capture");
 CaptureGate gate;check(!gate.allowed(),"uninitialized capture gate");
 gate.update(p,true);check(gate.allowed(),"ActivationWithoutScript");auto revision=gate.revision();
 gate.update(p,true);check(gate.revision()==revision,"RepeatedActivationIsIdempotent");
 gate.update(p,false);check(!gate.allowed(),"UncheckedPluginDisablesCapture");
 gate.update(p,true);gate.suppress();gate.update(p,true);check(!gate.allowed(),"UserExitPreventsAxRestart");
 GetTempFileNameW(temporary,L"ptc",0,profile);auto replacement=std::wstring(profile)+L".tmp";
 const std::string json=R"({"Enabled":true,"RecordingPaused":false,"Revision":1})";
 {std::ofstream f{std::filesystem::path(profile)};f<<json;}
 std::atomic<bool> reading{true};std::thread reader([&]{while(reading)ReadCapturePolicy(profile);});unsigned sharingFailures=0;DWORD failureCode=0;
 for(unsigned i=0;i<500;++i){
  {std::ofstream f{std::filesystem::path(replacement)};f<<json;}
  if(!ReplaceFileW(profile,replacement.c_str(),nullptr,0,nullptr,nullptr)){++sharingFailures;failureCode=GetLastError();}
 }
 reading=false;reader.join();DeleteFileW(profile);DeleteFileW(replacement.c_str());
 if(sharingFailures)std::cerr<<"replace failures="<<sharingFailures<<" last Win32 error="<<failureCode<<'\n';
 check(sharingFailures==0,"policy reader prevented atomic settings replacement");
 std::cout<<"Collector activation policy tests passed\n";return 0;
 }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
#else
int main(){std::cerr<<"Formal collector activation is not implemented\n";return 1;}
#endif
