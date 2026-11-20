#include <windows.h>
#include <sddl.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include "../include/BridgeSupport.h"
#include "../include/GraphTimeline.h"
#include "../include/GraphPacket.h"
#include "../include/PlaybackIdentity.h"
#include "../include/PlaybackWarmup.h"
#include "../../PotTrace.Collector/include/CollectorConfig.h"
#include "../include/MpscQueue.h"

namespace {
using namespace pottrace;
std::wstring wide(std::string_view s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring r(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n);return r;}
std::string utf8(std::wstring_view s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::string guid(){GUID g;CoCreateGuid(&g);wchar_t value[40];StringFromGUID2(g,value,40);return utf8(std::wstring_view(value+1,36));}
std::string utc(){SYSTEMTIME t;GetSystemTime(&t);char s[40];sprintf_s(s,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return s;}
std::string shiftedUtc(const std::string& stamp,int64_t deltaMs){
 if(!deltaMs)return stamp;SYSTEMTIME t{};
 if(sscanf_s(stamp.c_str(),"%hu-%hu-%huT%hu:%hu:%hu.%huZ",&t.wYear,&t.wMonth,&t.wDay,&t.wHour,&t.wMinute,&t.wSecond,&t.wMilliseconds)!=7)return stamp;
 FILETIME ft{};if(!SystemTimeToFileTime(&t,&ft))return stamp;ULARGE_INTEGER ticks;ticks.LowPart=ft.dwLowDateTime;ticks.HighPart=ft.dwHighDateTime;ticks.QuadPart+=deltaMs*10000;
 ft.dwLowDateTime=ticks.LowPart;ft.dwHighDateTime=ticks.HighPart;FileTimeToSystemTime(&ft,&t);
 char value[40];sprintf_s(value,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return value;
}
std::wstring sid(){HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return {};DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<char> b(n);GetTokenInformation(token,TokenUser,b.data(),n,&n);CloseHandle(token);LPWSTR text=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid,&text))return {};std::wstring r=text;LocalFree(text);return r;}
struct Raw {std::string text;ULONGLONG at;std::string stamp;};
struct GraphRaw {GraphFact fact;std::string stamp;uint64_t lossSerial=0;};
struct Out {long long seq;std::string json;};
struct GraphRetirement {std::atomic<uint64_t> binding{0};uint64_t at=0;};
struct State {
 PlaybackIdentity item;bool confirmCallbackPath=false;
 PlaybackWarmup warmup;
 std::atomic<uint64_t> firstCallback{0},firstReady{0},firstMatched{0};
 std::atomic<bool> stop=false,suppressed=false,pluginDisabled=false,scriptKnown=false;std::mutex rawGate,outGate;
 std::deque<Raw> raw;size_t rawBytes=0;bool rawGap=false;
 MpscQueue<GraphRaw,4096> graphRaw;std::atomic<size_t> graphBytes{0};GraphTimeline timeline;uint64_t graphEpoch=0;
 std::atomic<uint64_t> graphLoss{0};uint64_t graphHandledLoss=0;
 GraphRetirement retirements[16];
 bool graphMode=false,collectorMode=false,positionKnown=true,durationKnown=true;
 bool rateKnown=true,rateEstimated=false;uint64_t rateWindowFrom=0;
 std::deque<Out> outgoing;size_t outBytes=0;bool outGap=false;
 std::wstring folder,exe,data,channel,pipe;std::string instance=guid(),epoch,path;
 long long seq=0,pos=0,duration=0,ratePos=0;ULONGLONG last=0,rateAt=0;double rate=1;int warmSamples=0;std::string playState="Unknown";bool active=false;HWND hwnd=nullptr;
};
State* state=nullptr;std::mutex initGate;
#ifdef _DEBUG
void testTrace(const wchar_t* variable,std::string_view message){
 wchar_t file[32768];DWORD n=GetEnvironmentVariableW(variable,file,32768);if(!n||n>=32768)return;
 HANDLE stream=CreateFileW(file,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(stream!=INVALID_HANDLE_VALUE){auto line=std::to_string(GetTickCount64())+"\t"+std::string(message)+"\n";DWORD written;WriteFile(stream,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);CloseHandle(stream);}
}
#endif
BOOL CALLBACK findWindow(HWND h,LPARAM arg){DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;wchar_t cls[80];GetClassNameW(h,cls,80);if(wcscmp(cls,L"PotPlayer64")==0){*reinterpret_cast<HWND*>(arg)=h;return FALSE;}return TRUE;}
#ifdef POTTRACE_BRIDGE_TEST
bool query(HWND h,WPARAM key,long long& value);
#else
bool query(HWND h,WPARAM key,long long& value){DWORD_PTR result=0;if(!h||!SendMessageTimeoutW(h,WM_USER,key,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,80,&result))return false;value=static_cast<LONG_PTR>(result);return true;}
#endif
void emit(State& s,std::string kind,ULONGLONG now,const std::string& stamp,bool gap=false){
 if(s.suppressed)return;
 std::lock_guard lock(s.outGate);
 if(s.outGap){gap=true;s.outGap=false;}
 long long seq=++s.seq;
 std::string j="{\"protocolVersion\":1,\"instanceId\":"+quote(s.instance)+",\"mediaEpoch\":"+(s.epoch.empty()?"null":quote(s.epoch))+",\"sequence\":"+std::to_string(seq)+",\"kind\":"+quote(kind)+",\"observedAtUtc\":"+quote(stamp)+",\"monotonicMs\":"+std::to_string(now)+",\"sourcePath\":"+(s.path.empty()?"null":quote(s.path))+",\"positionMs\":"+(s.positionKnown?std::to_string(std::max(0LL,s.pos)):"null")+",\"durationMs\":"+(s.durationKnown?std::to_string(std::max(0LL,s.duration)):"null")+",\"state\":"+quote(s.playState)+",\"rate\":"+(s.rateKnown?std::to_string(s.rate):"null")+",\"rateIsEstimated\":"+(s.rateEstimated?"true":"false")+",\"rateWindowStartMs\":"+(s.rateWindowFrom?std::to_string(s.rateWindowFrom):"null")+",\"quality\":"+quote(gap?"Gap":s.graphMode?"Observed":"Estimated")+"}";
 if(s.outgoing.size()>=4096||s.outBytes+j.size()>8*1024*1024){s.outGap=true;return;}
#ifdef _DEBUG
 // Explicit fixture diagnostics only; normal builds never write event traces.
 wchar_t tracePath[32768];
 if(GetEnvironmentVariableW(L"POTTRACE_TEST_EVENT_TRACE",tracePath,32768)>0){
  HANDLE trace=CreateFileW(tracePath,FILE_APPEND_DATA,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
  if(trace!=INVALID_HANDLE_VALUE){auto line=j+"\n";DWORD written;WriteFile(trace,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);CloseHandle(trace);}
 }
#endif
 s.outBytes+=j.size();s.outgoing.push_back({seq,std::move(j)});
}
void sample(State& s,ULONGLONG now){
 if(s.graphMode)return;
 if(!s.hwnd||!IsWindow(s.hwnd))EnumWindows(findWindow,reinterpret_cast<LPARAM>(&s.hwnd));
 long long status=0,pos=0,total=0;
 if(!query(s.hwnd,0x5006,status)||!query(s.hwnd,0x5004,pos)||!query(s.hwnd,0x5002,total)){
  if(s.active)emit(s,"Gap",now,utc(),true);s.last=now;return;
 }
#ifdef _DEBUG
 testTrace(L"POTTRACE_TEST_RAW_TRACE","Sample\t"+std::to_string(status)+"\t"+std::to_string(pos)+"\t"+std::to_string(total));
#endif
 if(!s.active&&s.path.empty())return;
 if(!s.active){
  if((status!=1&&status!=2)||total<=0)return;
  s.active=true;s.pos=pos;s.duration=total;s.playState=status==2?"Playing":"Paused";s.last=now;s.warmSamples=0;s.rateAt=now;s.ratePos=pos;
  emit(s,"MediaOpened",now,utc());return;
 }
 // The verified WM_USER position is refreshed in roughly 500 ms steps.
 // Keep the previous anchor across a brief unchanged sample instead of
 // inventing a pause or a rate change from a quantized value.
 if(status==2&&s.playState=="Playing"&&pos==s.pos&&now-s.last<900)return;
 auto old=s.playState;auto previous=s.pos;auto dt=static_cast<long long>(now-s.last);
 s.pos=pos;s.duration=total;s.playState=status==2?"Playing":status==1?"Paused":"Stopped";
 std::string kind="Snapshot";bool gap=dt>1500;
 if(gap)kind="Gap";
 else if(s.playState=="Stopped"){kind="Stopped";s.active=false;}
 else if(s.playState=="Paused"&&old!="Paused")kind="Paused";
 else if(s.playState=="Playing"&&old=="Paused")kind="Resumed";
 else if(s.playState=="Playing"&&(old=="Playing"||old=="Buffering")){
  auto result=classify(dt,pos-previous,s.rate);
  if(s.warmSamples<2&&pos>previous&&dt>0&&dt<=1500&&pos-previous<=dt*s.rate+100){result={"Snapshot",s.rate,false};s.warmSamples++;}
  kind=result.kind;gap=result.gap;s.rate=result.rate;
  if(kind=="BufferingStarted")s.playState="Buffering";
  else if(old=="Buffering"&&kind=="Snapshot")kind="BufferingEnded";
 }
 if(kind=="Snapshot"&&s.playState=="Playing"&&now-s.rateAt>=2000){
  double estimated=estimateRate(static_cast<long long>(now-s.rateAt),pos-s.ratePos,s.rate);
  if(estimated!=s.rate){s.rate=estimated;kind="RateChanged";}
  s.rateAt=now;s.ratePos=pos;
 }else if(kind!="Snapshot"){s.rateAt=now;s.ratePos=pos;}
 emit(s,kind,now,utc(),gap);s.last=now;if(kind=="Stopped")s.path.clear();
}
void callback(State& s,const Raw& raw){
 if(s.graphMode)return;
 auto a=raw.text.find('\t'),b=raw.text.find('\t',a+1),c=raw.text.find('\t',b+1);if(a==std::string::npos||b==std::string::npos||c==std::string::npos)return;
 auto kind=raw.text.substr(0,a),path=raw.text.substr(c+1);long long total=0;
 try{total=std::stoll(raw.text.substr(a+1,b-a-1));}catch(...){return;}
 ULONGLONG now=std::max(raw.at,s.last);
 if(kind=="Open"){
  if(s.active){emit(s,"MediaClosed",now,raw.stamp);s.active=false;}
  s.path=path;s.epoch=guid();s.rate=1;s.pos=0;s.duration=0;s.last=now;s.playState="Unknown";emit(s,"MediaOpened",now,raw.stamp);return;
 }
 if(kind=="Start"){
  if(s.active&&s.path==path){s.duration=total;return;}
  if(s.epoch.empty()||s.path!=path){if(s.active)emit(s,"MediaClosed",now,raw.stamp);s.epoch=guid();}
  s.path=path;s.duration=total;s.pos=0;s.playState="Unknown";s.active=false;s.last=now;s.rate=1;
  // Start can arrive after Pause or even Stop. Only a current successful
  // observation may activate the session; a timeout leaves it pending.
  sample(s,std::max(now,GetTickCount64()));return;
 }
 if(!s.active||s.path!=path){if(kind=="Close"&&s.path==path)s.path.clear();return;}
 if(kind=="Time")return;
 if(kind=="Pause"||kind=="Resume"){
  long long actual=0;if(!query(s.hwnd,0x5006,actual))return;
  auto desired=kind=="Pause"?"Paused":"Playing";
  if((kind=="Pause"&&actual!=1)||(kind=="Resume"&&actual!=2)||s.playState==desired)return;
 }
 query(s.hwnd,0x5004,s.pos);
 if(kind=="Pause")s.playState="Paused";
 if(kind=="Resume")s.playState="Playing";
 emit(s,kind=="Pause"?"Paused":kind=="Resume"?"Resumed":kind=="Complete"?"Completed":"MediaClosed",now,raw.stamp);
 s.last=now;s.rateAt=now;s.ratePos=s.pos;if(kind=="Close"||kind=="Complete"){s.active=false;s.epoch.clear();s.path.clear();}
}
void graphTransitions(State& s,const std::vector<GraphTransition>& transitions,const std::string& stamp,uint64_t referenceAt){
 for(const auto& e:transitions){
  if(s.graphEpoch!=e.epoch){s.graphEpoch=e.epoch;s.epoch=guid();}
  s.path=e.path;s.pos=e.position.value_or(0);s.duration=e.duration.value_or(0);
  s.positionKnown=e.position.has_value();s.durationKnown=e.duration.has_value();
  s.playState=e.state;s.rate=e.rate;s.active=e.kind!="MediaClosed";s.last=e.at;
  s.rateKnown=e.rateKnown;s.rateEstimated=e.estimatedRate;s.rateWindowFrom=e.rateWindowFrom;
  emit(s,e.kind,e.at,shiftedUtc(stamp,static_cast<int64_t>(e.at)-static_cast<int64_t>(referenceAt)),e.gap);
 }
}
void graphCallback(State& s,const GraphFact& fact,const std::string& stamp){
 for(const auto& confirmed:s.warmup.accept(fact))
  graphTransitions(s,s.timeline.apply(confirmed),stamp,fact.at);
}
void drainGraphRetirements(State& s){
 for(auto& slot:s.retirements){auto binding=slot.binding.load();if(!binding||binding==UINT64_MAX)continue;auto at=slot.at;slot.binding=0;
  GraphFact close;close.kind=GraphFactKind::Closed;close.binding=binding;close.begin=close.at=at;
  graphCallback(s,close,shiftedUtc(utc(),static_cast<int64_t>(at)-static_cast<int64_t>(GetTickCount64())));
 }
}
void observe(State* p){auto& s=*p;EnumWindows(findWindow,reinterpret_cast<LPARAM>(&s.hwnd));emit(s,"InstanceStarted",GetTickCount64(),utc());ULONGLONG next=0;bool wasSuppressed=false;
 while(!s.stop){
  std::deque<Raw> batch;bool gap=false;{std::lock_guard lock(s.rawGate);batch.swap(s.raw);s.rawBytes=0;gap=s.rawGap;s.rawGap=false;}
  if(gap)emit(s,"Gap",GetTickCount64(),utc(),true);
  for(const auto& raw:batch)callback(s,raw);
  GraphRaw raw;for(int count=0;count<4096&&s.graphRaw.pop(raw);++count){
   s.graphBytes-=sizeof(GraphRaw)+raw.fact.path.size();
   if(raw.lossSerial!=s.graphHandledLoss){s.warmup.reset();graphTransitions(s,s.timeline.interrupt(raw.fact.at),raw.stamp,raw.fact.at);s.graphHandledLoss=raw.lossSerial;}
   graphCallback(s,raw.fact,raw.stamp);
  }
  drainGraphRetirements(s);
  if(s.suppressed){std::lock_guard lock(s.outGate);s.outgoing.clear();s.outBytes=0;s.active=false;wasSuppressed=true;Sleep(20);continue;}
  if(wasSuppressed){s.epoch=guid();s.active=false;wasSuppressed=false;}
  auto now=GetTickCount64();if(now>=next){sample(s,now);next=now+(s.playState=="Paused"?500:250);}
  Sleep(20);
 }
}
bool transfer(HANDLE pipe,bool write,void* data,DWORD length){
 auto bytes=static_cast<char*>(data);while(length){OVERLAPPED ov{};ov.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);DWORD count=0;
  BOOL ok=write?WriteFile(pipe,bytes,length,&count,&ov):ReadFile(pipe,bytes,length,&count,&ov);
  if(!ok&&GetLastError()==ERROR_IO_PENDING){if(WaitForSingleObject(ov.hEvent,2000)!=WAIT_OBJECT_0){CancelIoEx(pipe,&ov);WaitForSingleObject(ov.hEvent,INFINITE);CloseHandle(ov.hEvent);return false;}ok=GetOverlappedResult(pipe,&ov,&count,FALSE);}
  CloseHandle(ov.hEvent);if(!ok||count==0)return false;bytes+=count;length-=count;
 }return true;
}
bool send(HANDLE pipe,std::string json){DWORD len=static_cast<DWORD>(json.size());return transfer(pipe,true,&len,4)&&transfer(pipe,true,json.data(),len);}
bool receive(HANDLE pipe,std::string& json){DWORD len=0;if(!transfer(pipe,false,&len,4)||len==0||len>1024*1024)return false;json.resize(len);return transfer(pipe,false,json.data(),len);}
std::wstring ini(const std::wstring& file,const wchar_t* key,const wchar_t* fallback=L""){wchar_t value[32768];GetPrivateProfileStringW(L"PotTrace",key,fallback,value,32768,file.c_str());return value;}
bool launch(State& s){if(s.exe.empty())return false;std::wstring args=L"\""+s.exe+L"\" --background --channel "+s.channel;if(s.collectorMode)args+=L" --collector";if(!s.data.empty())args+=L" --data-dir \""+s.data+L"\"";STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
 if(!CreateProcessW(s.exe.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;}
void transport(State* p){auto& s=*p;auto file=s.folder+L"\\pottrace-bridge.ini";s.exe=ini(file,L"HostPath");s.data=ini(file,L"DataDirectory");s.channel=ini(file,L"Channel",L"main");
 if(s.channel.empty()||s.channel.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")!=std::wstring::npos)return;
 DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);s.pipe=L"\\\\.\\pipe\\PotTrace."+sid()+L"."+std::to_wstring(session)+L"."+s.channel;
 std::deque<ULONGLONG> launches;ULONGLONG retryAt=0;
 while(!s.stop){
  if(s.graphMode&&s.suppressed){Sleep(100);continue;}
  HANDLE pipe=CreateFileW(s.pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
  if(pipe==INVALID_HANDLE_VALUE){auto now=GetTickCount64();while(!launches.empty()&&now-launches.front()>=60000)launches.pop_front();if(!s.suppressed&&now>=retryAt&&launches.size()<3){launch(s);launches.push_back(now);retryAt=now+(launches.size()==1?1000:launches.size()==2?5000:15000);}Sleep(250);continue;}
  std::string response;bool ok=send(pipe,"{\"version\":1,\"type\":\"Hello\",\"instanceId\":"+quote(s.instance)+",\"processId\":"+std::to_string(GetCurrentProcessId())+"}")&&receive(pipe,response);
  if(ok&&!s.graphMode)s.suppressed=false;
  while(ok&&!s.stop){
   DWORD available=0;if(!PeekNamedPipe(pipe,nullptr,0,nullptr,&available,nullptr))break;
   if(available){if(!receive(pipe,response))break;if(response.find("UserExit")!=std::string::npos){s.suppressed=true;break;}}
   std::vector<Out> batch;size_t batchBytes=0;{std::lock_guard lock(s.outGate);for(auto& item:s.outgoing){if(!batchFits(batchBytes,batch.size(),item.json.size()))break;batchBytes+=item.json.size();batch.push_back(item);}}
   if(batch.empty()){Sleep(50);continue;}
   std::string frame="{\"version\":1,\"type\":\"Events\",\"instanceId\":"+quote(s.instance)+",\"events\":[";
   for(size_t i=0;i<batch.size();i++){if(i)frame+=',';frame+=batch[i].json;}frame+="]}";
   ok=send(pipe,std::move(frame))&&receive(pipe,response);if(!ok)break;
   if(response.find("UserExit")!=std::string::npos){s.suppressed=true;break;}
   auto at=response.find("\"ackSequence\":");if(at==std::string::npos){ok=false;break;}
   long long ack=0;try{ack=std::stoll(response.substr(at+14));}catch(...){ok=false;break;}
   {std::lock_guard lock(s.outGate);while(!s.outgoing.empty()&&s.outgoing.front().seq<=ack){s.outBytes-=s.outgoing.front().json.size();s.outgoing.pop_front();}}
   Sleep(200);
  }
  CancelIoEx(pipe,nullptr);CloseHandle(pipe);Sleep(250);
 }
}
DWORD WINAPI observeEntry(void* value){observe(static_cast<State*>(value));return 0;}
DWORD WINAPI transportEntry(void* value){transport(static_cast<State*>(value));return 0;}
}
extern "C" __declspec(dllexport) int32_t PotTrace_Initialize(const uint8_t* bytes,uint32_t length){
 if(!bytes||!length||length>131072)return -1;std::lock_guard lock(initGate);if(state)return state->stop?-2:0;
 auto folder=wide(std::string_view(reinterpret_cast<const char*>(bytes),length));if(folder.empty())return -1;
 HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&PotTrace_Initialize),&pinned);
 state=new State;state->folder=std::move(folder);
 state->graphMode=ini(state->folder+L"\\pottrace-bridge.ini",L"SourceMode")==L"DirectShow";
 state->collectorMode=ini(state->folder+L"\\pottrace-bridge.ini",L"CaptureBackend")==L"Collector";
 if(state->graphMode&&state->collectorMode){
  auto config=LoadCollectorConfig(std::filesystem::path(state->folder)/L"PotTrace.Collector.ax");
  state->confirmCallbackPath=config.valid&&config.registryProfile;
  if(state->confirmCallbackPath)state->item.enable(ReadCapturePolicy(config.policy).allowed()&&IsPlayerPluginEnabled(config));
 }
 // CreateThread does not wait for the thread's CRT startup handshake. Waiting
 // here can stall the host's extension dispatch while it holds internal locks.
 bool observeEnabled=true,sendEnabled=true;
#ifdef _DEBUG
 wchar_t workers[64];if(GetEnvironmentVariableW(L"POTTRACE_TEST_WORKERS",workers,64)){
  observeEnabled=wcscmp(workers,L"sender")==0||wcscmp(workers,L"none")==0?false:true;
  sendEnabled=wcscmp(workers,L"observer")==0||wcscmp(workers,L"none")==0?false:true;
 }
#endif
 HANDLE observer=observeEnabled?CreateThread(nullptr,0,observeEntry,state,0,nullptr):nullptr;
 HANDLE sender=sendEnabled?CreateThread(nullptr,0,transportEntry,state,0,nullptr):nullptr;
 if(observer)CloseHandle(observer);if(sender)CloseHandle(sender);
 if((observeEnabled&&!observer)||(sendEnabled&&!sender)){state->stop=true;return -3;}return 0;
}
extern "C" __declspec(dllexport) int32_t PotTrace_Enqueue(const uint8_t* bytes,uint32_t length){
 auto p=state;if(!p||p->stop||!bytes||length>262144)return -1;
 if(p->graphMode){
  std::string_view message(reinterpret_cast<const char*>(bytes),length);
  if(message.starts_with("Ready\t")){p->pluginDisabled=false;p->scriptKnown=true;}
  else if(p->confirmCallbackPath&&!p->pluginDisabled&&!p->suppressed){
   auto kindEnd=message.find('\t');
   auto second=kindEnd==message.npos?message.npos:message.find('\t',kindEnd+1);
   auto third=second==message.npos?message.npos:message.find('\t',second+1);
   if(third!=message.npos){try{
    const auto now=GetTickCount64();
    auto number=[](std::string_view field){int64_t n=-1;auto result=std::from_chars(field.data(),field.data()+field.size(),n);return result.ec==std::errc()&&result.ptr==field.data()+field.size()?n:int64_t(-1);};
    p->item.callback(message.substr(0,kindEnd),message.substr(third+1),now,
     number(message.substr(kindEnd+1,second-kindEnd-1)),number(message.substr(second+1,third-second-1)));
    uint64_t unset=0;if(p->item.current(now+1))p->firstCallback.compare_exchange_strong(unset,now);
   }catch(...){p->item.enable(false);return -1;}}
  }
  return 0;
 }
#ifdef _DEBUG
 testTrace(L"POTTRACE_TEST_RAW_TRACE",std::string_view(reinterpret_cast<const char*>(bytes),length));
#endif
 std::lock_guard lock(p->rawGate);if(p->raw.size()>=4096||p->rawBytes+length>8*1024*1024){p->rawGap=true;return 1;}
 p->raw.push_back({std::string(reinterpret_cast<const char*>(bytes),length),GetTickCount64(),utc()});p->rawBytes+=length;return 0;
}
extern "C" __declspec(dllexport) int32_t PotTrace_Shutdown(){if(state){if(state->graphMode){state->pluginDisabled=true;state->item.enable(false);state->scriptKnown=true;}else state->stop=true;}return 0;}
extern "C" __declspec(dllexport) void PotTrace_ConfirmationTiming(uint64_t* values){
 auto p=state;if(!p||!values)return;
 values[0]=p->firstCallback.load();values[1]=p->firstReady.load();values[2]=p->firstMatched.load();
}
extern "C" __declspec(dllexport) uint32_t PotTrace_CollectorState(){auto p=state;return !p?0:(p->graphMode?1u:0u)|(p->pluginDisabled?2u:0u)|(p->suppressed?4u:0u)|(p->stop?8u:0u)|(p->scriptKnown?16u:0u);}
extern "C" __declspec(dllexport) void PotTrace_EnableItemConfirmation(int enabled){
 auto p=state;if(p&&p->confirmCallbackPath)p->item.enable(enabled&&!p->pluginDisabled&&!p->suppressed&&!p->stop);
}
extern "C" __declspec(dllexport) int32_t PotTrace_RetireGraph(uint64_t binding,uint64_t at){
 auto p=state;if(!p||p->stop||!p->graphMode||!binding||binding==UINT64_MAX||at>GetTickCount64())return -1;
 for(auto& slot:p->retirements){uint64_t empty=0;if(slot.binding.compare_exchange_strong(empty,UINT64_MAX)){slot.at=at;slot.binding=binding;return 0;}}
 ++p->graphLoss;return 1;
}
extern "C" __declspec(dllexport) int32_t PotTrace_Graph(const PotTraceGraphPacket* packet){
 auto p=state;if(!p||p->stop||!p->graphMode||!packet||packet->size!=sizeof(PotTraceGraphPacket)||packet->kind>4||packet->rateFlags>31||packet->rateWindowFrom>packet->at||packet->pathLength>131072||(!packet->path&&packet->pathLength))return -1;
 const bool corroborate=packet->version==3&&packet->qualified==2&&p->confirmCallbackPath;
 if(!corroborate&&(packet->version!=2||packet->qualified>1))return -1;
 if((packet->rateFlags&16)&&(!corroborate||packet->kind!=1||packet->state!=2))return -1;
 auto now=GetTickCount64();if(packet->at>now||packet->begin>packet->at)return -1;
 if(packet->kind!=3&&(p->pluginDisabled||p->suppressed))return 2;
 try{
  GraphFact f;f.kind=static_cast<GraphFactKind>(packet->kind);f.binding=packet->binding;f.generation=packet->generation;f.begin=packet->begin;f.at=packet->at;f.state=static_cast<GraphState>(packet->state);f.qualified=packet->qualified!=0;
  if(packet->pathLength)f.path.assign(packet->path,packet->pathLength);
  if(corroborate){
   auto proof=p->item.observation(f.path,f.begin);f.confirmationRequired=true;f.policyEpoch=proof.policy;
   f.identityEpoch=f.kind==GraphFactKind::Segment?p->item.current(f.begin):proof.epoch;
   f.witnessFirst=proof.first;f.witnessNext=proof.next;f.witnessDuration=proof.total;
   f.witnessStartDuration=proof.startDuration;f.foregroundStart=(packet->rateFlags&16)!=0;
   if(f.kind==GraphFactKind::Ready&&f.state==GraphState::Playing){
    uint64_t unset=0;p->firstReady.compare_exchange_strong(unset,f.at);
    unset=0;if(f.identityEpoch)p->firstMatched.compare_exchange_strong(unset,f.at);
   }
  }
  if(packet->position>=0)f.position=packet->position;if(packet->duration>=0)f.duration=packet->duration;f.rate=packet->rate;
  f.rateKnown=(packet->rateFlags&1)!=0;f.estimatedRate=(packet->rateFlags&2)!=0;f.rateChange=(packet->rateFlags&4)!=0;f.rateWindowFrom=packet->rateWindowFrom;
  f.positionDiscontinuity=(packet->rateFlags&8)!=0;
  FILETIME filetime;GetSystemTimeAsFileTime(&filetime);ULARGE_INTEGER ticks;ticks.LowPart=filetime.dwLowDateTime;ticks.HighPart=filetime.dwHighDateTime;ticks.QuadPart-=(now-packet->at)*10000;
  filetime.dwLowDateTime=ticks.LowPart;filetime.dwHighDateTime=ticks.HighPart;SYSTEMTIME time;FileTimeToSystemTime(&filetime,&time);char stamp[40];sprintf_s(stamp,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond,time.wMilliseconds);
  GraphRaw entry{std::move(f),stamp,p->graphLoss.load()};
  const auto bytes=sizeof(GraphRaw)+entry.fact.path.size();
  if(p->graphBytes.fetch_add(bytes)+bytes>8*1024*1024){p->graphBytes-=bytes;++p->graphLoss;return 1;}
  if(!p->graphRaw.push(std::move(entry))){p->graphBytes-=bytes;++p->graphLoss;return 1;}return 0;
 }catch(...){++p->graphLoss;return -1;}
}
