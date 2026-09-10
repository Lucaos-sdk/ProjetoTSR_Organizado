#include "dx12_context.h"
#ifdef TSR_FSR_LEGACY
#include "../../_OptiScaler_Source/external/FidelityFX-API-1.1.4/ffx-api/include/ffx_api/ffx_upscale.h"
#include "../../_OptiScaler_Source/external/FidelityFX-API-1.1.4/ffx-api/include/ffx_api/dx12/ffx_api_dx12.h"
#else
#include "../../_OptiScaler_Source/external/FidelityFX-SDK/Kits/FidelityFX/upscalers/include/ffx_upscale.h"
#include "../../_OptiScaler_Source/external/FidelityFX-SDK/Kits/FidelityFX/api/include/dx12/ffx_api_dx12.h"
#endif
#include <filesystem>
#include "test_texture_transfer.h"
#include "../integration/fsr_upscale_version.h"
#ifndef TSR_FSR_LEGACY
static_assert(sizeof(tsr::integration::UpscaleVersion)==sizeof(ffxCreateContextDescUpscaleVersion));
static_assert(offsetof(tsr::integration::UpscaleVersion,version)==offsetof(ffxCreateContextDescUpscaleVersion,version));
#endif

static void Require(ffxReturnCode_t result,const char* what) {
    if(result!=FFX_API_RETURN_OK)throw std::runtime_error(std::string(what)+": "+std::to_string(result));
}
int main(int argc,char** argv) {
    try {
        if(argc!=2)throw std::runtime_error("Usage: tsr_fsr_sdk_dx12 absolute-signed-upscaler.dll");
        const auto path=std::filesystem::absolute(argv[1]);
        HMODULE module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if(!module)throw std::runtime_error("Cannot load SDK: "+std::to_string(GetLastError()));
        auto query=reinterpret_cast<PfnFfxQuery>(GetProcAddress(module,"ffxQuery"));
        auto create=reinterpret_cast<PfnFfxCreateContext>(GetProcAddress(module,"ffxCreateContext"));
        auto destroy=reinterpret_cast<PfnFfxDestroyContext>(GetProcAddress(module,"ffxDestroyContext"));
        auto dispatch=reinterpret_cast<PfnFfxDispatch>(GetProcAddress(module,"ffxDispatch"));
        if(!query || !create || !destroy || !dispatch)throw std::runtime_error("SDK exports missing");
        Context c(false,false,true);
        ffxQueryDescGetVersions versions{};versions.header.type=FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
        versions.createDescType=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;versions.device=c.device.Get();
        uint64_t count=0;versions.outputCount=&count;Require(query(nullptr,&versions.header),"Query count");
        if(!count || count>64)throw std::runtime_error("Invalid provider count");
        std::vector<uint64_t> ids(count);std::vector<const char*> names(count);
        versions.versionIds=ids.data();versions.versionNames=names.data();Require(query(nullptr,&versions.header),"Query providers");
        bool tested=false;
        for(size_t i=0;i<count;++i) {
            std::cout<<"Provider "<<i<<": "<<(names[i]?names[i]:"null")<<" id="<<ids[i]<<'\n';
            if(!names[i] || std::string(names[i]).find("4.1.1")==std::string::npos)continue;
            ffxOverrideVersion chosen{};chosen.header.type=FFX_API_DESC_TYPE_OVERRIDE_VERSION;chosen.versionId=ids[i];
            ffxCreateBackendDX12Desc backend{};backend.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
            backend.device=c.device.Get();backend.header.pNext=&chosen.header;
            tsr::integration::UpscaleVersion version(&backend.header);
#ifndef TSR_FSR_LEGACY
            if(version.version!=FFX_UPSCALER_VERSION || version.header.type!=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION)
                throw std::runtime_error("Upscale ABI extension disagrees with official SDK");
#endif
            ffxCreateContextDescUpscale desc{};desc.header.type=FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE;desc.header.pNext=&version.header;
            desc.flags=FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE|FFX_UPSCALE_ENABLE_DEPTH_INVERTED|FFX_UPSCALE_ENABLE_AUTO_EXPOSURE;
            desc.maxRenderSize={1280,720};desc.maxUpscaleSize={1920,1080};
            ffxContext context=nullptr;Require(create(&context,&desc.header,nullptr),"Create 4.1.1");
            ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
            Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
            Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
            Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
            const tsr::Size render{1280,720},out{1920,1080};
            auto colors=tsr::Fixture(render);std::vector<float> depths(tsr::Count(render),0.5f);
            std::vector<std::array<float,2>> motions(tsr::Count(render),{0,0});
            Transfer color(c,list.Get(),render,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),16);
            Transfer depth(c,list.Get(),render,DXGI_FORMAT_R32_FLOAT,depths.data(),4);
            Transfer motion(c,list.Get(),render,DXGI_FORMAT_R32G32_FLOAT,motions.data(),8);
            Transfer output(c,list.Get(),out,DXGI_FORMAT_R16G16B16A16_FLOAT,nullptr,8);
            // Drain on failure before resources or SDK context can be released.
            struct Drain {Context& c;~Drain(){ComPtr<ID3D12Fence> f;if(SUCCEEDED(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&f))) && SUCCEEDED(c.queue->Signal(f.Get(),1))){Event e;if(SUCCEEDED(f->SetEventOnCompletion(1,e.handle)))WaitForSingleObject(e.handle,30000);}}} drain{c};
            for(UINT64 frame=0;frame<8;++frame) {
                if(frame){Check(allocator->Reset(),"Reset allocator");Check(list->Reset(allocator.Get(),nullptr),"Reset list");}
                ffxDispatchDescUpscale d{};d.header.type=FFX_API_DISPATCH_DESC_TYPE_UPSCALE;d.commandList=list.Get();
                d.color=ffxApiGetResourceDX12(color.texture.Get());d.depth=ffxApiGetResourceDX12(depth.texture.Get());
                d.motionVectors=ffxApiGetResourceDX12(motion.texture.Get());
                d.output=ffxApiGetResourceDX12(output.texture.Get(),FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
                d.renderSize={render.width,render.height};d.upscaleSize={out.width,out.height};d.motionVectorScale={1,1};
                d.frameTimeDelta=16.67f;d.preExposure=1;d.reset=frame==0;d.cameraNear=1000;d.cameraFar=0.1f;
                d.cameraFovAngleVertical=1;d.viewSpaceToMetersFactor=1;
                Require(dispatch(&context,&d.header),"Dispatch FSR 4.1.1");
                output.CopyBack(list.Get());Transition(list.Get(),output.texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
                Check(list->Close(),"Close list");ID3D12CommandList* commands[]={list.Get()};c.queue->ExecuteCommandLists(1,commands);
                Check(c.queue->Signal(fence.Get(),frame+1),"Signal");Event event;Check(fence->SetEventOnCompletion(frame+1,event.handle),"Wait fence");
                if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0 || fence->GetCompletedValue()==UINT64_MAX)throw std::runtime_error("GPU completion failed");
                const auto pixels=output.Read(out);double energy=0;
                for(const auto& pixel:pixels)for(unsigned ch=0;ch<3;++ch){if(!std::isfinite(pixel[ch]))throw std::runtime_error("Non-finite neural output");energy+=std::abs(pixel[ch]);}
                if(energy<1)throw std::runtime_error("Blank neural output");
                std::cout<<"PASS dispatch frame="<<frame<<" finite nonblank output; RGB sum="<<energy<<'\n';
            }
            Check(c.device->GetDeviceRemovedReason(),"Device removed");
            Require(destroy(&context,nullptr),"Destroy 4.1.1");tested=true;
        }
        if(!tested)throw std::runtime_error("FSR 4.1.1 not advertised for this device; no fallback claimed as FSR4");
        std::cout<<"PASS official FSR 4.1.1: creation, eight GPU dispatches, completed readbacks; visual quality not assessed\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
