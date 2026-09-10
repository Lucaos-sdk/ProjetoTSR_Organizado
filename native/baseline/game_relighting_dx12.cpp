#include "dx12_context.h"
#include "test_texture_transfer.h"
#include "../integration/game_relighting_dx12.h"
#include <DirectXPackedVector.h>
#ifdef TSR_TEST_SUBMISSION_HOOKS
#include "../integration/submission_hooks_dx12.h"
static std::shared_ptr<tsr::integration::SubmissionObserver> hookObserver;
static tsr::integration::ResetOriginal originalReset=nullptr;
static tsr::integration::ExecuteOriginal originalExecute=nullptr;
static unsigned resetCallbacks=0,executeCallbacks=0;
static HRESULT STDMETHODCALLTYPE HookReset(ID3D12GraphicsCommandList* list,ID3D12CommandAllocator* a,ID3D12PipelineState* p) {
    const auto result=originalReset(list,a,p);++resetCallbacks;hookObserver->NotifyReset(list,SUCCEEDED(result));return result;
}
static void STDMETHODCALLTYPE HookExecute(ID3D12CommandQueue* q,UINT n,ID3D12CommandList* const* lists) {
    originalExecute(q,n,lists);++executeCallbacks;hookObserver->NotifySubmitted(q,n,lists);
}
struct HooksGuard {
    ~HooksGuard(){tsr::integration::DetachSubmissionHooks(originalReset,HookReset,originalExecute,HookExecute);}
};
#endif
static void ManualSubmit(const std::shared_ptr<tsr::integration::SubmissionObserver>& o,ID3D12CommandQueue* q,UINT n,ID3D12CommandList* const* lists) {
#ifndef TSR_TEST_SUBMISSION_HOOKS
    o->NotifySubmitted(q,n,lists);
#endif
}
static void ManualReset(const std::shared_ptr<tsr::integration::SubmissionObserver>& o,ID3D12CommandList* list,bool success) {
#ifndef TSR_TEST_SUBMISSION_HOOKS
    o->NotifyReset(list,success);
#endif
}
using tsr::integration::GameRelighting;
using tsr::integration::CameraProjection;
static void Require(bool b,const char* message){if(!b)throw std::runtime_error(message);}
static tsr::Pixel Expected(const std::vector<float>& raw,const std::vector<tsr::Pixel>& color,
                          UINT w,UINT h,UINT x,UINT y,const CameraProjection& p,float strength,float colorTransfer=1) {
    auto original=color[size_t(y)*w+x];
    if(!x||!y||x==w-1||y==h-1)return original;
    auto z=[&](UINT xx,UINT yy){float d=raw[size_t(yy)*w+xx];return std::isfinite(d)&&d>0&&d<1?p.depthB/(d-p.depthA):0.f;};
    const float center=z(x,y);if(!std::isfinite(center)||center<=0)return original;
    float l=z(x-1,y),r=z(x+1,y),u=z(x,y-1),d=z(x,y+1);
    auto valid=[center](float v){return std::isfinite(v)&&v>0&&std::abs(v-center)<=center*.02f;};
    if((!valid(l)&&!valid(r))||(!valid(u)&&!valid(d)))return original;
    auto pos=[&](float xx,float yy,float zz){return std::array<float,3>{(xx-p.cx)/p.fx*zz,(yy-p.cy)/p.fy*zz,zz};};
    auto cp=pos(float(x),float(y),center);
    auto lp=valid(l)?pos(float(x-1),float(y),l):cp,rp=valid(r)?pos(float(x+1),float(y),r):cp;
    auto up=valid(u)?pos(float(x),float(y-1),u):cp,dp=valid(d)?pos(float(x),float(y+1),d):cp;
    std::array<float,3> dx{},dy{},n{};for(unsigned i=0;i<3;++i){dx[i]=rp[i]-lp[i];dy[i]=dp[i]-up[i];}
    n={dx[1]*dy[2]-dx[2]*dy[1],dx[2]*dy[0]-dx[0]*dy[2],dx[0]*dy[1]-dx[1]*dy[0]};
    float len=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);if(!std::isfinite(len)||len<=1e-10f)return original;
    for(float& v:n)v/=n[2]<0?-len:len;
    const auto* weights=tsr::integration::GameLightWeights;
    for(unsigned ch=0;ch<3;++ch){float gain=weights[192+ch];for(unsigned i=0;i<24;++i)
        gain+=std::tanh(n[0]*weights[4*i]+n[1]*weights[4*i+1]+n[2]*weights[4*i+2]+weights[4*i+3])*weights[96+4*i+ch];
        original[ch]*=std::exp(std::clamp(gain,-2.f,2.f)*strength);}
    if(colorTransfer==0){const auto& input=color[size_t(y)*w+x];
        const double oldLuma=.2126*input[0]+.7152*input[1]+.0722*input[2];
        const double newLuma=.2126*original[0]+.7152*original[1]+.0722*original[2];
        for(int ch=0;ch<3;++ch)original[ch]=float(input[ch]*(oldLuma>0?newLuma/oldLuma:1));}
    return original;
}
int main(int argc,char** argv) {
    try {
        bool warp=argc>1&&std::string(argv[1])=="--warp";
        Context c(warp,true,true);const tsr::Size size{65,37};const size_t count=tsr::Count(size);
        // This fixture deliberately clears individual pixels to different values.
        // Ignore only the expected clear-value optimization warning; errors stay enabled.
        ComPtr<ID3D12InfoQueue> info;Check(c.device.As(&info),"Info queue");
        D3D12_MESSAGE_ID ignored[]={D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE};
        D3D12_INFO_QUEUE_FILTER filter{};filter.DenyList.NumIDs=1;filter.DenyList.pIDList=ignored;
        Check(info->AddStorageFilterEntries(&filter),"Expected clear warning filter");
        auto observer=std::make_shared<tsr::integration::SubmissionObserver>();
        GameRelighting pass(c.device.Get(),observer);
        ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
        Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)),"Allocator");
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
#ifdef TSR_TEST_SUBMISSION_HOOKS
        hookObserver=observer;HooksGuard hooksGuard;
        Require(tsr::integration::AttachSubmissionHooks(list.Get(),c.queue.Get(),originalReset,HookReset,originalExecute,HookExecute)==NO_ERROR,"Hook installation failed");
        Require(tsr::integration::AttachSubmissionHooks(list.Get(),c.queue.Get(),originalReset,HookReset,originalExecute,HookExecute)==NO_ERROR,"Hook installation is not idempotent");
