#pragma once
struct ConnectedRenderer {
 HRESULT result=E_FAIL;ComPtr<IBaseFilter> filter;HWND window=nullptr;std::vector<std::string> chain;
};
// Follow only the actual connected video stream. Do not choose a renderer from
// an incomplete graph enumeration or from another disconnected branch.
ConnectedRenderer followVideoPeer(IPin* firstPeer,bool allowPlayerConfirmedRenderer=false){
 ConnectedRenderer found;ComPtr<IPin> current;std::vector<ComPtr<IUnknown>> seen;
 for(int depth=0;depth<16;++depth){
  ComPtr<IPin> peer;if(depth==0){peer=firstPeer;if(!peer)return found;}else{found.result=current->ConnectedTo(peer.GetAddressOf());if(FAILED(found.result))return found;}
  PIN_INFO info{};found.result=peer->QueryPinInfo(&info);if(FAILED(found.result)||!info.pFilter)return found;
  ComPtr<IBaseFilter> owner;owner.Attach(info.pFilter);ComPtr<IUnknown> identity;owner.As(&identity);
  if(std::any_of(seen.begin(),seen.end(),[&](auto& item){return item.Get()==identity.Get();})){found.result=E_UNEXPECTED;return found;}
  seen.push_back(identity);found.chain.push_back(filterName(owner.Get()));
  ComPtr<IMFGetService> services;ComPtr<IMFVideoDisplayControl> display;HWND window=nullptr;
  if(SUCCEEDED(owner.As(&services))&&SUCCEEDED(services->GetService(MR_VIDEO_RENDER_SERVICE,IID_PPV_ARGS(display.GetAddressOf()))))display->GetVideoWindow(&window);
  if(!window){ComPtr<IVideoWindow> video;if(SUCCEEDED(owner.As(&video))){OAHWND value=0;if(SUCCEEDED(video->get_Owner(&value)))window=reinterpret_cast<HWND>(value);}}
  if(window){found.result=S_OK;found.filter=owner;found.window=window;return found;}
  ComPtr<IEnumPins> pins;found.result=owner->EnumPins(pins.GetAddressOf());if(FAILED(found.result))return found;
  ComPtr<IPin> pin,next;ULONG fetched=0;int count=0,candidates=0,outputs=0;HRESULT ended=S_OK;
  while(count++<32&&(ended=pins->Next(1,pin.ReleaseAndGetAddressOf(),&fetched))==S_OK){
   PIN_DIRECTION direction{};found.result=pin->QueryDirection(&direction);if(FAILED(found.result))return found;
   if(direction!=PINDIR_OUTPUT)continue;++outputs;
   CMediaType media;if(SUCCEEDED(pin->ConnectionMediaType(&media))&&media.majortype==MEDIATYPE_Video){next=pin;++candidates;}
  }
  if(allowPlayerConfirmedRenderer&&ended==S_FALSE&&outputs==0&&
     (found.chain.back()=="Built-in Direct3D11 Video Renderer"||found.chain.back()=="Built-in Direct3D 11 Video Renderer")){
   found.result=S_OK;found.filter=owner;return found;
  }
  if(ended!=S_FALSE||candidates!=1){found.result=FAILED(ended)?ended:E_UNEXPECTED;return found;}
  current=next;
 }
 found.result=E_UNEXPECTED;return found;
}

inline ConnectedRenderer followVideoOutput(IPin* output){ComPtr<IPin> peer;auto hr=output->ConnectedTo(peer.GetAddressOf());if(FAILED(hr)){ConnectedRenderer r;r.result=hr;return r;}return followVideoPeer(peer.Get());}
