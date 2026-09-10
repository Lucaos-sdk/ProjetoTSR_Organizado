#pragma once
#include "dx12_context.h"
#include "relighting_shader.h"
struct RelightingParameters {
    uint32_t width,height;
    float fx,fy,cx,cy,strength,relativeDepthLimit;
};
static_assert(sizeof(RelightingParameters)==32);
class RelightingPass {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
public:
    explicit RelightingPass(ID3D12Device* device) {
        if(!device)throw std::invalid_argument("Relighting requires a device");
        D3D12_DESCRIPTOR_RANGE ranges[2]{};
        ranges[0].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;ranges[0].NumDescriptors=2;
        ranges[1].RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_UAV;ranges[1].NumDescriptors=1;
        D3D12_ROOT_PARAMETER params[4]{};
        for(UINT i=0;i<2;++i){params[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;params[i].DescriptorTable={1,&ranges[i]};}
        params[2].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;params[2].Constants={0,0,8};
        params[3].ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;params[3].Descriptor.ShaderRegister=1;
        D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=4;desc.pParameters=params;
        ComPtr<ID3DBlob> blob,errors;
        Check(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&errors),"Relighting root serialization");
        Check(device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&root)),"Relighting root");
        auto hr=D3DCompile(kRelightingShader,std::strlen(kRelightingShader),"Relighting.hlsl",nullptr,nullptr,"main","cs_5_1",
            D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS,0,&blob,&errors);
        if(FAILED(hr)&&errors)std::cerr<<static_cast<const char*>(errors->GetBufferPointer());
        Check(hr,"Relighting compile");
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{};pso.pRootSignature=root.Get();pso.CS={blob->GetBufferPointer(),blob->GetBufferSize()};
        Check(device->CreateComputePipelineState(&pso,IID_PPV_ARGS(&pipeline)),"Relighting pipeline");
    }
    // Caller owns matching typed textures, descriptors, weights, states and fences.
    // No allocation, queue submission, readback or wait occurs in Record.
    void Record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,
                D3D12_GPU_DESCRIPTOR_HANDLE srv,D3D12_GPU_DESCRIPTOR_HANDLE uav,
                D3D12_GPU_VIRTUAL_ADDRESS weights,const RelightingParameters& p) const {
        if(!list||!heap||!weights||weights%256||!p.width||!p.height||p.width>16384||p.height>16384||
           !std::isfinite(p.fx)||!std::isfinite(p.fy)||p.fx<=0||p.fy<=0||
           !std::isfinite(p.cx)||!std::isfinite(p.cy)||!std::isfinite(p.strength)||p.strength<0||p.strength>1||
           !std::isfinite(p.relativeDepthLimit)||p.relativeDepthLimit<=0||p.relativeDepthLimit>1)
            throw std::invalid_argument("Invalid relighting bindings/projection/strength");
        ID3D12DescriptorHeap* heaps[]={heap};list->SetDescriptorHeaps(1,heaps);
        list->SetComputeRootSignature(root.Get());list->SetPipelineState(pipeline.Get());
        list->SetComputeRootDescriptorTable(0,srv);list->SetComputeRootDescriptorTable(1,uav);
        list->SetComputeRoot32BitConstants(2,8,&p,0);list->SetComputeRootConstantBufferView(3,weights);
        list->Dispatch((p.width+7)/8,(p.height+7)/8,1);
    }
};
