#include <streams.h>
#include <evr.h>
#include <mfidl.h>
#include <wrl/client.h>
#include <algorithm>
#include <vector>
#include <memory>
#include "CollectorRuntime.h"
#include "CollectorConfig.h"
#include "GraphSink.h"
#include "EffectiveRate.h"
#include "TerminalDelivery.h"

// Private production class; deliberately distinct from the diagnostic AX.
const CLSID CLSID_PotTraceCollector={0xa5092f25,0x09c8,0x46eb,{0x91,0x38,0x63,0x8e,0x3c,0x0b,0x42,0x87}};
namespace {
using Microsoft::WRL::ComPtr;
using namespace pottrace;
std::string utf8(std::wstring_view s){int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);std::string result(n,'\0');if(n)WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),result.data(),n,nullptr,nullptr);return result;}
std::string filterName(IBaseFilter*){return {};}
std::string currentFile(IFileSourceFilter* source,HRESULT& hr){LPOLESTR file=nullptr;hr=source->GetCurFile(&file,nullptr);std::unique_ptr<OLECHAR,decltype(&CoTaskMemFree)> owned(file,CoTaskMemFree);return SUCCEEDED(hr)&&file?utf8(file):std::string();}
#include "ConnectedRenderer.h"
#include "RendererMeasurement.h"

struct Service {
 CollectorConfig config;CollectorSink sink;CaptureGate gate;CollectorRuntime runtime;
 std::filesystem::path healthFile,moduleFile;uint64_t healthAt=0;
 std::atomic<uint64_t> rejected{0};
 static DWORD WINAPI Manager(void* value){
  auto& s=*static_cast<Service*>(value);
  try{s.config=LoadCollectorConfig(s.moduleFile);auto folder=s.moduleFile.parent_path();if(ReadCollectorIni((folder/L"pottrace-collector.ini").wstring(),L"PotTrace",L"Diagnostics")==L"1")s.healthFile=folder/L"collector-health.json";}
  catch(...){s.gate.suppress();}
  for(;;){s.Refresh();Sleep(25);}
 }
 Service(){
  HMODULE module=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&Manager),&module))return;
  wchar_t file[32768]{};GetModuleFileNameW(module,file,32768);moduleFile=file;
  HANDLE manager=CreateThread(nullptr,0,Manager,this,0,nullptr);if(manager)CloseHandle(manager);else gate.suppress();
 }
 void Refresh() noexcept{
  try{
   auto policy=config.valid?ReadCapturePolicy(config.policy):CapturePolicy{};
   auto status=sink.State();if(status&4)gate.suppress();
   bool plugin=IsPlayerPluginEnabled(config)&&!(status&2);
   bool started=policy.allowed()&&plugin&&sink.Start(config);
   gate.update(policy,started&&plugin&&!(status&8));
   if(!gate.allowed())runtime.SuspendCapture(GetTickCount64());
   if(!healthFile.empty()&&GetTickCount64()-healthAt>=250){
    healthAt=GetTickCount64();auto h=runtime.Snapshot();auto temp=healthFile;temp+=L".tmp";
    {std::ofstream f(temp,std::ios::binary|std::ios::trunc);f<<"{\"at\":"<<healthAt<<",\"workers\":"<<h.workerCount<<",\"slots\":"<<h.occupiedSlots<<",\"failures\":"<<h.failures<<",\"rejected\":"<<rejected<<",\"lost\":"<<sink.lost<<",\"enabled\":"<<(gate.allowed()?"true":"false")<<",\"bridgeState\":"<<status<<"}";}
    MoveFileExW(temp.c_str(),healthFile.c_str(),MOVEFILE_REPLACE_EXISTING);
   }
  }catch(...){gate.update({},false);}
 }
};
Service& service(){static auto* value=new Service;return *value;}
bool retireBinding(uint64_t binding,uint64_t at) noexcept {
 auto& s=service();if(s.sink.Close(binding,at)==0)return true;++s.rejected;s.gate.suppress();return false;
}

