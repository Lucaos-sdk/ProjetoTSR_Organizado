#include "input_adapter_pass.h"
#include "temporal_pass.h"
#include "output_adapter_pass.h"
#include "output_history_reference.h"
#include "test_texture_transfer.h"

int main(int argc,char** argv) {
    try {
        bool warp=false,debug=false,full=false,fp16=false,regions=false;
        for(int i=1;i<argc;++i) {
            const std::string arg=argv[i];
            if(arg=="--warp") warp=true;
            else if(arg=="--debug") debug=true;
            else if(arg=="--1080p") full=true;
            else if(arg=="--fp16") fp16=true;
            else if(arg=="--regions") regions=true;
            else throw std::invalid_argument("Usage: tsr_input_chain_dx12 [--warp] [--debug] [--1080p] [--fp16] [--regions]");
        }
        Context c(warp,debug,true);InputAdapterPass adapter(c.device.Get());TemporalPass temporal(c.device.Get(),true);
        OutputAdapterPass outputPass(c.device.Get());
        const auto finalFormat=fp16?DXGI_FORMAT_R16G16B16A16_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT;
        for (const auto format:{finalFormat,fp16?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32G32_FLOAT}) {
            D3D12_FEATURE_DATA_FORMAT_SUPPORT support{format};
            Check(c.device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support)),"Format support");
            if (!(support.Support1&D3D12_FORMAT_SUPPORT1_SHADER_LOAD) ||
                (format==finalFormat && !(support.Support2&D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE)))
                throw std::runtime_error("Requested typed format unsupported");
        }
        std::cout<<"Input/output precision: "<<(fp16?"FP16":"FP32")<<'\n';
        const tsr::Size render=full?tsr::Size{1280,720}:tsr::Size{17,9};
        const tsr::Size output=full?tsr::Size{1920,1080}:tsr::Size{26,14};
        const tsr::Size sourceSize=regions?tsr::Size{render.width+5,render.height+7}:render;
        const tsr::Size destinationSize=regions?tsr::Size{output.width+7,output.height+5}:output;
        const tsr::OutputRegion outputRegion=regions?tsr::OutputRegion{{output.width-2,output.height-2},1,1,3,2}:tsr::OutputRegion{output};
        const float sentinel[4]={-123.5f,231.f,-77.f,.5f};
        ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
        Check(c.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"Chain allocator");
        Check(c.device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"Chain list");
        constexpr unsigned frames=6,descriptorsPerFrame=13;
        ComPtr<ID3D12DescriptorHeap> heap;D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.NumDescriptors=frames*descriptorsPerFrame;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(c.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&heap)),"Chain descriptors");
        const auto stride=c.device->GetDescriptorHandleIncrementSize(hd.Type);
        ComPtr<ID3D12DescriptorHeap> clearHeap;
        D3D12_DESCRIPTOR_HEAP_DESC clearDesc=hd;clearDesc.NumDescriptors=frames;clearDesc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Check(c.device->CreateDescriptorHeap(&clearDesc,IID_PPV_ARGS(&clearHeap)),"Clear CPU descriptors");
        std::vector<std::unique_ptr<Transfer>> inputs;
        std::vector<std::unique_ptr<Transfer>> histories,geometries,finals;
        std::vector<tsr::TemporalResult> expected;
        tsr::OutputHistoryReference reference;
        tsr::FrameParameters previous{};
        // Seed resources are defined but must never be consumed on the first reset.
        std::vector<tsr::Pixel> poison(tsr::Count(output),{900,800,700,1});
        Transfer seedColor(c,list.Get(),output,DXGI_FORMAT_R32G32B32A32_FLOAT,poison.data(),16);
        Transfer seedGeometry(c,list.Get(),output,DXGI_FORMAT_R32G32B32A32_FLOAT,poison.data(),16);
        const tsr::Motion phases[]={{-.25f,-.25f},{.25f,.25f},{.25f,-.25f},{-.25f,.25f}};
        for(unsigned frame=0;frame<frames;++frame) {
            tsr::InputConversion p{};p.size=render;p.linearDepth=0;p.fixedCamera=frame!=3;p.originX=regions?2:0;p.originY=regions?3:0;
            // Exercise normal and reversed depth; convert signed normalized motion.
            p.depthA=frame%2 ? -.01f : 1.01f;p.depthB=frame%2 ? .101f : -.101f;
            p.motionScale={float(render.width),-float(render.height)};p.jitterRemoval={.5f,-.25f};p.colorScale=2;
            auto f=tsr::FrameFixture(render,output);f.parameters.frameIndex=frame;f.parameters.reset=frame==0 || frame==4;
            f.parameters.jitterPixels=phases[frame%4];f.parameters.depthReprojection=tsr::DepthReprojection::PredictedPreviousZ;
            const float z=frame<2?2.f:5.f;
            std::fill(f.depth.begin(),f.depth.end(),z);
            f.predictedPreviousDepth.assign(f.color.size(),p.fixedCamera?z:0);
            std::vector<float> rawDepth(f.color.size(),p.depthA+p.depthB/z);
            std::vector<tsr::Motion> rawMotion(f.color.size(),{1.f/float(render.width),-.5f/float(render.height)});
            auto rawColor=f.color;
            for(size_t i=0;i<f.color.size();++i) {
                rawColor[i][0]+=float(frame)*.5003f+.00013f;
                rawColor[i][3]=.3333f; f.color[i][3]=rawColor[i][3];
                for(unsigned ch=0;ch<3;++ch) f.color[i][ch]=rawColor[i][ch]*2;
                // Independent intended result: (+1,-(-.5)) minus jitter correction.
                f.motion[i]={.5f,.75f};
            }
            std::vector<uint16_t> halfColor(rawColor.size()*4),halfMotion(rawMotion.size()*2);
            if(fp16) {
                using namespace DirectX::PackedVector;
                for(size_t i=0;i<rawColor.size();++i) {
                    for(unsigned ch=0;ch<4;++ch) {
                        halfColor[i*4+ch]=XMConvertFloatToHalf(rawColor[i][ch]);
                        f.color[i][ch]=XMConvertHalfToFloat(halfColor[i*4+ch])*(ch<3?2.f:1.f);
                    }
                    for(unsigned ch=0;ch<2;++ch) {
                        halfMotion[i*2+ch]=XMConvertFloatToHalf(rawMotion[i][ch]);
                        f.motion[i][ch]=float(double(XMConvertHalfToFloat(halfMotion[i*2+ch]))*p.motionScale[ch]-p.jitterRemoval[ch]);
                    }
                }
            }
            expected.push_back(reference.Run(f));
            auto make=[&](DXGI_FORMAT format,const void* data,size_t bytes) {
                std::vector<unsigned char> padded;
                if(data && regions) {
                    padded.assign(tsr::Count(sourceSize)*bytes,0x5a);
                    for(uint32_t y=0;y<render.height;++y)
                        std::memcpy(padded.data()+((size_t(y)+p.originY)*sourceSize.width+p.originX)*bytes,
                            static_cast<const char*>(data)+size_t(y)*render.width*bytes,size_t(render.width)*bytes);
                    data=padded.data();
                }
                inputs.push_back(std::make_unique<Transfer>(c,list.Get(),data?sourceSize:render,format,data,bytes,false));
                return inputs.back()->texture.Get();
            };
            ID3D12Resource* rawC=make(finalFormat,fp16?static_cast<const void*>(halfColor.data()):rawColor.data(),fp16?8:16);
            ID3D12Resource* rawZ=make(DXGI_FORMAT_R32_FLOAT,rawDepth.data(),4);
            ID3D12Resource* rawM=make(fp16?DXGI_FORMAT_R16G16_FLOAT:DXGI_FORMAT_R32G32_FLOAT,fp16?static_cast<const void*>(halfMotion.data()):rawMotion.data(),fp16?4:8);
            ID3D12Resource* packedC=make(DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
            ID3D12Resource* packedG=make(DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16);
            ID3D12Resource* oldC=frame?histories.back()->texture.Get():seedColor.texture.Get();
            ID3D12Resource* oldG=frame?geometries.back()->texture.Get():seedGeometry.texture.Get();
            histories.push_back(std::make_unique<Transfer>(c,list.Get(),output,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16));
            geometries.push_back(std::make_unique<Transfer>(c,list.Get(),output,DXGI_FORMAT_R32G32B32A32_FLOAT,nullptr,16));
            finals.push_back(std::make_unique<Transfer>(c,list.Get(),destinationSize,finalFormat,nullptr,fp16?8:16));
            ID3D12Resource* resources[]={rawC,rawZ,rawM,packedC,packedG,packedC,packedG,oldC,oldG,histories.back()->texture.Get(),geometries.back()->texture.Get(),histories.back()->texture.Get(),finals.back()->texture.Get()};
            auto cpu=heap->GetCPUDescriptorHandleForHeapStart();cpu.ptr+=SIZE_T(frame*descriptorsPerFrame)*stride;
            for(unsigned index=0;index<descriptorsPerFrame;++index) {
                const bool uav=index==3 || index==4 || index==9 || index==10 || index==12;
                if(uav) {D3D12_UNORDERED_ACCESS_VIEW_DESC v{};v.Format=resources[index]->GetDesc().Format;v.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;c.device->CreateUnorderedAccessView(resources[index],nullptr,&v,cpu);}
                else {D3D12_SHADER_RESOURCE_VIEW_DESC v{};v.Format=resources[index]->GetDesc().Format;v.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;v.Texture2D.MipLevels=1;v.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;c.device->CreateShaderResourceView(resources[index],&v,cpu);}
                cpu.ptr+=stride;
            }
            auto at=[&](unsigned index){auto g=heap->GetGPUDescriptorHandleForHeapStart();g.ptr+=UINT64(frame*descriptorsPerFrame+index)*stride;return g;};
            adapter.Record(list.Get(),heap.Get(),at(0),at(3),p,sourceSize);
            Transition(list.Get(),packedC,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(list.Get(),packedG,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            temporal.Record(list.Get(),{heap.Get(),at(5),at(9)},f.parameters,previous,tsr::CanReuse(frame!=0,previous,f.parameters));
            Transition(list.Get(),histories.back()->texture.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            Transition(list.Get(),geometries.back()->texture.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            if(regions) {
                auto clearCpu=clearHeap->GetCPUDescriptorHandleForHeapStart();clearCpu.ptr+=SIZE_T(frame)*stride;
                D3D12_UNORDERED_ACCESS_VIEW_DESC clearView{};clearView.Format=finalFormat;clearView.ViewDimension=D3D12_UAV_DIMENSION_TEXTURE2D;
                c.device->CreateUnorderedAccessView(finals.back()->texture.Get(),nullptr,&clearView,clearCpu);
                list->ClearUnorderedAccessViewFloat(at(12),clearCpu,finals.back()->texture.Get(),sentinel,0,nullptr);
                D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=finals.back()->texture.Get();
                list->ResourceBarrier(1,&barrier);
            }
            outputPass.Record(list.Get(),heap.Get(),at(11),at(12),outputRegion,output,destinationSize);
            previous=f.parameters;
        }
        // Readbacks are recorded ONLY after all six frames have consumed their histories.
        for(unsigned frame=0;frame<frames;++frame) {
            finals[frame]->CopyBack(list.Get());
            histories[frame]->CopyBack(list.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
            geometries[frame]->CopyBack(list.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        }
        Check(list->Close(),"Chain close");ComPtr<ID3D12Fence> fence;
        Check(c.device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"Chain fence");Event event;
        ID3D12CommandList* lists[]={list.Get()};c.queue->ExecuteCommandLists(1,lists);
        Check(c.queue->Signal(fence.Get(),1),"Chain signal");Check(fence->SetEventOnCompletion(1,event.handle),"Chain completion");
        if(WaitForSingleObject(event.handle,30000)!=WAIT_OBJECT_0)throw std::runtime_error("Chain GPU wait failed");
        Check(c.device->GetDeviceRemovedReason(),"Chain device removed");c.CheckDebugMessages();
        for(unsigned frame=0;frame<frames;++frame) {
            const auto actual=histories[frame]->Read(output),geometry=geometries[frame]->Read(output);
            tsr::Verify(actual,expected[frame].output);tsr::Verify(geometry,expected[frame].geometry);
            const auto finalImage=finals[frame]->Read(destinationSize);
            for(uint32_t y=0;y<destinationSize.height;++y) for(uint32_t x=0;x<destinationSize.width;++x) {
                const bool inside=x>=outputRegion.destinationX && y>=outputRegion.destinationY &&
                    x-outputRegion.destinationX<outputRegion.size.width && y-outputRegion.destinationY<outputRegion.size.height;
                const size_t dest=size_t(y)*destinationSize.width+x;
                for(unsigned ch=0;ch<4;++ch) {
                    if(!inside) {
                        if(finalImage[dest][ch]!=sentinel[ch])throw std::runtime_error("Write outside output region");
                        continue;
                    }
                    const size_t source=size_t(y-outputRegion.destinationY+outputRegion.sourceY)*output.width+x-outputRegion.destinationX+outputRegion.sourceX;
                    const float target=fp16?DirectX::PackedVector::XMConvertHalfToFloat(DirectX::PackedVector::XMConvertFloatToHalf(actual[source][ch])):actual[source][ch];
                    const float tolerance=fp16?std::max(0x1p-24f,std::abs(target)*0x1p-10f):0.f;
                    if(!std::isfinite(finalImage[dest][ch]) || std::abs(finalImage[dest][ch]-target)>tolerance)
                        throw std::runtime_error("Caller output conversion mismatch");
                }
            }
            size_t accepted=0;
            for(size_t i=0;i<geometry.size();++i) {
                if(geometry[i][3]!=expected[frame].geometry[i][3])throw std::runtime_error("Chain history mask mismatch");
                accepted+=geometry[i][3]>0;
            }
            if((frame==1 || frame==5) ? accepted==0 : accepted!=0)throw std::runtime_error("Chain history policy oracle failed");
            std::cout<<"PASS GPU chain frame="<<frame<<" accepted_history="<<accepted<<'\n';
        }
        std::cout<<"PASS six frames, one submission, no intermediate image readback; validation harness, not game performance.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
