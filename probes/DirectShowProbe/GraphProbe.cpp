// Isolated feasibility probe. Never installed or registered system-wide.
#include <streams.h>
#include <evr.h>
#include <mfidl.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <wrl/client.h>
#include "../../native/PotTrace.Bridge/include/BridgeSupport.h"
#include "../../native/PotTrace.Bridge/include/ObservationFence.h"
#include "../../native/PotTrace.Bridge/include/TerminalDelivery.h"
#include "../../native/PotTrace.Bridge/include/MpscQueue.h"
#include "../../native/PotTrace.Bridge/include/EffectiveRate.h"

// {284611F2-2EB1-44A2-9C75-803C2C144D71}
const CLSID CLSID_PotTraceGraphProbe={0x284611f2,0x2eb1,0x44a2,{0x9c,0x75,0x80,0x3c,0x2c,0x14,0x4d,0x71}};
namespace {
using Microsoft::WRL::ComPtr;
std::string utf8(std::wstring_view s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string r(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),r.data(),n,nullptr,nullptr);return r;}
std::string id(){GUID g{};CoCreateGuid(&g);wchar_t s[40]{};StringFromGUID2(g,s,40);return utf8(s);}
std::string filterName(IBaseFilter* filter){FILTER_INFO info{};if(FAILED(filter->QueryFilterInfo(&info)))return "<unknown>";ComPtr<IFilterGraph> graph;graph.Attach(info.pGraph);return utf8(info.achName);}
std::string currentFile(IFileSourceFilter* source,HRESULT& result){LPOLESTR path=nullptr;result=source->GetCurFile(&path,nullptr);std::unique_ptr<OLECHAR,decltype(&CoTaskMemFree)> owned(path,CoTaskMemFree);return SUCCEEDED(result)&&path?utf8(path):std::string();}
// Read-only capability experiment: never change a clock or its rate.
std::string rateCapability(IUnknown* object){
 ComPtr<IMFRateControl> control;float directRate=0,serviceRate=0,clockRate=0;BOOL thin=FALSE;
 HRESULT directResult=object?object->QueryInterface(IID_PPV_ARGS(control.GetAddressOf())):E_POINTER;
 if(SUCCEEDED(directResult))directResult=control->GetRate(&thin,&directRate);
 ComPtr<IMFGetService> service;HRESULT serviceResult=object?object->QueryInterface(IID_PPV_ARGS(service.GetAddressOf())):E_POINTER;
 if(SUCCEEDED(serviceResult)){control.Reset();serviceResult=service->GetService(MF_RATE_CONTROL_SERVICE,IID_PPV_ARGS(control.GetAddressOf()));if(SUCCEEDED(serviceResult))serviceResult=control->GetRate(&thin,&serviceRate);}
 ComPtr<IMFMediaSink> sink;HRESULT clockResult=object?object->QueryInterface(IID_PPV_ARGS(sink.GetAddressOf())):E_POINTER;
 if(SUCCEEDED(clockResult)){ComPtr<IMFPresentationClock> clock;clockResult=sink->GetPresentationClock(clock.GetAddressOf());if(SUCCEEDED(clockResult)&&clock){control.Reset();clockResult=clock.As(&control);if(SUCCEEDED(clockResult))clockResult=control->GetRate(&thin,&clockRate);}}
 return "{\"directResult\":"+std::to_string(directResult)+",\"directRate\":"+std::to_string(directRate)+",\"serviceResult\":"+std::to_string(serviceResult)+",\"serviceRate\":"+std::to_string(serviceRate)+",\"clockResult\":"+std::to_string(clockResult)+",\"clockRate\":"+std::to_string(clockRate)+"}";
}
struct Log {
 pottrace::MpscQueue<std::string,4096> queue;std::atomic<size_t> bytes{0};std::atomic<unsigned long> lost=0;
 std::atomic<uint64_t> submitted{0},completed{0};std::wstring file;
 static DWORD WINAPI worker(void* value){
  auto& s=*static_cast<Log*>(value);
  for(;;){
   std::deque<std::string> batch;std::string item;while(batch.size()<256&&s.queue.pop(item)){s.bytes-=item.size();batch.push_back(std::move(item));}
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
  try {auto size=value.size();if(bytes.fetch_add(size)+size>8*1024*1024){bytes-=size;lost++;return;}if(!queue.push(std::move(value))){bytes-=size;lost++;return;}submitted++;}
  catch(...){lost++;}
 }
 bool flush(DWORD timeout) noexcept {
  const auto target=submitted.load(),deadline=GetTickCount64()+timeout;
  while(completed.load()<target){if(GetTickCount64()>=deadline)return false;Sleep(1);}return true;
 }
};
Log& logger(){static Log* s=new Log;return *s;}
#include "GraphSink.h"
#include "RendererWatch.h"
#include "ConnectedRenderer.h"

class Probe final : public CTransInPlaceFilter {
 std::string instance=id();std::atomic<bool> first{true};
 std::atomic<uintptr_t> graphId{0};std::atomic<int> observedState{0};
 std::atomic<bool> inputConnected{false},outputConnected{false};
 std::atomic<bool> everConnected{false};
 std::atomic<uint64_t> activeBinding{0};
 std::shared_ptr<RendererWatch> watch;
 std::shared_ptr<pottrace::ObservationFence> fence=std::make_shared<pottrace::ObservationFence>();
 void trace(const char* kind,HRESULT result=S_OK,REFERENCE_TIME mediaStart=0,REFERENCE_TIME mediaStop=0,double rate=1,bool inspect=false) noexcept {
  const auto at=GetTickCount64();
  try {
   // Only application/lifecycle boundaries enumerate the graph. The streaming
   // path records timestamps only, never calls back into the graph or the disk.
   std::vector<std::string> sources;std::string filters="[",videoWindows="[",windowCapabilities="[",pinCapabilities="[",upstreamName,upstreamSource;bool uncertain=false,hasWindow=false,hasPin=false;HRESULT enumEnd=S_OK,upstreamResult=E_NOINTERFACE,graphVideoResult=E_NOINTERFACE,graphOwnerResult=E_NOINTERFACE;ULONG enumFetched=0;
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
   ComPtr<IBaseFilter> observedRenderer;HWND observedWindow=nullptr;bool multipleRenderers=false;
   auto windowRecord=[&](HWND window,const std::string& name,IBaseFilter* renderer=nullptr){
    if(!window)return;DWORD pid=0;GetWindowThreadProcessId(window,&pid);if(hasWindow)videoWindows+=',';hasWindow=true;
    videoWindows+="{\"filter\":"+pottrace::quote(name)+",\"hwnd\":"+std::to_string(reinterpret_cast<uintptr_t>(window))+",\"root\":"+std::to_string(reinterpret_cast<uintptr_t>(GetAncestor(window,GA_ROOT)))+",\"pid\":"+std::to_string(pid)+",\"visible\":"+(IsWindowVisible(window)?"true":"false")+"}";
    wchar_t rootClass[80]{};GetClassNameW(GetAncestor(window,GA_ROOT),rootClass,80);
    if(renderer&&pid==GetCurrentProcessId()&&wcscmp(rootClass,L"PotPlayer64")==0){if(observedWindow&&observedWindow!=window)multipleRenderers=true;observedWindow=window;observedRenderer=renderer;}
   };
   if(inspect&&m_pGraph){
    ComPtr<IVideoWindow> graphVideo;graphVideoResult=m_pGraph->QueryInterface(IID_PPV_ARGS(graphVideo.GetAddressOf()));
    if(SUCCEEDED(graphVideoResult)){OAHWND owner=0;graphOwnerResult=graphVideo->get_Owner(&owner);if(SUCCEEDED(graphOwnerResult))windowRecord(reinterpret_cast<HWND>(owner),"<graph IVideoWindow>");}
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
       OAHWND owner=0;ownerResult=video->get_Owner(&owner);if(SUCCEEDED(ownerResult))windowRecord(reinterpret_cast<HWND>(owner),name,filter.Get());
      }
      ComPtr<IMFGetService> service;
      auto serviceResult=filter.As(&service);
      if(SUCCEEDED(serviceResult)){
       ComPtr<IMFVideoDisplayControl> display;
       displayResult=service->GetService(MR_VIDEO_RENDER_SERVICE,IID_PPV_ARGS(display.GetAddressOf()));
       if(SUCCEEDED(displayResult)){
        HWND window=nullptr;if(SUCCEEDED(display->GetVideoWindow(&window)))windowRecord(window,name,filter.Get());
       }
      }
      ComPtr<IMFVideoDisplayControl> directDisplay;auto directDisplayResult=filter.As(&directDisplay);
      ComPtr<IMediaSeeking> filterSeeking;HRESULT filterRateResult=E_NOINTERFACE;double filterRate=0;
      if(SUCCEEDED(filter.As(&filterSeeking)))filterRateResult=filterSeeking->GetRate(&filterRate);
      ComPtr<IMediaPosition> filterPosition;HRESULT positionRateResult=E_NOINTERFACE;double positionRate=0;
      if(SUCCEEDED(filter.As(&filterPosition)))positionRateResult=filterPosition->get_Rate(&positionRate);
      auto mfRate=rateCapability(filter.Get());ComPtr<IReferenceClock> reference;
      std::string referenceRate="null";if(SUCCEEDED(filter->GetSyncSource(reference.GetAddressOf()))&&reference)referenceRate=rateCapability(reference.Get());
      if(SUCCEEDED(directDisplayResult)){HWND window=nullptr;if(SUCCEEDED(directDisplay->GetVideoWindow(&window)))windowRecord(window,name+" directDisplay",filter.Get());}
      FILTER_STATE filterState=State_Stopped;HRESULT stateResult=E_NOTIMPL;int framesDrawn=-1;HRESULT framesResult=E_NOINTERFACE;
      if(name.find("Renderer")!=std::string::npos){stateResult=filter->GetState(0,&filterState);ComPtr<IQualProp> quality;if(SUCCEEDED(filter->QueryInterface(IID_IQualProp,reinterpret_cast<void**>(quality.GetAddressOf()))))framesResult=quality->get_FramesDrawn(&framesDrawn);}
      ComPtr<IEnumPins> pins;
      if(SUCCEEDED(filter->EnumPins(pins.GetAddressOf()))){
       ComPtr<IPin> pin;ULONG fetched=0;
       for(int p=0;p<32&&pins->Next(1,pin.ReleaseAndGetAddressOf(),&fetched)==S_OK;++p){
        PIN_DIRECTION direction{};if(FAILED(pin->QueryDirection(&direction))||direction!=PINDIR_INPUT)continue;
        ComPtr<IOverlay> overlay;auto overlayResult=pin.As(&overlay);HRESULT handleResult=E_NOINTERFACE;HWND overlayWindow=nullptr;
        if(SUCCEEDED(overlayResult)){handleResult=overlay->GetWindowHandle(&overlayWindow);if(SUCCEEDED(handleResult))windowRecord(overlayWindow,name+" input IOverlay",filter.Get());}
        if(hasPin)pinCapabilities+=',';hasPin=true;
        pinCapabilities+="{\"filter\":"+pottrace::quote(name)+",\"overlayResult\":"+std::to_string(overlayResult)+",\"handleResult\":"+std::to_string(handleResult)+",\"hwnd\":"+std::to_string(reinterpret_cast<uintptr_t>(overlayWindow))+"}";
       }
      }
      if(count)windowCapabilities+=',';
      windowCapabilities+="{\"filter\":"+pottrace::quote(name)+",\"videoInterfaceResult\":"+std::to_string(videoResult)+",\"ownerResult\":"+std::to_string(ownerResult)+",\"serviceResult\":"+std::to_string(serviceResult)+",\"displayResult\":"+std::to_string(displayResult)+",\"directDisplayResult\":"+std::to_string(directDisplayResult)+",\"stateResult\":"+std::to_string(stateResult)+",\"filterState\":"+std::to_string(filterState)+",\"framesResult\":"+std::to_string(framesResult)+",\"framesDrawn\":"+std::to_string(framesDrawn)+",\"filterRateResult\":"+std::to_string(filterRateResult)+",\"filterRate\":"+std::to_string(filterRate)+",\"positionRateResult\":"+std::to_string(positionRateResult)+",\"positionRate\":"+std::to_string(positionRate)+"}";
      // Append the independent Media Foundation/clock rate capability results.
      windowCapabilities.pop_back();windowCapabilities+=",\"mfRate\":"+mfRate+",\"referenceRate\":"+referenceRate+"}";
      ++count;
     }
     if(count==128)uncertain=true;
    }else uncertain=true;
   }
   ConnectedRenderer binding;
   if(inspect&&m_pOutput&&outputConnected)binding=followVideoOutput(m_pOutput);
   if(binding.result==S_OK){
    // Only this independently traversed renderer can qualify the observer.
    observedRenderer.Reset();observedWindow=nullptr;multipleRenderers=false;
    windowRecord(binding.window,"<connected video renderer>",binding.filter.Get());
   }
   if(inspect&&!watch&&binding.result==S_OK&&observedRenderer&&!multipleRenderers&&inputConnected&&outputConnected&&m_pGraph&&strcmp(kind,"PauseBefore")==0){
    wchar_t enabled[8]{};
    if(GetEnvironmentVariableW(L"POTTRACE_DSHOW_RENDERER_WATCH",enabled,8)&&wcscmp(enabled,L"1")==0){
     auto value=std::make_shared<RendererWatch>();ComPtr<IPin> peer;PIN_INFO pin{};
     if(SUCCEEDED(m_pInput->ConnectedTo(peer.GetAddressOf()))&&SUCCEEDED(peer->QueryPinInfo(&pin))&&pin.pFilter){
      ComPtr<IBaseFilter> owner;owner.Attach(pin.pFilter);
      if(SUCCEEDED(owner.As(&value->source))&&SUCCEEDED(m_pGraph->QueryInterface(IID_PPV_ARGS(value->seeking.GetAddressOf())))){
       value->renderer=observedRenderer;observedRenderer.As(&value->rendererSeeking);value->window=observedWindow;value->instance=instance;value->graph=graphId.load();value->fence=std::atomic_load(&fence);
       value->binding=graphSink().next++;
       if(value->start()){activeBinding=value->binding;watch=std::move(value);}
       else {value->fence->retire();value->retire();std::atomic_store(&fence,std::make_shared<pottrace::ObservationFence>());}
      }
     }
    }
   }
   std::string connectedChain="[";for(size_t i=0;i<binding.chain.size();++i){if(i)connectedChain+=',';connectedChain+=pottrace::quote(binding.chain[i]);}connectedChain+=']';
   filters+=']';videoWindows+=']';windowCapabilities+=']';pinCapabilities+=']';std::sort(sources.begin(),sources.end());sources.erase(std::unique(sources.begin(),sources.end()),sources.end());
   std::string paths="[";for(size_t i=0;i<sources.size();++i){if(i)paths+=',';paths+=pottrace::quote(sources[i]);}paths+=']';
   const auto inspectedAt=GetTickCount64();auto& log=logger();
   std::string line="{\"at\":"+std::to_string(at)+",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"thread\":"+std::to_string(GetCurrentThreadId())+
    ",\"instance\":"+pottrace::quote(instance)+",\"graph\":"+std::to_string(graphId.load())+
    ",\"event\":"+pottrace::quote(kind)+",\"state\":"+std::to_string(observedState.load())+",\"result\":"+std::to_string(result)+
    ",\"inputConnected\":"+(inputConnected?"true":"false")+",\"outputConnected\":"+(outputConnected?"true":"false")+
    ",\"identityInspected\":"+(inspect?"true":"false")+",\"inspectionStartedMs\":"+(inspect?std::to_string(at):"null")+",\"inspectionEndedMs\":"+(inspect?std::to_string(inspectedAt):"null")+",\"enumEnd\":"+std::to_string(enumEnd)+",\"enumFetched\":"+std::to_string(enumFetched)+",\"uncertain\":"+(uncertain?"true":"false")+",\"sources\":"+paths+",\"filters\":"+filters+
    ",\"upstreamName\":"+pottrace::quote(upstreamName)+",\"upstreamSource\":"+pottrace::quote(upstreamSource)+",\"upstreamResult\":"+std::to_string(upstreamResult)+",\"videoWindows\":"+videoWindows+",\"windowCapabilities\":"+windowCapabilities+",\"pinCapabilities\":"+pinCapabilities+",\"graphVideoResult\":"+std::to_string(graphVideoResult)+",\"graphOwnerResult\":"+std::to_string(graphOwnerResult)+",\"connectedRendererResult\":"+std::to_string(binding.result)+",\"connectedRendererWindow\":"+std::to_string(reinterpret_cast<uintptr_t>(binding.window))+",\"connectedVideoChain\":"+connectedChain+",\"mediaStart100ns\":"+std::to_string(mediaStart)+",\"mediaStop100ns\":"+std::to_string(mediaStop)+",\"rate\":"+std::to_string(rate)+",\"lost\":"+std::to_string(log.lost.load())+"}\n";
   log.put(std::move(line));
  }catch(...){logger().lost++;}
 }
