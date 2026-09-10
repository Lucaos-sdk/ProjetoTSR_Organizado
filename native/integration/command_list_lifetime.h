#pragma once
#include "submission_observer.h"
#include <d3d12.h>
#include <dxgi.h>

namespace tsr::integration {
// Dedicated private-data key. This object never owns the command list itself.
inline constexpr GUID LifetimeKey{0x93b9c4cb,0x4d53,0x4692,{0x9e,0x84,0x82,0x5f,0x14,0xd2,0xd1,0x70}};
class ListLifetime final : public IUnknown {
    std::atomic<ULONG> refs{1};
    std::shared_ptr<SubmissionObserver> observer;
    ID3D12CommandList* identity;
    ~ListLifetime() {observer->NotifyDestroyed(identity);}
public:
    ListLifetime(std::shared_ptr<SubmissionObserver> o,ID3D12CommandList* l):observer(std::move(o)),identity(l){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out) override {
        if(!out)return E_POINTER;
        *out=nullptr;
        if(iid!=__uuidof(IUnknown))return E_NOINTERFACE;
        *out=static_cast<IUnknown*>(this);AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {const ULONG n=--refs;if(!n)delete this;return n;}
};
// Install ONCE, before registering work. Caller serializes this with list use.
// Never replace this private data while the command list can execute.
inline HRESULT TrackListLifetime(ID3D12CommandList* list,
                                 std::shared_ptr<SubmissionObserver> observer=SharedSubmissions()) {
    if(!list || !observer)return E_INVALIDARG;
    UINT bytes=0;
    const HRESULT existing=list->GetPrivateData(LifetimeKey,&bytes,nullptr);
    if(SUCCEEDED(existing) || bytes!=0)return HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS);
    if(existing!=DXGI_ERROR_NOT_FOUND)return existing;
    auto sentinel=new ListLifetime(std::move(observer),list);
    const HRESULT result=list->SetPrivateDataInterface(LifetimeKey,sentinel);
    sentinel->Release();
    return result;
}
}
