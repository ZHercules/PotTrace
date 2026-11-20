#pragma once
#include "../../native/PotTrace.Collector/include/RendererMeasurement.h"
// Slow seeking queries must not delay current-source/state observation.
struct RendererWatch : std::enable_shared_from_this<RendererWatch> {
 std::atomic<bool> stop{false};
 pottrace::TerminalDelivery terminal;
 std::shared_ptr<pottrace::ObservationFence> fence;
 ComPtr<IBaseFilter> renderer;ComPtr<IFileSourceFilter> source;ComPtr<IMediaSeeking> seeking,rendererSeeking;
 std::string instance;uintptr_t graph=0;HWND window=nullptr;uint64_t binding=0;
 struct Parameter {std::shared_ptr<RendererWatch> watch;bool metadata;};
 bool start(){
  HMODULE pinned=nullptr;
  if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&run),&pinned))return false;
  wchar_t value[8]{};const bool stateOnly=GetEnvironmentVariableW(L"POTTRACE_DSHOW_STATE_ONLY",value,8)&&wcscmp(value,L"1")==0;
  for(int i=0;i<(stateOnly?1:2);++i){
   auto parameter=std::make_unique<Parameter>(Parameter{shared_from_this(),i==1});
   HANDLE thread=CreateThread(nullptr,0,run,parameter.get(),0,nullptr);
   if(!thread){stop=true;return false;}parameter.release();CloseHandle(thread);
  }return true;
 }
 bool deliverTerminal(){return terminal.attempt([&](uint64_t at){return graphSink().send(3,binding,0,at,at,0);});}
 void retire(){terminal.mark(GetTickCount64());stop=true;deliverTerminal();}
 static DWORD WINAPI run(void* parameter){
  auto owned=std::unique_ptr<Parameter>(static_cast<Parameter*>(parameter));
  auto watch=owned->watch;const bool metadata=owned->metadata;owned.reset();
  const auto initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
  if(FAILED(initialized))return 1;
  std::string loggedPath;int loggedState=-9;uint64_t loggedGeneration=0,loggedAt=0;bool loggedConsistent=false;
  pottrace::EffectiveRate effectiveRate;pottrace::ClockContinuity continuity;ComPtr<IUnknown> heldClockIdentity;std::string ratePath;
  while(!watch->stop){
   const auto ticket=watch->fence->capture();const auto begin=GetTickCount64();
   try {
    auto r=ReadRendererMeasurement(watch->renderer.Get(),watch->source.Get(),watch->seeking.Get(),watch->window,metadata);
    auto [measurementBegin,end,before,after,stateBefore,stateAfter,sourceBefore,sourceAfter,positionResult,durationResult,rateResult,clockResult,position,duration,clockTime,rate,clockQpc,clockFrequency,clockIdentity,path,repeated,sameClock,consistent]=std::move(r);
    DWORD pid=0;GetWindowThreadProcessId(watch->window,&pid);
    auto& log=logger();
    std::string line="{\"event\":"+pottrace::quote(metadata?"RendererSample":"RendererState")+",\"at\":"+std::to_string(end)+",\"pid\":"+std::to_string(GetCurrentProcessId())+",\"instance\":"+pottrace::quote(watch->instance)+",\"graph\":"+std::to_string(watch->graph)+",\"binding\":"+std::to_string(watch->binding)+",\"generation\":"+std::to_string(ticket.generation)+
     ",\"state\":"+std::to_string(after)+",\"stateBeforeResult\":"+std::to_string(stateBefore)+",\"stateAfterResult\":"+std::to_string(stateAfter)+",\"result\":0,\"inputConnected\":true,\"outputConnected\":true,\"identityInspected\":true,\"inspectionStartedMs\":"+std::to_string(begin)+",\"inspectionEndedMs\":"+std::to_string(end)+
     ",\"uncertain\":"+(consistent?"false":"true")+",\"sources\":["+pottrace::quote(path)+"],\"upstreamSource\":"+pottrace::quote(path)+",\"upstreamResult\":"+std::to_string(sourceAfter)+",\"positionMs\":"+(metadata?std::to_string(position/10000):"null")+",\"durationMs\":"+(metadata?std::to_string(duration/10000):"null")+",\"rate\":"+std::to_string(rate)+
     ",\"clockResult\":"+std::to_string(clockResult)+",\"clockTime100ns\":"+std::to_string(clockTime)+",\"clockQpc\":"+std::to_string(clockQpc.QuadPart)+",\"clockFrequency\":"+std::to_string(clockFrequency.QuadPart)+",\"videoWindows\":[{\"filter\":\"observed renderer\",\"hwnd\":"+std::to_string(reinterpret_cast<uintptr_t>(watch->window))+",\"root\":"+std::to_string(reinterpret_cast<uintptr_t>(GetAncestor(watch->window,GA_ROOT)))+",\"pid\":"+std::to_string(pid)+",\"visible\":"+(IsWindowVisible(watch->window)?"true":"false")+"}],\"filters\":[],\"windowCapabilities\":[],\"lost\":"+std::to_string(log.lost.load())+",\"sinkLost\":"+std::to_string(graphSink().lost.load())+"}\n";
    auto nextRate=effectiveRate;pottrace::EffectiveRateResult effective;auto nextPath=path;
    if(metadata){
      continuity.observe(reinterpret_cast<uintptr_t>(clockIdentity.Get()),sameClock&&SUCCEEDED(clockResult)&&clockFrequency.QuadPart>0);
      if(!consistent||after!=State_Running||!sameClock||FAILED(clockResult)||clockFrequency.QuadPart<=0||path!=ratePath||heldClockIdentity.Get()!=clockIdentity.Get())nextRate.reset();
      if(consistent&&after==State_Running&&sameClock&&SUCCEEDED(clockResult)&&clockFrequency.QuadPart>0)
       effective=nextRate.observe(end,double(clockQpc.QuadPart)*1000/clockFrequency.QuadPart,double(position)/10000,double(clockTime)/10000,ticket.generation,reinterpret_cast<uintptr_t>(clockIdentity.Get()));
      if(effective.discontinuity)continuity.invalidate();
      effective.discontinuity=continuity.invalid();
    }
    bool submittedSample=false;
    watch->fence->publish(ticket,[&]{
     if(metadata){effectiveRate.swap(nextRate);ratePath.swap(nextPath);}
     if(consistent){auto result=graphSink().send(metadata?1:0,watch->binding,ticket.generation,begin,end,after,path,metadata?position/10000:-1,metadata?duration/10000:-1,effective.value.value_or(1),effective.value.has_value(),effective.changed,effective.from,effective.discontinuity);submittedSample=metadata&&result==0;}
     // Keep first observations and changes; steady state diagnostic heartbeats
     // need not contend with the logger 100 times/second. Ingestion is unchanged.
     if(metadata||path!=loggedPath||after!=loggedState||ticket.generation!=loggedGeneration||consistent!=loggedConsistent||end-loggedAt>=100){
      log.put(std::move(line));loggedPath=path;loggedState=after;loggedGeneration=ticket.generation;loggedAt=end;loggedConsistent=consistent;
     }
    });
    // COM releases and estimator allocations stay outside the lifecycle fence.
    if(submittedSample){continuity.submitted(reinterpret_cast<uintptr_t>(clockIdentity.Get()),true);heldClockIdentity=clockIdentity;}
   }catch(...){logger().lost++;}
   Sleep(metadata?25:10);
  }
  watch->terminal.mark(GetTickCount64());
  // Backpressure cannot strand a retired binding. Only workers wait; neither
  // the filter lifecycle caller nor the streaming thread waits for delivery.
  while(!watch->deliverTerminal())Sleep(10);
  watch.reset();CoUninitialize();return 0;
 }
};
