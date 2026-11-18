#pragma once
// Shared between the production collector and diagnostic probe. This is a
// read-only query; admission remains guarded by the caller's lifecycle fence.
struct RendererMeasurement {
 uint64_t begin=0,end=0;FILTER_STATE before=State_Stopped,after=State_Stopped;
 HRESULT stateBefore=E_FAIL,stateAfter=E_FAIL,sourceBefore=E_FAIL,sourceAfter=E_FAIL;
 HRESULT positionResult=S_OK,durationResult=S_OK,rateResult=S_OK,clockResult=E_NOINTERFACE;
 LONGLONG position=0,duration=0,clockTime=0;double rate=1;
 LARGE_INTEGER clockQpc{},clockFrequency{};ComPtr<IUnknown> clockIdentity;
 std::string path,repeated;bool sameClock=false,consistent=false;
};
inline RendererMeasurement ReadRendererMeasurement(IBaseFilter* renderer,IFileSourceFilter* source,IMediaSeeking* seeking,HWND window,bool metadata){
 RendererMeasurement r;r.begin=GetTickCount64();r.stateBefore=renderer->GetState(0,&r.before);r.path=currentFile(source,r.sourceBefore);
 QueryPerformanceFrequency(&r.clockFrequency);
 if(metadata){r.positionResult=seeking->GetCurrentPosition(&r.position);r.durationResult=seeking->GetDuration(&r.duration);r.rateResult=seeking->GetRate(&r.rate);
  ComPtr<IReferenceClock> clock,repeated;ComPtr<IUnknown> repeatedIdentity;
  if(SUCCEEDED(renderer->GetSyncSource(clock.GetAddressOf()))&&clock){clock.As(&r.clockIdentity);r.clockResult=clock->GetTime(&r.clockTime);}
  QueryPerformanceCounter(&r.clockQpc);
  if(SUCCEEDED(renderer->GetSyncSource(repeated.GetAddressOf()))&&repeated)repeated.As(&repeatedIdentity);
  r.sameClock=r.clockIdentity&&r.clockIdentity.Get()==repeatedIdentity.Get();
 }
 r.repeated=currentFile(source,r.sourceAfter);r.stateAfter=renderer->GetState(0,&r.after);r.end=GetTickCount64();
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);wchar_t cls[80]{};GetClassNameW(GetAncestor(window,GA_ROOT),cls,80);
 r.consistent=pid==GetCurrentProcessId()&&wcscmp(cls,L"PotPlayer64")==0&&r.stateBefore==S_OK&&r.stateAfter==S_OK&&r.before==r.after&&r.sourceBefore==S_OK&&r.sourceAfter==S_OK&&!r.path.empty()&&r.path==r.repeated&&r.positionResult==S_OK&&r.durationResult==S_OK&&r.rateResult==S_OK&&r.end-r.begin<=50;
 return r;
}
