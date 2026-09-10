#pragma once
#include "dx12_context.h"
#include <memory>
#include <DirectXPackedVector.h>
// Validation harness only: uploads/readbacks stay outside production passes.
struct Transfer {
    ComPtr<ID3D12Resource> texture,buffer;
    std::unique_ptr<TextureTransfer> layout;
    Transfer(Context& c,ID3D12GraphicsCommandList* list,tsr::Size size,DXGI_FORMAT format,
             const void* source,size_t pixelBytes,bool readback=true) {
        D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.DepthOrArraySize=desc.MipLevels=1;desc.SampleDesc.Count=1;
        desc.Width=size.width;desc.Height=size.height;desc.Format=format;
        desc.Flags=source ? D3D12_RESOURCE_FLAG_NONE : D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        D3D12_HEAP_PROPERTIES props{};props.Type=D3D12_HEAP_TYPE_DEFAULT;props.CreationNodeMask=props.VisibleNodeMask=1;
        Check(c.device->CreateCommittedResource(&props,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&texture)),"Adapter texture");
        layout=std::make_unique<TextureTransfer>(c.device.Get(),texture.Get());
        if (source || readback) buffer=Buffer(c.device.Get(),size_t(layout->bytes),source?D3D12_HEAP_TYPE_UPLOAD:D3D12_HEAP_TYPE_READBACK,
                      source?D3D12_RESOURCE_STATE_GENERIC_READ:D3D12_RESOURCE_STATE_COPY_DEST);
        if (source) {
            void* mapped=nullptr;const D3D12_RANGE noRead{0,0};Check(buffer->Map(0,&noRead,&mapped),"Upload map");
            for (UINT y=0;y<size.height;++y) std::memcpy(static_cast<char*>(mapped)+layout->footprint.Offset+size_t(y)*layout->footprint.Footprint.RowPitch,
                static_cast<const char*>(source)+size_t(y)*size.width*pixelBytes,size_t(size.width)*pixelBytes);
            buffer->Unmap(0,nullptr);
            Transition(list,texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_DEST);
            const auto src=layout->BufferLocation(buffer.Get()),dst=TextureTransfer::TextureLocation(texture.Get());
            list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
            Transition(list,texture.Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        } else Transition(list,texture.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    void CopyBack(ID3D12GraphicsCommandList* list,D3D12_RESOURCE_STATES before=D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        if (!buffer) throw std::logic_error("Readback not allocated");
        Transition(list,texture.Get(),before,D3D12_RESOURCE_STATE_COPY_SOURCE);
        const auto src=TextureTransfer::TextureLocation(texture.Get()),dst=layout->BufferLocation(buffer.Get());
        list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);
    }
    std::vector<tsr::Pixel> Read(tsr::Size size) {
        if (!buffer) throw std::logic_error("Readback not allocated");
        const auto desc=texture->GetDesc();const auto format=desc.Format;
        if (size.width!=desc.Width || size.height!=desc.Height)
            throw std::logic_error("Read size mismatch");
        if (format!=DXGI_FORMAT_R32G32B32A32_FLOAT && format!=DXGI_FORMAT_R16G16B16A16_FLOAT)
            throw std::logic_error("Read requires RGBA float format");
        std::vector<tsr::Pixel> result(tsr::Count(size));void* mapped=nullptr;
        const D3D12_RANGE range{0,size_t(layout->bytes)},noWrite{0,0};Check(buffer->Map(0,&range,&mapped),"Readback map");
        for (UINT y=0;y<size.height;++y) {
            const auto row=static_cast<const char*>(mapped)+layout->footprint.Offset+size_t(y)*layout->footprint.Footprint.RowPitch;
            if (format==DXGI_FORMAT_R32G32B32A32_FLOAT)
                std::memcpy(result.data()+size_t(y)*size.width,row,size_t(size.width)*sizeof(tsr::Pixel));
            else for (UINT x=0;x<size.width;++x) for (unsigned ch=0;ch<4;++ch) {
                uint16_t half;std::memcpy(&half,row+(size_t(x)*4+ch)*2,2);
                result[size_t(y)*size.width+x][ch]=DirectX::PackedVector::XMConvertHalfToFloat(half);
            }
        }
        buffer->Unmap(0,&noWrite);return result;
    }
};