struct Watch final : CollectorWork {
 ComPtr<IPin> upstream,downstream;ComPtr<IFilterGraph> graph;
 ComPtr<IBaseFilter> renderer;ComPtr<IFileSourceFilter> source;ComPtr<IMediaSeeking> seeking;
 HWND window=nullptr;std::atomic<bool> initialized{false};
 EffectiveRate effectiveRate;ClockContinuity continuity;ComPtr<IUnknown> heldClock;std::string ratePath;
 uint64_t rateRevision=0;TerminalDelivery terminal;
 Watch(IPin* inputPeer,IPin* outputPeer,IFilterGraph* owner):upstream(inputPeer),downstream(outputPeer),graph(owner){}
 void Admitted() noexcept override {control->onClose=retireBinding;if(service().gate.allowed())control->binding=service().sink.next++;}
 bool Bind(){
  PIN_INFO info{};if(FAILED(upstream->QueryPinInfo(&info))||!info.pFilter)return false;
  ComPtr<IBaseFilter> owner;owner.Attach(info.pFilter);ComPtr<IFileSourceFilter> candidate;
  if(FAILED(owner.As(&candidate)))return false;
  auto connected=followVideoPeer(downstream.Get());
  if(connected.result!=S_OK||!connected.filter||!connected.window)return false;
  DWORD pid=0;GetWindowThreadProcessId(connected.window,&pid);wchar_t cls[80]{};GetClassNameW(GetAncestor(connected.window,GA_ROOT),cls,80);
  if(pid!=GetCurrentProcessId()||wcscmp(cls,L"PotPlayer64")!=0)return false;
  ComPtr<IMediaSeeking> position;if(FAILED(graph.As(&position)))return false;
  source=candidate;renderer=connected.filter;window=connected.window;seeking=position;initialized=true;return true;
 }
 void Observe(bool metadata) override {
  auto& s=service();
  if(!s.gate.allowed()){
   if(!metadata){auto change=control->fence.changing();control->Close(GetTickCount64());}
   else {effectiveRate.reset();continuity.invalidate();ratePath.clear();heldClock.Reset();}
   return;
  }
  if(!metadata){
   if(!control->binding){auto ticket=control->fence.capture();control->fence.publish(ticket,[&]{if(s.gate.allowed()&&!control->binding)control->binding=s.sink.next++;});}
   if(control->retired||!control->binding)return;
   if(!initialized&&!Bind())return;
  }
  if(!initialized)return;auto ticket=control->fence.capture();if(!ticket.allowed)return;
  const auto binding=control->binding.load(),revision=s.gate.revision();if(!binding)return;
  auto r=ReadRendererMeasurement(renderer.Get(),source.Get(),seeking.Get(),window,metadata);
  EffectiveRate nextRate;EffectiveRateResult effective;
  if(metadata){
   nextRate=effectiveRate;
   if(rateRevision!=revision){nextRate.reset();continuity.invalidate();rateRevision=revision;}
   continuity.observe(reinterpret_cast<uintptr_t>(r.clockIdentity.Get()),r.sameClock&&SUCCEEDED(r.clockResult)&&r.clockFrequency.QuadPart>0);
   if(!r.consistent||r.after!=State_Running||!r.sameClock||FAILED(r.clockResult)||r.clockFrequency.QuadPart<=0||r.path!=ratePath||heldClock.Get()!=r.clockIdentity.Get())nextRate.reset();
   if(r.consistent&&r.after==State_Running&&r.sameClock&&SUCCEEDED(r.clockResult)&&r.clockFrequency.QuadPart>0)
    effective=nextRate.observe(r.end,double(r.clockQpc.QuadPart)*1000/r.clockFrequency.QuadPart,double(r.position)/10000,double(r.clockTime)/10000,ticket.generation,reinterpret_cast<uintptr_t>(r.clockIdentity.Get()));
   if(effective.discontinuity)continuity.invalidate();effective.discontinuity=continuity.invalid();
  }
  bool submitted=false;
  control->fence.publish(ticket,[&]{
   if(!s.gate.allowed()||s.gate.revision()!=revision||control->binding!=binding)return;
   if(metadata){effectiveRate.swap(nextRate);ratePath=r.path;}
   if(!r.consistent)return;
   PotTraceGraphPacket p;p.kind=metadata?1:0;p.qualified=1;p.binding=binding;p.generation=ticket.generation;p.begin=r.begin;p.at=r.end;p.state=r.after;
   p.path=r.path.data();p.pathLength=static_cast<uint32_t>(r.path.size());p.position=metadata?r.position/10000:-1;p.duration=metadata?r.duration/10000:-1;
   p.rate=effective.value.value_or(1);p.rateFlags=2|(effective.value?1:0)|(effective.changed?4:0)|(effective.discontinuity?8:0);p.rateWindowFrom=effective.from;
   submitted=s.sink.Send(p)==0;
  });
  if(metadata&&submitted){continuity.submitted(reinterpret_cast<uintptr_t>(r.clockIdentity.Get()),true);heldClock=r.clockIdentity;}
 }
 void Finished(bool metadata) noexcept override {if(!metadata)control->Close(GetTickCount64());}
};

