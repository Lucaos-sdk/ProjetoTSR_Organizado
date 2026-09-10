#pragma once
#include <d3d12.h>
#include <detours/detours.h>

namespace tsr::integration {
// The only two hooks needed for GPU ownership. No HUD/resource discovery hooks.
// Caller serializes installation/removal and supplies unwrapped native objects.
using ResetOriginal=HRESULT (STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*,ID3D12CommandAllocator*,ID3D12PipelineState*);
using ExecuteOriginal=void (STDMETHODCALLTYPE*)(ID3D12CommandQueue*,UINT,ID3D12CommandList* const*);
inline LONG AttachSubmissionHooks(ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,
    ResetOriginal& reset,ResetOriginal onReset,ExecuteOriginal& execute,ExecuteOriginal onExecute) {
    if(reset&&execute)return NO_ERROR;
    if(reset||execute||!list||!queue||!onReset||!onExecute)return ERROR_INVALID_PARAMETER;
    reset=reinterpret_cast<ResetOriginal>((*reinterpret_cast<void***>(list))[10]);
    execute=reinterpret_cast<ExecuteOriginal>((*reinterpret_cast<void***>(queue))[10]);
    LONG result=DetourTransactionBegin();
    if(result!=NO_ERROR){reset=nullptr;execute=nullptr;return result;}
    result=DetourUpdateThread(GetCurrentThread());
    if(result==NO_ERROR)result=DetourAttach(reinterpret_cast<void**>(&reset),reinterpret_cast<void*>(onReset));
    if(result==NO_ERROR)result=DetourAttach(reinterpret_cast<void**>(&execute),reinterpret_cast<void*>(onExecute));
    if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();
    if(result!=NO_ERROR){reset=nullptr;execute=nullptr;}
    return result;
}
inline LONG DetachSubmissionHooks(ResetOriginal& reset,ResetOriginal onReset,ExecuteOriginal& execute,ExecuteOriginal onExecute) {
    if(!reset&&!execute)return NO_ERROR;
    if(!reset||!execute)return ERROR_INVALID_PARAMETER;
    LONG result=DetourTransactionBegin();if(result!=NO_ERROR)return result;
    result=DetourUpdateThread(GetCurrentThread());
    if(result==NO_ERROR)result=DetourDetach(reinterpret_cast<void**>(&reset),reinterpret_cast<void*>(onReset));
    if(result==NO_ERROR)result=DetourDetach(reinterpret_cast<void**>(&execute),reinterpret_cast<void*>(onExecute));
    if(result==NO_ERROR)result=DetourTransactionCommit();else DetourTransactionAbort();
    if(result==NO_ERROR){reset=nullptr;execute=nullptr;}
    return result;
}
}
