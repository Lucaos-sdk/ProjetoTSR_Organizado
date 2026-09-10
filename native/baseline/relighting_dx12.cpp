#include "relighting_pass.h"
#include "test_texture_transfer.h"
#include <fstream>
#include <filesystem>
#include <iomanip>

template<class T> std::vector<T> ReadRaw(const std::filesystem::path& path,size_t count) {
    if(std::filesystem::file_size(path)!=count*sizeof(T))throw std::runtime_error("Invalid fixture size: "+path.string());
    std::vector<T> data(count);std::ifstream file(path,std::ios::binary);
    if(!file.read(reinterpret_cast<char*>(data.data()),count*sizeof(T)))throw std::runtime_error("Fixture read failed");
    return data;
}
int main(int argc,char** argv) {
    try {
        bool warp=false,debug=false;std::filesystem::path dir;unsigned samples=30;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--warp")warp=true;
            else if(arg=="--debug")debug=true;
            else if(arg=="--fixture"&&i+1<argc)dir=argv[++i];
            else if(arg=="--samples"&&i+1<argc)samples=unsigned(std::stoul(argv[++i]));
            else throw std::invalid_argument("Usage: --fixture DIR [--warp] [--debug] [--samples N]");
        }
        if(dir.empty()||samples<1||samples>500)throw std::invalid_argument("Fixture and 1..500 samples required");
        auto p=ReadRaw<RelightingParameters>(dir/"parameters.bin",1)[0];
        if(!p.width||!p.height||p.width>4096||p.height>4096)throw std::invalid_argument("Fixture dimensions exceed harness limit");
        const tsr::Size size{p.width,p.height};const size_t count=tsr::Count(size);
        auto colors=ReadRaw<tsr::Pixel>(dir/"color.bin",count);
        auto depth=ReadRaw<float>(dir/"depth.bin",count);
        auto expected=ReadRaw<tsr::Pixel>(dir/"expected.bin",count);
        auto weights=ReadRaw<float>(dir/"weights.bin",49*4);
        for(auto w:weights)if(!std::isfinite(w))throw std::runtime_error("Nonfinite weights");
        Context c(warp,debug,true);RelightingPass pass(c.device.Get());
        auto weightsBuffer=Buffer(c.device.Get(),1024,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_STATE_GENERIC_READ);
        void* mapped=nullptr;D3D12_RANGE noRead{0,0};Check(weightsBuffer->Map(0,&noRead,&mapped),"Map weights");
        std::memset(mapped,0,1024);std::memcpy(mapped,weights.data(),weights.size()*4);weightsBuffer->Unmap(0,nullptr);
        ComPtr<ID3D12Fence> fence;Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Fence");
        Event event;UINT64 fenceValue=0;
        auto submit=[&](ID3D12GraphicsCommandList* list){ID3D12CommandList* lists[]={list};c.queue->ExecuteCommandLists(1,lists);
            Check(c.queue->Signal(fence.Get(),++fenceValue),"Signal");Check(fence->SetEventOnCompletion(fenceValue,event.handle),"Completion");
            if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("GPU timeout");
            Check(c.device->GetDeviceRemovedReason(),"Device removed");};
        ComPtr<ID3D12CommandAllocator> alloc;ComPtr<ID3D12GraphicsCommandList> list;
        Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&alloc)),"Allocator");
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,alloc.Get(),nullptr,IID_PPV_ARGS(&list)),"List");
        Transfer color(c,list.Get(),size,DXGI_FORMAT_R32G32B32A32_FLOAT,colors.data(),16);
        Transfer z(c,list.Get(),size,DXGI_FORMAT_R32_FLOAT,depth.data(),4);
        Transfer out(c,list.Get(),size,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
        Check(list->Close(),"Close upload");submit(list.Get());Check(alloc->Reset(),"Reset allocator");Check(list->Reset(alloc.Get(),nullptr),"Reset list");
        ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors=3;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;Check(c.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Descriptors");
        const auto stride=c.device->GetDescriptorHandleIncrementSize(hd.Type);auto cpu=heap->GetCPUDescriptorHandleForHeapStart();
        for(auto resource:{color.texture.Get(),z.texture.Get()}) {D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=resource->GetDesc().Format;
            v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            c.device->CreateShaderResourceView(resource,&v,cpu);cpu.ptr+=stride;}
        D3D12_UNORDERED_ACCESS_VIEW_DESC uv{};uv.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;uv.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
        c.device->CreateUnorderedAccessView(out.texture.Get(),nullptr,&uv,cpu);
        ComPtr<ID3D12QueryHeap> query;D3D12_QUERY_HEAP_DESC qd{};qd.Type=D3D12_QUERY_HEAP_TYPE_TIMESTAMP;qd.Count=2;
        Check(c.device->CreateQueryHeap(&qd,IID_PPV_ARGS(&query)),"Timestamp heap");
        auto timing=Buffer(c.device.Get(),16,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
        UINT64 frequency=0;Check(c.queue->GetTimestampFrequency(&frequency),"Timestamp frequency");
        auto gpu=heap->GetGPUDescriptorHandleForHeapStart();
        list->EndQuery(query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0);
        pass.Record(list.Get(),heap.Get(),gpu,{gpu.ptr+2*UINT64(stride)},weightsBuffer->GetGPUVirtualAddress(),p);
        list->EndQuery(query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,1);
        list->ResolveQueryData(query.Get(),D3D12_QUERY_TYPE_TIMESTAMP,0,2,timing.Get(),0);
        out.CopyBack(list.Get());Transition(list.Get(),out.texture.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        Check(list->Close(),"Close inference");
        std::vector<double> ms;
        for(unsigned i=0;i<samples+5;++i) {submit(list.Get());UINT64* ticks=nullptr;D3D12_RANGE range{0,16};
            Check(timing->Map(0,&range,reinterpret_cast<void**>(&ticks)),"Map timing");
            const double elapsed=double(ticks[1]-ticks[0])*1000/double(frequency);timing->Unmap(0,&noRead);
            if(i>=5)ms.push_back(elapsed);}
        c.CheckDebugMessages();auto actual=out.Read(size);double maxError=0;
        for(size_t i=0;i<count;++i)for(unsigned ch=0;ch<4;++ch){
            if(!std::isfinite(actual[i][ch])||!std::isfinite(expected[i][ch]))throw std::runtime_error("Nonfinite result/reference");
            maxError=std::max(maxError,double(std::abs(actual[i][ch]-expected[i][ch])));
            if(ch==3&&actual[i][ch]!=colors[i][ch])throw std::runtime_error("Alpha changed");
            if(p.strength==0&&actual[i][ch]!=colors[i][ch])throw std::runtime_error("Bypass changed color");}
        std::ofstream output(dir/(warp?"gpu-warp.bin":"gpu.bin"),std::ios::binary);
        output.write(reinterpret_cast<const char*>(actual.data()),actual.size()*sizeof(tsr::Pixel));
        if(!output)throw std::runtime_error("Output write failed");
        std::sort(ms.begin(),ms.end());
        std::ofstream report(dir/(warp?"timing-warp.json":"timing.json"));
        report<<std::setprecision(10)<<"{\"width\":"<<p.width<<",\"height\":"<<p.height<<",\"samples\":"<<samples
            <<",\"debug_layer\":"<<(debug?"true":"false")<<",\"warp\":"<<(warp?"true":"false")<<",\"median_ms\":"<<ms[ms.size()/2]
            <<",\"p95_ms\":"<<ms[size_t(std::ceil(.95*ms.size()))-1]<<",\"max_gpu_cpu_error\":"<<maxError<<"}\n";
        if(!report)throw std::runtime_error("Report write failed");
        std::cout<<"Relighting "<<p.width<<'x'<<p.height<<" median_ms="<<ms[ms.size()/2]<<" p95_ms="<<ms[size_t(std::ceil(.95*ms.size()))-1]
                 <<" max_gpu_cpu_error="<<maxError<<'\n';
        if(maxError>2e-3)throw std::runtime_error("GPU/CPU tolerance exceeded");
        std::cout<<"PASS trained depth-guided relighting, alpha and finite output\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
