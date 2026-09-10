#pragma once
#include <d3d12.h>
namespace tsr::integration {
inline D3D12_RESOURCE_DESC ShaderTextureDesc(D3D12_RESOURCE_DESC source,D3D12_RESOURCE_FLAGS requested,
                                            uint64_t width,uint32_t height,DXGI_FORMAT format) {
    if(width && height){source.Width=width;source.Height=height;}
    // A scalar compute output derived from a depth/stencil input is a different
    // resource. R32_FLOAT cannot inherit the source's depth/stencil attachment.
    if(format==DXGI_FORMAT_R32_FLOAT && source.Format!=format &&
       (source.Flags&D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL)) {
        source.Flags=static_cast<D3D12_RESOURCE_FLAGS>(source.Flags &
            ~(D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL|D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE));
    }
    if(format!=DXGI_FORMAT_UNKNOWN)source.Format=format;
    source.Flags|=requested;
    return source;
}
inline bool SameShaderTexture(const D3D12_RESOURCE_DESC& a,const D3D12_RESOURCE_DESC& b) {
    return a.Dimension==b.Dimension && a.Alignment==b.Alignment && a.Width==b.Width && a.Height==b.Height &&
           a.DepthOrArraySize==b.DepthOrArraySize && a.MipLevels==b.MipLevels && a.Format==b.Format &&
           a.SampleDesc.Count==b.SampleDesc.Count && a.SampleDesc.Quality==b.SampleDesc.Quality &&
           a.Layout==b.Layout && a.Flags==b.Flags;
}
}
