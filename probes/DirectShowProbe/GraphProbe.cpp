// Isolated feasibility probe. Never installed or registered system-wide.
#include <streams.h>
#include <evr.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <wrl/client.h>
#include "../../native/PotTrace.Bridge/include/BridgeSupport.h"

// {284611F2-2EB1-44A2-9C75-803C2C144D71}
const CLSID CLSID_PotTraceGraphProbe={0x284611f2,0x2eb1,0x44a2,{0x9c,0x75,0x80,0x3c,0x2c,0x14,0x4d,0x71}};
namespace {
using Microsoft::WRL::ComPtr;
std::string utf8(std::wstring_view s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::string id(){GUID g{};CoCreateGuid(&g);wchar_t s[40]{};StringFromGUID2(g,s,40);return utf8(s);}
std::string filterName(IBaseFilter* filter){FILTER_INFO info{};if(FAILED(filter->QueryFilterInfo(&info)))return "<unknown>";ComPtr<IFilterGraph> graph;graph.Attach(info.pGraph);return utf8(info.achName);}
std::string currentFile(IFileSourceFilter* source,HRESULT& result){LPOLESTR path=nullptr;result=source->GetCurFile(&path,nullptr);std::unique_ptr<OLECHAR,decltype(&CoTaskMemFree)> owned(path,CoTaskMemFree);return SUCCEEDED(result)&&path?utf8(path):std::string();}
struct Log {
 std::mutex gate;std::deque<std::string> queue;size_t bytes=0;std::atomic<unsigned long> lost=0;
 std::atomic<uint64_t> submitted{0},completed{0};std::wstring file;
 static DWORD WINAPI worker(void* value){
  auto& s=*static_cast<Log*>(value);
  for(;;){
   std::deque<std::string> batch;{std::lock_guard lock(s.gate);batch.swap(s.queue);s.bytes=0;}
   if(!batch.empty()){
    HANDLE f=CreateFileW(s.file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f!=INVALID_HANDLE_VALUE){for(auto& line:batch){DWORD written=0;if(!WriteFile(f,line.data(),static_cast<DWORD>(line.size()),&written,nullptr)||written!=line.size())s.lost++;}CloseHandle(f);}
    else s.lost+=static_cast<unsigned long>(batch.size());
    s.completed+=batch.size();
   }
   Sleep(20);
  }
 }
 Log(){
  wchar_t value[32768];DWORD n=GetEnvironmentVariableW(L"POTTRACE_DSHOW_TRACE",value,32768);if(!n||n>=32768)return;
  file.assign(value,n);
  // Probe lifetime is deliberately the player process; no code can be unloaded
  // while the asynchronous writer runs. Production lifetime design is separate.
  HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&worker),&pinned)){file.clear();return;}
  HANDLE thread=CreateThread(nullptr,0,worker,this,0,nullptr);if(thread)CloseHandle(thread);else file.clear();
 }
 void put(std::string value) noexcept {
  if(file.empty())return;
  try {std::unique_lock lock(gate,std::try_to_lock);if(!lock||queue.size()>=4096||bytes+value.size()>8*1024*1024){lost++;return;}auto size=value.size();queue.push_back(std::move(value));bytes+=size;submitted++;}
  catch(...){lost++;}
 }
 bool flush(DWORD timeout) noexcept {
  const auto target=submitted.load(),deadline=GetTickCount64()+timeout;
  while(completed.load()<target){if(GetTickCount64()>=deadline)return false;Sleep(1);}return true;
 }
};
Log& logger(){static Log* s=new Log;return *s;}