class Collector final : public CTransInPlaceFilter {
 std::atomic<GraphLease> lease;std::shared_ptr<ObservationFence> idle=std::make_shared<ObservationFence>();
 void Retire() noexcept{auto held=lease.exchange(GraphLease{});if(held)service().runtime.Retire(held);}
 void Attach(){
  if(lease.load()||!m_pInput||!m_pOutput||!m_pGraph)return;
  ComPtr<IPin> input,output;
  // These calls are to this filter's own pins, not external graph traversal.
  if(FAILED(m_pInput->ConnectedTo(input.GetAddressOf()))||FAILED(m_pOutput->ConnectedTo(output.GetAddressOf())))return;
  auto work=std::make_shared<Watch>(input.Get(),output.Get(),m_pGraph);
  auto attached=service().runtime.Attach(work);if(!attached){++service().rejected;return;}GraphLease empty;if(!lease.compare_exchange_strong(empty,attached))service().runtime.Retire(attached);
 }
public:
 Collector(LPUNKNOWN outer,HRESULT* hr):CTransInPlaceFilter(NAME("PotTrace Collector"),outer,CLSID_PotTraceCollector,hr,false){service();}
 ~Collector() override {Retire();}
 static CUnknown* WINAPI Create(LPUNKNOWN outer,HRESULT* hr){try{return new Collector(outer,hr);}catch(...){if(hr)*hr=E_OUTOFMEMORY;return nullptr;}}
 HRESULT CheckInputType(const CMediaType* type) override{return type&&type->majortype==MEDIATYPE_Video?S_OK:VFW_E_TYPE_NOT_ACCEPTED;}
 HRESULT Transform(IMediaSample*) override{return S_OK;}
 STDMETHODIMP Pause() override {CAutoLock lock(m_pLock);Attach();auto held=lease.load();auto change=(held?held->fence:*idle).changing();return CTransInPlaceFilter::Pause();}
 STDMETHODIMP Run(REFERENCE_TIME start) override {CAutoLock lock(m_pLock);Attach();auto held=lease.load();auto change=(held?held->fence:*idle).changing();return CTransInPlaceFilter::Run(start);}
 STDMETHODIMP Stop() override {CAutoLock lock(m_pLock);Retire();return CTransInPlaceFilter::Stop();}
 STDMETHODIMP JoinFilterGraph(IFilterGraph* graph,LPCWSTR name) override{CAutoLock lock(m_pLock);if(!graph)Retire();return CTransInPlaceFilter::JoinFilterGraph(graph,name);}
 HRESULT NewSegment(REFERENCE_TIME start,REFERENCE_TIME stop,double rate) override{
  auto held=lease.load();auto change=(held?held->fence:*idle).changing();auto& s=service();
  if(held&&s.gate.allowed()){PotTraceGraphPacket p;p.kind=2;p.qualified=1;p.binding=held->binding;p.generation=held->fence.capture().generation;p.begin=p.at=GetTickCount64();p.position=start/10000;s.sink.Send(p);}
  return CTransInPlaceFilter::NewSegment(start,stop,rate);
 }
};
const AMOVIESETUP_MEDIATYPE media={&MEDIATYPE_Video,&MEDIASUBTYPE_NULL};
const AMOVIESETUP_PIN pins[]={{L"Input",FALSE,FALSE,FALSE,FALSE,&CLSID_NULL,nullptr,1,&media},{L"Output",FALSE,TRUE,FALSE,FALSE,&CLSID_NULL,nullptr,1,&media}};
const AMOVIESETUP_FILTER setup={&CLSID_PotTraceCollector,L"PotTrace Collector",MERIT_DO_NOT_USE,2,pins};
}
CFactoryTemplate g_Templates[]={{L"PotTrace Collector",&CLSID_PotTraceCollector,Collector::Create,nullptr,&setup}};
int g_cTemplates=sizeof(g_Templates)/sizeof(g_Templates[0]);
STDAPI DllRegisterServer(){return E_ACCESSDENIED;}
STDAPI DllUnregisterServer(){return E_ACCESSDENIED;}
extern "C" __declspec(dllexport) void PotTrace_CollectorHealth(unsigned long long* counters){if(!counters)return;auto h=service().runtime.Snapshot();counters[0]=h.workerCount;counters[1]=h.occupiedSlots;counters[2]=h.failures;counters[3]=service().rejected;counters[4]=service().sink.lost;counters[5]=service().gate.allowed();}
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE,DWORD,LPVOID);
BOOL APIENTRY DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved){return DllEntryPoint(instance,reason,reserved);}