#endif
        ComPtr<ID3D12Fence> fence;Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
        Event event;UINT64 value=0;
        auto submit=[&]{Check(list->Close(),"Close");ID3D12CommandList* lists[]={list.Get()};
            c.queue->ExecuteCommandLists(1,lists);ManualSubmit(observer,c.queue.Get(),1,lists);
            Check(c.queue->Signal(fence.Get(),++value),"Signal");Check(fence->SetEventOnCompletion(value,event.handle),"Event");
            Require(WaitForSingleObject(event.handle,30000)==WAIT_OBJECT_0,"Timeout");};
        auto reset=[&]{Check(alloc->Reset(),"Allocator reset");const HRESULT hr=list->Reset(alloc.Get(),nullptr);
            ManualReset(observer,list.Get(),SUCCEEDED(hr));Check(hr,"Reset");};
        double overallError=0;
        ComPtr<ID3D12Resource> lastColor,lastDepth;
        for(bool distant:{false,true})for(float colorTransfer:{0.f,1.f})for(bool nativeDepth:{false,true})for(bool half:{false,true}) {
            tsr::integration::LightingFrame lighting;lighting.colorTransfer=colorTransfer;
            lighting.sceneryProtection=distant?1.f:0.f;
            CameraProjection projection{52,51,31,17,-.000030755997f,.20000616f,1};
            std::vector<float> raw(count);std::vector<tsr::Pixel> colors(count);std::vector<uint16_t> halves(count*4);
            for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x){const size_t i=size_t(y)*size.width+x;
                float z=(distant?100.f:3.f)+.005f*float(x)+.003f*float(y);if(x>40)z+=2;
                raw[i]=projection.depthA+projection.depthB/z;
                if(x<5)raw[i]=0; // sky, both clear depth and discontinuity
                colors[i]={.3f+float(x)*.01f,.8f,.4f,float(y%4)*.25f};
                for(unsigned ch=0;ch<4;++ch){halves[4*i+ch]=DirectX::PackedVector::XMConvertFloatToHalf(colors[i][ch]);
                    if(half)colors[i][ch]=DirectX::PackedVector::XMConvertHalfToFloat(halves[4*i+ch]);}}
            if(!nativeDepth)raw[18*size.width+20]=std::numeric_limits<float>::quiet_NaN();
            Transfer color(c,list.Get(),size,half?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT,
                half?static_cast<void*>(halves.data()):static_cast<void*>(colors.data()),half?8:16);
            ComPtr<ID3D12Resource> depth;
            std::unique_ptr<Transfer> uploadedDepth;ComPtr<ID3D12DescriptorHeap> dsvHeap;
            if(!nativeDepth){uploadedDepth=std::make_unique<Transfer>(c,list.Get(),size,DXGI_FORMAT_R32_FLOAT,raw.data(),4);depth=uploadedDepth->texture;}
            else {
                D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;rd.Width=size.width;rd.Height=size.height;
                rd.DepthOrArraySize=rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Format=DXGI_FORMAT_R32G8X24_TYPELESS;rd.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
                D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_DEFAULT;hp.CreationNodeMask=hp.VisibleNodeMask=1;
                D3D12_CLEAR_VALUE clear{};clear.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
                Check(c.device->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&depth)),"Native depth");
                D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV;hd.NumDescriptors=1;
                Check(c.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&dsvHeap)),"DSV heap");
                D3D12_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D32_FLOAT_S8X24_UINT;view.ViewDimension=D3D12_DSV_DIMENSION_TEXTURE2D;
                auto handle=dsvHeap->GetCPUDescriptorHandleForHeapStart();c.device->CreateDepthStencilView(depth.Get(),&view,handle);
                for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x){D3D12_RECT rect{LONG(x),LONG(y),LONG(x+1),LONG(y+1)};
                    list->ClearDepthStencilView(handle,D3D12_CLEAR_FLAG_DEPTH,raw[size_t(y)*size.width+x],0,1,&rect);}
                Transition(list.Get(),depth.Get(),D3D12_RESOURCE_STATE_DEPTH_WRITE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            }
            submit();reset();std::string reason;
            lastColor=color.texture;lastDepth=depth;
            Require(!pass.Record(list.Get(),color.texture.Get(),depth.Get(),size.width,size.height,projection,0,reason),"Zero strength should bypass");
            Require(!pass.Record(list.Get(),color.texture.Get(),depth.Get(),size.width-1,size.height,projection,.35f,reason),"Region mismatch should bypass");
            auto* output=pass.Record(list.Get(),color.texture.Get(),depth.Get(),size.width,size.height,projection,.35f,reason,lighting);
            Require(output!=nullptr,reason.c_str());
            Require(!pass.Record(list.Get(),color.texture.Get(),depth.Get(),size.width,size.height,projection,.35f,reason)&&reason=="list_already_registered","Duplicate recording must bypass");
            TextureTransfer transfer(c.device.Get(),output);auto readback=Buffer(c.device.Get(),size_t(transfer.bytes),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
            Transition(list.Get(),output,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);
            auto src=TextureTransfer::TextureLocation(output),dst=transfer.BufferLocation(readback.Get());list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            Transition(list.Get(),output,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            submit();
            // Replay the very same recording: no descriptor/resource reuse before Reset.
            ID3D12CommandList* replay[]={list.Get()};c.queue->ExecuteCommandLists(1,replay);ManualSubmit(observer,c.queue.Get(),1,replay);
            Check(c.queue->Signal(fence.Get(),++value),"Replay signal");Check(fence->SetEventOnCompletion(value,event.handle),"Replay event");
            Require(WaitForSingleObject(event.handle,30000)==WAIT_OBJECT_0,"Replay timeout");
            void* mapped=nullptr;D3D12_RANGE range{0,size_t(transfer.bytes)},noWrite{};Check(readback->Map(0,&range,&mapped),"Readback");
            unsigned changed=0;double error=0;
            for(UINT y=0;y<size.height;++y)for(UINT x=0;x<size.width;++x){auto expected=distant?colors[size_t(y)*size.width+x]:Expected(raw,colors,size.width,size.height,x,y,projection,.35f,colorTransfer);
                auto row=static_cast<const char*>(mapped)+transfer.footprint.Offset+size_t(y)*transfer.footprint.Footprint.RowPitch;
                for(unsigned ch=0;ch<4;++ch){float actual;if(half){uint16_t bits;std::memcpy(&bits,row+(size_t(x)*4+ch)*2,2);actual=DirectX::PackedVector::XMConvertHalfToFloat(bits);}
                    else std::memcpy(&actual,row+(size_t(x)*4+ch)*4,4);
                    Require(std::isfinite(actual),"Nonfinite output");error=std::max(error,double(std::abs(actual-expected[ch])));
                    if(ch==3)Require(actual==colors[size_t(y)*size.width+x][ch],"Alpha changed");
                    if(ch<3&&std::abs(actual-colors[size_t(y)*size.width+x][ch])>.01f)++changed;}}
            readback->Unmap(0,&noWrite);
            if(distant)Require(error==0,"Native distant protection changed input");
            else Require(changed>count,"No visible learned color change");
            Require(error<(half?.002:0.0001),"GPU/CPU error");
            overallError=std::max(overallError,error);reset();c.CheckDebugMessages();
            std::cout<<"PASS native_depth="<<nativeDepth<<" fp16="<<half<<" color_transfer="<<colorTransfer<<" distant_protection="<<distant<<" error="<<error<<" changed_channels="<<changed<<'\n';
        }
        Check(list->Close(),"Final close");ManualReset(observer,list.Get(),true);
        // A GPU-side gate makes unfinished submissions deterministic. Releasing the
        // list ends replay eligibility, but must NOT free its buffers before the fence.
        ComPtr<ID3D12Fence> gate;Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate)),"Gate");
        Check(c.queue->Wait(gate.Get(),1),"Queue gate");
        struct OpenGate {ID3D12Fence* fence;~OpenGate(){fence->Signal(1);}} openGate{gate.Get()};
        std::vector<ComPtr<ID3D12CommandAllocator>> heldAllocators;
        CameraProjection p{52,51,31,17,-.000030755997f,.20000616f,1};
        std::string reason;
        for(unsigned i=0;i<6;++i) {
            ComPtr<ID3D12CommandAllocator> a;ComPtr<ID3D12GraphicsCommandList> l;
            Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&a)),"Held allocator");
            Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,a.Get(),nullptr,IID_PPV_ARGS(&l)),"Held list");
            Require(pass.Record(l.Get(),lastColor.Get(),lastDepth.Get(),size.width,size.height,p,.35f,reason)!=nullptr,"Slot unavailable before capacity");
            Check(l->Close(),"Held close");ID3D12CommandList* commands[]={l.Get()};c.queue->ExecuteCommandLists(1,commands);
            ManualSubmit(observer,c.queue.Get(),1,commands);l.Reset();heldAllocators.push_back(a);
        }
        reset();Require(!pass.Record(list.Get(),lastColor.Get(),lastDepth.Get(),size.width,size.height,p,.35f,reason)&&reason=="gpu_slots_busy",
            "Unfinished GPU slots must stay unavailable even after command-list destruction");
        Check(gate->Signal(1),"Open gate");Check(c.queue->Signal(fence.Get(),++value),"Drain signal");
        Check(fence->SetEventOnCompletion(value,event.handle),"Drain event");Require(WaitForSingleObject(event.handle,30000)==WAIT_OBJECT_0,"Drain timeout");
        Require(pass.Record(list.Get(),lastColor.Get(),lastDepth.Get(),size.width,size.height,p,.35f,reason)!=nullptr,"Completed slots must become reusable");
        submit();reset();Check(list->Close(),"Pool test close");c.CheckDebugMessages();
        std::cout<<"PASS six in-flight slots, list destruction before GPU completion, no-wait fallback and reuse after fences\n";
#ifdef TSR_TEST_SUBMISSION_HOOKS
        Require(resetCallbacks>5&&executeCallbacks>10,"Missing real hook callbacks");
        Require(tsr::integration::DetachSubmissionHooks(originalReset,HookReset,originalExecute,HookExecute)==NO_ERROR,"Hook removal failed");
        Require(!originalReset&&!originalExecute,"Hook removal left trampolines");
        std::cout<<"PASS real submission hooks without HUD/FG: reset_callbacks="<<resetCallbacks<<" execute_callbacks="<<executeCallbacks<<" manual_notifications=0\n";
#endif
        std::cout<<"PASS actual runtime raw-depth, FP16, sky, discontinuities, alpha, replay and duplicate/bypass contracts; max_error="<<overallError<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
