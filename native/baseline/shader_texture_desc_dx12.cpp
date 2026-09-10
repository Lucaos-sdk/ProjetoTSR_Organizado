#include "dx12_context.h"
#include "../integration/shader_texture_desc.h"
int main(int argc,char** argv) {
    try {
        Context c(argc==2 && std::string(argv[1])=="--warp",false,true);
        auto require=[](bool ok){if(!ok)throw std::runtime_error("Texture descriptor regression");};
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        for(auto format:{DXGI_FORMAT_R32G8X24_TYPELESS,DXGI_FORMAT_R24G8_TYPELESS}) {
            D3D12_RESOURCE_DESC source{};source.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            source.Width=1476;source.Height=830;source.DepthOrArraySize=source.MipLevels=1;source.SampleDesc.Count=1;
            source.Format=format;source.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
            ComPtr<ID3D12Resource> depth;Check(c.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&source,D3D12_RESOURCE_STATE_DEPTH_WRITE,nullptr,IID_PPV_ARGS(&depth)),"Create source depth");
            // Production derives the descriptor from the existing input resource.
            // GetDesc resolves Alignment=0 to the driver's concrete alignment.
            source=depth->GetDesc();
            const auto flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET|D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS|D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
            auto old=source;old.Format=DXGI_FORMAT_R32_FLOAT;old.Flags|=flags;
            ComPtr<ID3D12Resource> rejected;
            const auto oldResult=c.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&old,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&rejected));
            if(oldResult!=E_INVALIDARG || rejected)throw std::runtime_error("Old descriptor unexpectedly accepted");
            auto corrected=tsr::integration::ShaderTextureDesc(source,flags,1920,1080,DXGI_FORMAT_R32_FLOAT);
            require(corrected.Width==1920 && corrected.Height==1080 && corrected.Flags==flags);
            ComPtr<ID3D12Resource> output;Check(c.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&corrected,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,nullptr,IID_PPV_ARGS(&output)),"Create corrected output");
            require(tsr::integration::SameShaderTexture(corrected,output->GetDesc()));
            auto different=corrected;different.Flags=D3D12_RESOURCE_FLAG_NONE;
            require(!tsr::integration::SameShaderTexture(corrected,different));
            const auto unchanged=tsr::integration::ShaderTextureDesc(source,D3D12_RESOURCE_FLAG_NONE,0,0,DXGI_FORMAT_UNKNOWN);
            require(tsr::integration::SameShaderTexture(source,unchanged));
            std::cout<<"PASS depth format="<<int(format)<<" old HRESULT=E_INVALIDARG, corrected GPU texture accepted\n";
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