class Probe final : public CTransInPlaceFilter {
 std::string instance=id();std::atomic<bool> first{true};
 std::atomic<uintptr_t> graphId{0};std::atomic<int> observedState{0};
 std::atomic<bool> inputConnected{false},outputConnected{false};
 std::atomic<bool> everConnected{false};
 void trace(const char* kind,HRESULT result=S_OK,REFERENCE_TIME mediaStart=0,REFERENCE_TIME mediaStop=0,double rate=1,bool inspect=false) noexcept {
  const auto at=GetTickCount64();
  try {
   // Only application/lifecycle boundaries enumerate the graph. The streaming
   // path records timestamps only, never calls back into the graph or the disk.
   std::vector<std::string> sources;std::string filters="[",videoWindows="[",windowCapabilities="[",upstreamName,upstreamSource;bool uncertain=false,hasWindow=false;HRESULT enumEnd=S_OK,upstreamResult=E_NOINTERFACE;ULONG enumFetched=0;
   if(inspect){observedState=m_State;inputConnected=m_pInput&&m_pInput->IsConnected();outputConnected=m_pOutput&&m_pOutput->IsConnected();if(inputConnected&&outputConnected)everConnected=true;}
   if(inspect&&m_pInput){
    ComPtr<IPin> peer;
    if(SUCCEEDED(m_pInput->ConnectedTo(peer.GetAddressOf()))&&peer){
     PIN_INFO pin{};
     if(SUCCEEDED(peer->QueryPinInfo(&pin))&&pin.pFilter){
      ComPtr<IBaseFilter> owner;owner.Attach(pin.pFilter);upstreamName=filterName(owner.Get());
      ComPtr<IFileSourceFilter> source;upstreamResult=owner.As(&source);
      if(SUCCEEDED(upstreamResult)&&source)upstreamSource=currentFile(source.Get(),upstreamResult);
     }
    }
   }
   auto windowRecord=[&](HWND window,const std::string& name){
    if(!window)return;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(hasWindow)videoWindows+=',';hasWindow=true;
    videoWindows+="{\"filter\":"+pottrace::quote(name)+",\"hwnd\":"+std::to_string(reinterpret_cast<uintptr_t>(window))+",\"root\":"+std::to_string(reinterpret_cast<uintptr_t>(GetAncestor(window,GA_ROOT)))+",\"pid\":"+std::to_string(pid)+",\"visible\":"+(IsWindowVisible(window)?"true":"false")+"}";
   };
   if(inspect&&m_pGraph){
    ComPtr<IEnumFilters> enumeration;HRESULT e=m_pGraph->EnumFilters(enumeration.GetAddressOf());
    if(SUCCEEDED(e)&&enumeration){
     ComPtr<IBaseFilter> filter;int count=0;
     while(count<128){
      enumFetched=0;auto next=enumeration->Next(1,filter.ReleaseAndGetAddressOf(),&enumFetched);enumEnd=next;
      if(next!=S_OK){if(next!=S_FALSE)uncertain=true;break;}
      if(!filter){uncertain=true;break;}
      const auto name=filterName(filter.Get());
      if(count)filters+=',';filters+=pottrace::quote(name);
      ComPtr<IFileSourceFilter> source;
      if(SUCCEEDED(filter.As(&source))){
       HRESULT hr=S_OK;auto path=currentFile(source.Get(),hr);
       if(SUCCEEDED(hr)&&!path.empty()){sources.push_back(std::move(path));}else uncertain=true;
      }
      ComPtr<IVideoWindow> video;
      auto videoResult=filter.As(&video);HRESULT ownerResult=E_NOINTERFACE,displayResult=E_NOINTERFACE;
      if(SUCCEEDED(videoResult)){
       OAHWND owner=0;ownerResult=video->get_Owner(&owner);if(SUCCEEDED(ownerResult))windowRecord(reinterpret_cast<HWND>(owner),name);
      }
      ComPtr<IMFGetService> service;
      auto serviceResult=filter.As(&service);
      if(SUCCEEDED(serviceResult)){
       ComPtr<IMFVideoDisplayControl> display;
       displayResult=service->GetService(MR_VIDEO_RENDER_SERVICE,IID_PPV_ARGS(display.GetAddressOf()));
       if(SUCCEEDED(displayResult)){
        HWND window=nullptr;if(SUCCEEDED(display->GetVideoWindow(&window)))windowRecord(window,name);
       }
      }
      if(count)windowCapabilities+=',';
      windowCapabilities+="{\"filter\":"+pottrace::quote(name)+",\"videoInterfaceResult\":"+std::to_string(videoResult)+",\"ownerResult\":"+std::to_string(ownerResult)+",\"serviceResult\":"+std::to_string(serviceResult)+",\"displayResult\":"+std::to_string(displayResult)+"}";
      ++count;
     }
     if(count==128)uncertain=true;
    }else uncertain=true;
   }
   filters+=']';videoWindows+=']';windowCapabilities+=']';std::sort(sources.begin(),sources.end());sources.erase(std::unique(sources.begin(),sources.end()),sources.end());
   std::string paths="[";for(size_t i=0;i<sources.size();++i){if(i)paths+=',';paths+=pottrace::quote(sources[i]);}paths+=']';
   const auto inspectedAt=GetTickCount64();auto& log=logger();
   std::string line="{\"at\":"+std::to_string(at)+",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"thread\":"+std::to_string(GetCurrentThreadId())+
    ",\"instance\":"+pottrace::quote(instance)+",\"graph\":"+std::to_string(graphId.load())+
    ",\"event\":"+pottrace::quote(kind)+",\"state\":"+std::to_string(observedState.load())+",\"result\":"+std::to_string(result)+
    ",\"inputConnected\":"+(inputConnected?"true":"false")+",\"outputConnected\":"+(outputConnected?"true":"false")+
    ",\"identityInspected\":"+(inspect?"true":"false")+",\"inspectionStartedMs\":"+(inspect?std::to_string(at):"null")+",\"inspectionEndedMs\":"+(inspect?std::to_string(inspectedAt):"null")+",\"enumEnd\":"+std::to_string(enumEnd)+",\"enumFetched\":"+std::to_string(enumFetched)+",\"uncertain\":"+(uncertain?"true":"false")+",\"sources\":"+paths+",\"filters\":"+filters+
    ",\"upstreamName\":"+pottrace::quote(upstreamName)+",\"upstreamSource\":"+pottrace::quote(upstreamSource)+",\"upstreamResult\":"+std::to_string(upstreamResult)+",\"videoWindows\":"+videoWindows+",\"windowCapabilities\":"+windowCapabilities+",\"mediaStart100ns\":"+std::to_string(mediaStart)+",\"mediaStop100ns\":"+std::to_string(mediaStop)+",\"rate\":"+std::to_string(rate)+",\"lost\":"+std::to_string(log.lost.load())+"}\n";
   log.put(std::move(line));
  }catch(...){logger().lost++;}
 }
public:
 Probe(LPUNKNOWN outer,HRESULT* hr):CTransInPlaceFilter(NAME("PotTrace isolated graph probe"),outer,CLSID_PotTraceGraphProbe,hr,false){logger();trace("Construct");}
 ~Probe() override {trace("Destroy");if(everConnected)logger().flush(200);}
 static CUnknown* WINAPI Create(LPUNKNOWN outer,HRESULT* hr){try{return new Probe(outer,hr);}catch(...){if(hr)*hr=E_OUTOFMEMORY;return nullptr;}}
 HRESULT CheckInputType(const CMediaType* type) override {return type&&type->majortype==MEDIATYPE_Video?S_OK:VFW_E_TYPE_NOT_ACCEPTED;}
 HRESULT Transform(IMediaSample* sample) override {
  if(first.exchange(false)){REFERENCE_TIME start=0,stop=0;auto hr=sample->GetTime(&start,&stop);trace("FirstSample",hr,start,stop);}
  return S_OK;
 }
 STDMETHODIMP JoinFilterGraph(IFilterGraph* graph,LPCWSTR name) override {
  if(!graph)trace("Leave",S_OK,0,0,1,true);
  auto hr=CTransInPlaceFilter::JoinFilterGraph(graph,name);graphId=reinterpret_cast<uintptr_t>(graph);if(graph)trace("Join",hr,0,0,1,true);return hr;
 }
 STDMETHODIMP Pause() override {trace("PauseBefore",S_OK,0,0,1,true);auto hr=CTransInPlaceFilter::Pause();trace("PauseAfter",hr,0,0,1,true);return hr;}
 STDMETHODIMP Run(REFERENCE_TIME start) override {trace("RunBefore",S_OK,start,0,1,true);auto hr=CTransInPlaceFilter::Run(start);trace("RunAfter",hr,start,0,1,true);return hr;}
 STDMETHODIMP Stop() override {trace("StopBefore",S_OK,0,0,1,true);auto hr=CTransInPlaceFilter::Stop();trace("StopAfter",hr,0,0,1,true);return hr;}
 HRESULT NewSegment(REFERENCE_TIME start,REFERENCE_TIME stop,double rate) override {first=true;trace("NewSegment",S_OK,start,stop,rate);return CTransInPlaceFilter::NewSegment(start,stop,rate);}
};
const AMOVIESETUP_MEDIATYPE media={&MEDIATYPE_Video,&MEDIASUBTYPE_NULL};
const AMOVIESETUP_PIN pins[]={{L"Input",FALSE,FALSE,FALSE,FALSE,&CLSID_NULL,nullptr,1,&media},{L"Output",FALSE,TRUE,FALSE,FALSE,&CLSID_NULL,nullptr,1,&media}};
const AMOVIESETUP_FILTER setup={&CLSID_PotTraceGraphProbe,L"PotTrace isolated graph probe",MERIT_DO_NOT_USE,2,pins};
}
CFactoryTemplate g_Templates[]={{L"PotTrace isolated graph probe",&CLSID_PotTraceGraphProbe,Probe::Create,nullptr,&setup}};
int g_cTemplates=sizeof(g_Templates)/sizeof(g_Templates[0]);
// The probe must be loaded privately by the isolated player; global registration
// is deliberately unavailable, including when a UI tries to invoke it.
STDAPI DllRegisterServer(){return E_ACCESSDENIED;}
STDAPI DllUnregisterServer(){return E_ACCESSDENIED;}
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE,DWORD,LPVOID);
BOOL APIENTRY DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){return DllEntryPoint(instance,reason,reserved);}
