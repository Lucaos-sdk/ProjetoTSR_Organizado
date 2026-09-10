#include "input_adapter_pass.h"
#include <memory>

#include "test_texture_transfer.h"
int main(int argc,char** argv) {
    try {
        bool warp=false,debug=false;tsr::Size size{17,9};
        for(int i=1;i<argc;++i) {
            if(std::string(argv[i])=="--warp") warp=true;
            else if(std::string(argv[i])=="--debug") debug=true;
            else if(std::string(argv[i])=="--1080p") size={1920,1080};
            else throw std::invalid_argument("Usage: tsr_input_adapter_dx12 [--warp] [--debug] [--1080p]");
        }
        Context c(warp,debug,true);InputAdapterPass pass(c.device.Get());
        for (unsigned mode=0;mode<6;++mode) {
            tsr::InputConversion p{};p.size=size;p.fixedCamera=mode%2;p.linearDepth=mode<2;
            p.depthA=mode<4?1.01f:-.01f;p.depthB=mode<4?-.101f:.101f;
            p.motionScale={17,-9};p.jitterRemoval={.5f,-.25f};p.colorScale=2;
            auto colors=tsr::Fixture(p.size);auto expectedColor=colors;
            const size_t count=colors.size();std::vector<float> depth(count);std::vector<tsr::Motion> motion(count);
            std::vector<tsr::Pixel> expectedGeometry(count);
            for(size_t i=0;i<count;++i) {
                const float z=.2f+float(i%7)*.5f;
                depth[i]=p.linearDepth?z:p.depthA+p.depthB/z;motion[i]={.125f,-.25f};
                for(unsigned ch=0;ch<3;++ch) expectedColor[i][ch]*=p.colorScale;
                // Independent known motion oracle: (2.125-.5, 2.25+.25).
                expectedGeometry[i]={z,1.625f,2.5f,p.fixedCamera?z:0.f};
            }
            depth[0]=std::numeric_limits<float>::quiet_NaN();motion[1][0]=std::numeric_limits<float>::infinity();
            depth[2]=p.linearDepth?-1.f:2.f;
            for(unsigned i=0;i<3;++i) expectedGeometry[i]={0,0,0,0};
            for(size_t i=0;i<count;++i) {
                const auto cpu=tsr::ConvertGeometry(depth[i],motion[i],p);
                for(unsigned ch=0;ch<4;++ch) if(std::abs(cpu[ch]-expectedGeometry[i][ch])>1e-4) throw std::runtime_error("CPU conversion analytic oracle failed");
            }
            ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
            Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Allocator");
            Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
            Transfer color(c,list.Get(),p.size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),16);
            Transfer z(c,list.Get(),p.size,DXGI_FORMAT_R32_FLOAT,depth.data(),4);
            Transfer mv(c,list.Get(),p.size,DXGI_FORMAT_R32G32_FLOAT,motion.data(),8);
            Transfer out(c,list.Get(),p.size,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
            Transfer geom(c,list.Get(),p.size,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
            ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{};
            hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=5;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            Check(c.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Heap");
            const auto stride=c.device->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
            ID3D12Resource* resources[]={color.texture.Get(),z.texture.Get(),mv.texture.Get(),out.texture.Get(),geom.texture.Get()};
            for(unsigned i=0;i<5;++i) {
                if(i<3) {D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=resources[i]->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;c.device->CreateShaderResourceView(resources[i],&v,cpu);}
                else {D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=resources[i]->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;c.device->CreateUnorderedAccessView(resources[i],nullptr,&v,cpu);}
                cpu.ptr+=stride;
            }
            auto gpu=heap->GetGPUDescriptorHandleForHeapStart();
            pass.Record(list.Get(),heap.Get(),gpu,{gpu.ptr+UINT64(3)*stride},p,p.size);
            out.CopyBack(list.Get());geom.CopyBack(list.Get());Check(list->Close(),"Close");
            ComPtr<ID3D12Fence> fence;Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");Event event;
            ID3D12CommandList* lists[]={list.Get()};c.queue->ExecuteCommandLists(1,lists);Check(c.queue->Signal(fence.Get(),1),"Signal");
            Check(fence->SetEventOnCompletion(1,event.handle),"Completion");if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Wait failed");
            Check(c.device->GetDeviceRemovedReason(),"Device removed");c.CheckDebugMessages();
            tsr::Verify(out.Read(p.size),expectedColor);tsr::Verify(geom.Read(p.size),expectedGeometry);
            std::cout<<"PASS typed input adapter mode="<<mode<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
