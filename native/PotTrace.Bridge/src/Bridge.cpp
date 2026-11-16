#include <windows.h>
#include <sddl.h>
#include <objbase.h>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include "../include/BridgeSupport.h"

namespace {
using namespace pottrace;
std::wstring wide(std::string_view s){int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);std::wstring r(n,L'\0');if(n)MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n);return r;}
std::string utf8(std::wstring_view s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::string guid(){GUID g;CoCreateGuid(&g);wchar_t value[40];StringFromGUID2(g,value,40);return utf8(std::wstring_view(value+1,36));}
std::string utc(){SYSTEMTIME t;GetSystemTime(&t);char s[40];sprintf_s(s,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds);return s;}
std::wstring sid(){HANDLE token=nullptr;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))return {};DWORD n=0;GetTokenInformation(token,TokenUser,nullptr,0,&n);std::vector<char> b(n);GetTokenInformation(token,TokenUser,b.data(),n,&n);CloseHandle(token);LPWSTR text=nullptr;if(!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(b.data())->User.Sid,&text))return {};std::wstring r=text;LocalFree(text);return r;}
struct Raw {std::string text;ULONGLONG at;std::string stamp;};
struct Out {long long seq;std::string json;};
struct State {
 std::atomic<bool> stop=false,suppressed=false;std::mutex rawGate,outGate;
 std::deque<Raw> raw;size_t rawBytes=0;bool rawGap=false;
 std::deque<Out> outgoing;size_t outBytes=0;bool outGap=false;
 std::wstring folder,exe,data,channel,pipe;std::string instance=guid(),epoch,path;
 long long seq=0,pos=0,duration=0,ratePos=0;ULONGLONG last=0,rateAt=0;double rate=1;int warmSamples=0;std::string playState="Unknown";bool active=false;HWND hwnd=nullptr;
};
State* state=nullptr;std::mutex initGate;
BOOL CALLBACK findWindow(HWND h,LPARAM arg){DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(pid!=GetCurrentProcessId())return TRUE;wchar_t cls[80];GetClassNameW(h,cls,80);if(wcscmp(cls,L"PotPlayer64")==0){*reinterpret_cast<HWND*>(arg)=h;return FALSE;}return TRUE;}
bool query(HWND h,WPARAM key,long long& value){DWORD_PTR result=0;if(!h||!SendMessageTimeoutW(h,WM_USER,key,0,SMTO_ABORTIFHUNG|SMTO_BLOCK,80,&result))return false;value=static_cast<LONG_PTR>(result);return true;}
void emit(State& s,std::string kind,ULONGLONG now,const std::string& stamp,bool gap=false){
 if(s.suppressed)return;
 std::lock_guard lock(s.outGate);
 if(s.outGap){gap=true;s.outGap=false;}
 long long seq=++s.seq;
 std::string j="{\"protocolVersion\":1,\"instanceId\":"+quote(s.instance)+",\"mediaEpoch\":"+(s.epoch.empty()?"null":quote(s.epoch))+",\"sequence\":"+std::to_string(seq)+",\"kind\":"+quote(kind)+",\"observedAtUtc\":"+quote(stamp)+",\"monotonicMs\":"+std::to_string(now)+",\"sourcePath\":"+(s.path.empty()?"null":quote(s.path))+",\"positionMs\":"+std::to_string(std::max(0LL,s.pos))+",\"durationMs\":"+std::to_string(std::max(0LL,s.duration))+",\"state\":"+quote(s.playState)+",\"rate\":"+std::to_string(s.rate)+",\"quality\":"+quote(gap?"Gap":"Estimated")+"}";
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
 if(!s.hwnd||!IsWindow(s.hwnd))EnumWindows(findWindow,reinterpret_cast<LPARAM>(&s.hwnd));
 long long status=0,pos=0,total=0;
 if(!query(s.hwnd,0x5006,status)||!query(s.hwnd,0x5004,pos)||!query(s.hwnd,0x5002,total)){
  if(s.active)emit(s,"Gap",now,utc(),true);s.last=now;return;
 }
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
 auto a=raw.text.find('\t'),b=raw.text.find('\t',a+1),c=raw.text.find('\t',b+1);if(a==std::string::npos||b==std::string::npos||c==std::string::npos)return;
 auto kind=raw.text.substr(0,a),path=raw.text.substr(c+1);long long total=0;
 try{total=std::stoll(raw.text.substr(a+1,b-a-1));}catch(...){return;}
 ULONGLONG now=std::max(raw.at,s.last);
 if(kind=="Open"){
  if(s.active){emit(s,"MediaClosed",now,raw.stamp);s.active=false;}
  s.path=path;s.epoch=guid();s.rate=1;s.pos=0;s.last=now;s.playState="Unknown";emit(s,"MediaOpened",now,raw.stamp);return;
 }
 if(kind=="Start"){
  if(s.active&&s.path==path){s.duration=total;return;}
  if(s.epoch.empty()||s.path!=path){if(s.active)emit(s,"MediaClosed",now,raw.stamp);s.epoch=guid();}
  s.path=path;s.duration=total;s.pos=0;query(s.hwnd,0x5004,s.pos);s.playState="Playing";s.active=true;s.last=now;s.rateAt=now;s.ratePos=s.pos;s.warmSamples=0;
  emit(s,"MediaOpened",now,raw.stamp);return;
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
void observe(State* p){auto& s=*p;EnumWindows(findWindow,reinterpret_cast<LPARAM>(&s.hwnd));emit(s,"InstanceStarted",GetTickCount64(),utc());ULONGLONG next=0;bool wasSuppressed=false;
 while(!s.stop){
  std::deque<Raw> batch;bool gap=false;{std::lock_guard lock(s.rawGate);batch.swap(s.raw);s.rawBytes=0;gap=s.rawGap;s.rawGap=false;}
  if(gap)emit(s,"Gap",GetTickCount64(),utc(),true);
  for(const auto& raw:batch)callback(s,raw);
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
bool launch(State& s){if(s.exe.empty())return false;std::wstring args=L"\""+s.exe+L"\" --background --channel "+s.channel;if(!s.data.empty())args+=L" --data-dir \""+s.data+L"\"";STARTUPINFOW si{};si.cb=sizeof(si);si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;PROCESS_INFORMATION pi{};
 if(!CreateProcessW(s.exe.c_str(),args.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return false;CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return true;}
void transport(State* p){auto& s=*p;auto file=s.folder+L"\\pottrace-bridge.ini";s.exe=ini(file,L"HostPath");s.data=ini(file,L"DataDirectory");s.channel=ini(file,L"Channel",L"main");
 if(s.channel.empty()||s.channel.find_first_not_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-")!=std::wstring::npos)return;
 DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);s.pipe=L"\\\\.\\pipe\\PotTrace."+sid()+L"."+std::to_wstring(session)+L"."+s.channel;
 std::deque<ULONGLONG> launches;ULONGLONG retryAt=0;
 while(!s.stop){
  HANDLE pipe=CreateFileW(s.pipe.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED|SECURITY_SQOS_PRESENT|SECURITY_IDENTIFICATION,nullptr);
  if(pipe==INVALID_HANDLE_VALUE){auto now=GetTickCount64();while(!launches.empty()&&now-launches.front()>=60000)launches.pop_front();if(!s.suppressed&&now>=retryAt&&launches.size()<3){launch(s);launches.push_back(now);retryAt=now+(launches.size()==1?1000:launches.size()==2?5000:15000);}Sleep(250);continue;}
  std::string response;bool ok=send(pipe,"{\"version\":1,\"type\":\"Hello\",\"instanceId\":"+quote(s.instance)+",\"processId\":"+std::to_string(GetCurrentProcessId())+"}")&&receive(pipe,response);
  if(ok)s.suppressed=false;
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
 // CreateThread does not wait for the thread's CRT startup handshake. Waiting
 // here can stall the host's extension dispatch while it holds internal locks.
 HANDLE observer=CreateThread(nullptr,0,observeEntry,state,0,nullptr);
 HANDLE sender=CreateThread(nullptr,0,transportEntry,state,0,nullptr);
 if(observer)CloseHandle(observer);if(sender)CloseHandle(sender);
 if(!observer||!sender){state->stop=true;return -3;}return 0;
}
extern "C" __declspec(dllexport) int32_t PotTrace_Enqueue(const uint8_t* bytes,uint32_t length){
 auto p=state;if(!p||p->stop||!bytes||length>262144)return -1;
 std::lock_guard lock(p->rawGate);if(p->raw.size()>=4096||p->rawBytes+length>8*1024*1024){p->rawGap=true;return 1;}
 p->raw.push_back({std::string(reinterpret_cast<const char*>(bytes),length),GetTickCount64(),utc()});p->rawBytes+=length;return 0;
}
extern "C" __declspec(dllexport) int32_t PotTrace_Shutdown(){if(state)state->stop=true;return 0;}