public:
 Probe(LPUNKNOWN outer,HRESULT* hr):CTransInPlaceFilter(NAME("PotTrace isolated graph probe"),outer,CLSID_PotTraceGraphProbe,hr,false){logger();graphSink();trace("Construct");}
 void retireWatch(){std::atomic_load(&fence)->retire();activeBinding=0;if(watch){watch->retire();watch.reset();}}
 ~Probe() override {retireWatch();trace("Destroy");if(everConnected)logger().flush(200);}
 static CUnknown* WINAPI Create(LPUNKNOWN outer,HRESULT* hr){try{return new Probe(outer,hr);}catch(...){if(hr)*hr=E_OUTOFMEMORY;return nullptr;}}
 HRESULT CheckInputType(const CMediaType* type) override {return type&&type->majortype==MEDIATYPE_Video?S_OK:VFW_E_TYPE_NOT_ACCEPTED;}
 HRESULT Transform(IMediaSample* sample) override {
  if(first.exchange(false)){REFERENCE_TIME start=0,stop=0;auto hr=sample->GetTime(&start,&stop);trace("FirstSample",hr,start,stop);}
  return S_OK;
 }
 STDMETHODIMP JoinFilterGraph(IFilterGraph* graph,LPCWSTR name) override {
  if(!graph){retireWatch();trace("Leave",S_OK,0,0,1,true);}else std::atomic_store(&fence,std::make_shared<pottrace::ObservationFence>());
  auto hr=CTransInPlaceFilter::JoinFilterGraph(graph,name);graphId=reinterpret_cast<uintptr_t>(graph);if(graph)trace("Join",hr,0,0,1,true);return hr;
 }
 STDMETHODIMP Pause() override {auto lifetime=std::atomic_load(&fence);auto change=lifetime->changing();trace("PauseBefore",S_OK,0,0,1,true);auto hr=CTransInPlaceFilter::Pause();trace("PauseAfter",hr,0,0,1,true);return hr;}
 STDMETHODIMP Run(REFERENCE_TIME start) override {auto lifetime=std::atomic_load(&fence);auto change=lifetime->changing();trace("RunBefore",S_OK,start,0,1,true);auto hr=CTransInPlaceFilter::Run(start);trace("RunAfter",hr,start,0,1,true);return hr;}
 STDMETHODIMP Stop() override {retireWatch();trace("StopBefore",S_OK,0,0,1,true);auto hr=CTransInPlaceFilter::Stop();trace("StopAfter",hr,0,0,1,true);std::atomic_store(&fence,std::make_shared<pottrace::ObservationFence>());return hr;}
 HRESULT NewSegment(REFERENCE_TIME start,REFERENCE_TIME stop,double rate) override {auto lifetime=std::atomic_load(&fence);auto change=lifetime->changing();auto at=GetTickCount64();graphSink().send(2,activeBinding.load(),lifetime->capture().generation,at,at,-1,{},start/10000);first=true;trace("NewSegment",S_OK,start,stop,rate);return CTransInPlaceFilter::NewSegment(start,stop,rate);}
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
